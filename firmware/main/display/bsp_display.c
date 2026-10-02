#include "bsp_display.h"
#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"

#include "esp_lcd_jd9853.h"
#include "esp_lcd_panel_vendor.h"

#include "esp_log.h"

static const char *TAG = "bsp_display";
static uint8_t s_brightness_percent = 0;
static esp_lcd_panel_handle_t s_panel;

void bsp_spi_init(void)
{
    ESP_LOGI(TAG, "SPI bus init");
    spi_bus_config_t buscfg = {
        .sclk_io_num = BOARD_PIN_SCLK,
        .mosi_io_num = BOARD_PIN_MOSI,
        .miso_io_num = BOARD_PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(BOARD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
}

void bsp_display_init(esp_lcd_panel_io_handle_t *io_handle,
                       esp_lcd_panel_handle_t *panel_handle,
                       size_t max_transfer_sz)
{
    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_spi_config_t io_config =
        JD9853_PANEL_IO_SPI_CONFIG(BOARD_PIN_LCD_CS, BOARD_PIN_LCD_DC, NULL, NULL);
    io_config.pclk_hz = BOARD_LCD_PIXEL_CLOCK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_SPI_HOST, &io_config, io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BOARD_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    esp_lcd_new_panel_jd9853(*io_handle, &panel_config, panel_handle);

    ESP_ERROR_CHECK(esp_lcd_panel_reset(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(*panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(*panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(*panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(*panel_handle, true));

    s_panel = *panel_handle;
    (void)max_transfer_sz; /* sized via io_config's SPI queue depth, not used directly here */
}

void bsp_display_brightness_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = BOARD_BL_LEDC_MODE,
        .timer_num = BOARD_BL_LEDC_TIMER,
        .duty_resolution = BOARD_BL_LEDC_DUTY_RES,
        .freq_hz = BOARD_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = BOARD_BL_LEDC_MODE,
        .channel = BOARD_BL_LEDC_CHANNEL,
        .timer_sel = BOARD_BL_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BOARD_PIN_LCD_BL,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

void bsp_display_set_brightness(uint8_t brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
        ESP_LOGE(TAG, "Brightness value out of range, clamped to 100");
    }
    s_brightness_percent = brightness_percent;
    uint32_t duty = (brightness_percent * (BOARD_BL_LEDC_DUTY_MAX - 1)) / 100;
    ESP_ERROR_CHECK(ledc_set_duty(BOARD_BL_LEDC_MODE, BOARD_BL_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(BOARD_BL_LEDC_MODE, BOARD_BL_LEDC_CHANNEL));
    ESP_LOGI(TAG, "Backlight set to %d%%", brightness_percent);
}

void bsp_display_set_power(bool on)
{
    if (!s_panel) return;
    if (on) {
        esp_lcd_panel_disp_on_off(s_panel, true);
        bsp_display_set_brightness(100);
    } else {
        bsp_display_set_brightness(0);
        esp_lcd_panel_disp_on_off(s_panel, false);
    }
}
