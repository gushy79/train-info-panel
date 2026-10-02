#pragma once
/*
 * Pin mapping for the Waveshare ESP32-C6-Touch-LCD-1.47.
 *
 * Values match the board's schematic (files.waveshare.com/wiki/
 * ESP32-C6-Touch-LCD-1.47/ESP32-C6-Touch-LCD-1.47-Schematic.pdf) and the wiring
 * used in Waveshare's own official demo package -- this is board wiring, not a
 * creative choice, so it's expected to match. LCD and microSD share the SPI bus;
 * touch/IMU share I2C (per CLAUDE.md's hardware notes) -- neither touch nor SD
 * nor the IMU are brought up here, this spike only needs the display.
 */

#include "driver/gpio.h"
#include "driver/spi_master.h"

/* SPI bus (shared with microSD -- not used by this firmware) */
#define BOARD_SPI_HOST   SPI2_HOST
#define BOARD_PIN_SCLK   GPIO_NUM_1
#define BOARD_PIN_MOSI   GPIO_NUM_2
#define BOARD_PIN_MISO   GPIO_NUM_3

/* JD9853 LCD control lines */
#define BOARD_PIN_LCD_CS   GPIO_NUM_14
#define BOARD_PIN_LCD_DC   GPIO_NUM_15
#define BOARD_PIN_LCD_RST  GPIO_NUM_22
#define BOARD_PIN_LCD_BL   GPIO_NUM_23

#define BOARD_LCD_PIXEL_CLOCK_HZ (80 * 1000 * 1000)

/* Backlight PWM (LEDC) */
#define BOARD_BL_LEDC_TIMER      LEDC_TIMER_0
#define BOARD_BL_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BOARD_BL_LEDC_CHANNEL    LEDC_CHANNEL_0
#define BOARD_BL_LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define BOARD_BL_LEDC_DUTY_MAX   (1024)
#define BOARD_BL_LEDC_FREQ_HZ    (5000)

/* Panel native resolution is 172x320 portrait; this firmware runs it rotated
 * 90 degrees into 320x172 landscape (CLAUDE.md's mandated orientation). */
#define BOARD_LCD_H_RES 320
#define BOARD_LCD_V_RES 172

/* Touch (AXS5106L) shares I2C with the QMI8658A IMU; pins from the Waveshare wiki GPIO table. */
#define BOARD_PIN_TP_SDA   GPIO_NUM_18
#define BOARD_PIN_TP_SCL   GPIO_NUM_19
#define BOARD_PIN_TP_RST   GPIO_NUM_20
#define BOARD_PIN_TP_INT   GPIO_NUM_21   /* unused: the touch task polls */
#define BOARD_TP_I2C_ADDR  0x63   /* per the vendor driver; the wiki page lists 0x51, which NACKs */
#define BOARD_TP_I2C_HZ    400000
