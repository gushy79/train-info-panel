#include "touch/axs5106.h"

#include <stdio.h>
#include <string.h>

#include "display/board_pins.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define REG_POINTS 0x01
#define READ_LEN 14
#define I2C_TIMEOUT_MS 20

static const char *TAG = "axs5106";
static i2c_master_dev_handle_t s_dev;
static volatile int64_t s_last_int_us;   /* when the interrupt line last fell */
static volatile uint32_t s_int_edges;
static bool s_active;                     /* a finger was down on the previous read */
static int64_t s_last_idle_read_us;
static uint32_t s_reads_ok, s_reads_nack;

#define INT_WINDOW_US 60000      /* the controller answers reads for a short while after it signals */
#define IDLE_POLL_US 100000      /* backstop: look every 100 ms even if the interrupt never fires */

static void IRAM_ATTR on_touch_int(void *arg)
{
    (void)arg;
    s_last_int_us = esp_timer_get_time();
    s_int_edges++;
}

void axs5106_stats(uint32_t *int_edges, uint32_t *reads_ok, uint32_t *reads_nack)
{
    *int_edges = s_int_edges;
    *reads_ok = s_reads_ok;
    *reads_nack = s_reads_nack;
}

esp_err_t axs5106_init(void)
{
    /* Reset pulse, then give the controller time to come up before the first read. */
    gpio_config_t rst = { .pin_bit_mask = 1ULL << BOARD_PIN_TP_RST, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(BOARD_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    /* The controller NACKs reads while idle; it signals touches on INT (active low). Read only then. */
    gpio_config_t intr = { .pin_bit_mask = 1ULL << BOARD_PIN_TP_INT, .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE };
    ESP_ERROR_CHECK(gpio_config(&intr));
    esp_err_t isr = gpio_install_isr_service(0);
    if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) return isr;   /* already installed is fine */
    ESP_ERROR_CHECK(gpio_isr_handler_add(BOARD_PIN_TP_INT, on_touch_int, NULL));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_PIN_TP_SDA,
        .scl_io_num = BOARD_PIN_TP_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* the board has pull-ups; this is only a backstop */
    };
    i2c_master_bus_handle_t bus;
    esp_err_t e = i2c_new_master_bus(&bus_cfg, &bus);
    if (e != ESP_OK) return e;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_TP_I2C_ADDR,
        .scl_speed_hz = BOARD_TP_I2C_HZ,
    };
    e = i2c_master_bus_add_device(bus, &dev_cfg, &s_dev);
    if (e != ESP_OK) return e;

    /* Presence check by address probe: a data read is NACKed while the controller is idle. */
    e = i2c_master_probe(bus, BOARD_TP_I2C_ADDR, I2C_TIMEOUT_MS);
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "touch controller found at 0x%02x", BOARD_TP_I2C_ADDR);
        esp_log_level_set("i2c.master", ESP_LOG_NONE);   /* a NACK between touches is normal, not an error */
        return e;
    }
    /* Not answering: say which addresses do, so a wrong address is obvious from the log. */
    char found[96] = "";
    for (int a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(bus, a, I2C_TIMEOUT_MS) == ESP_OK) {
            size_t n = strlen(found);
            snprintf(found + n, sizeof found - n, " 0x%02x", a);
        }
    }
    ESP_LOGW(TAG, "no answer at 0x%02x (%s); devices on the bus:%s", BOARD_TP_I2C_ADDR, esp_err_to_name(e), found[0] ? found : " none");
    return e;
}

bool axs5106_read(uint16_t *x, uint16_t *y)
{
    /* Read at full rate while a finger is down or the controller has just signalled; otherwise only
     * every IDLE_POLL_US (an idle controller NACKs, which is normal and not logged). */
    int64_t now = esp_timer_get_time();
    bool signalled = (now - s_last_int_us) <= INT_WINDOW_US;
    if (!s_active && !signalled) {
        if (now - s_last_idle_read_us < IDLE_POLL_US) return false;
        s_last_idle_read_us = now;
    }
    /* Register address and data are two separate transactions with a STOP between (as in Waveshare's
     * driver): this controller NACKs a repeated-start read, which is what i2c_master_transmit_receive does. */
    uint8_t reg = REG_POINTS, b[READ_LEN];
    esp_err_t e = i2c_master_transmit(s_dev, &reg, 1, I2C_TIMEOUT_MS);
    if (e == ESP_OK) e = i2c_master_receive(s_dev, b, sizeof b, I2C_TIMEOUT_MS);
    if (e != ESP_OK) s_reads_nack++; else s_reads_ok++;
    if (e != ESP_OK || (b[1] & 0x0F) == 0) {
        s_active = false;
        return false;
    }
    s_active = true;
    *x = (uint16_t)(((b[2] & 0x0F) << 8) | b[3]);
    *y = (uint16_t)(((b[4] & 0x0F) << 8) | b[5]);
    return true;
}
