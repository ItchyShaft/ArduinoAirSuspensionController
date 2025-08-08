// SPDX-License-Identifier: MIT
// Minimal CST328 I2C touch driver for LVGL v9 on ESP32 (Arduino + ESP-IDF)
// Drop into esp32-smartdisplay (e.g. src/lvgl_touch_cst328_i2c.c)

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl.h"

// ---------- Build-time configuration (override via -D in platformio.ini) ----------
#ifndef CST328_I2C_NUM
#define CST328_I2C_NUM          I2C_NUM_0
#endif

#ifndef CST328_I2C_SDA
#define CST328_I2C_SDA          11      // Waveshare board's internal I2C SDA (per wiki)
#endif

#ifndef CST328_I2C_SCL
#define CST328_I2C_SCL          10      // Waveshare board's internal I2C SCL (per wiki)
#endif

#ifndef CST328_I2C_FREQ_HZ
#define CST328_I2C_FREQ_HZ      400000
#endif

#ifndef CST328_INT_GPIO
#define CST328_INT_GPIO         -1      // set to the real GPIO if you wire the INT pin; -1 to poll-only
#endif

#ifndef CST328_RST_GPIO
#define CST328_RST_GPIO         -1      // optional reset pin; -1 if not wired
#endif

#ifndef CST328_I2C_ADDR
#define CST328_I2C_ADDR         0x1A    // 7-bit address (datasheet default 0x34/0x35 8-bit) 
#endif

#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH           240
#endif

#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT          320
#endif

#ifndef DISPLAY_SWAP_XY
#define DISPLAY_SWAP_XY         0
#endif

#ifndef DISPLAY_MIRROR_X
#define DISPLAY_MIRROR_X        0
#endif

#ifndef DISPLAY_MIRROR_Y
#define DISPLAY_MIRROR_Y        0
#endif

// ---------- CST328 register map (normal mode) ----------
#define CST328_REG_POINTS_BASE  0xD000  // [0]: ID/Status (low nibble: 0x06 = touch), [1]: XH, [2]: YH, [3]: XYL
#define CST328_REG_FINGER_NUM   0xD005  // [bit7]=key flag, [3:0]=finger count
#define CST328_REG_MAGIC        0xD006  // 0xAB per datasheet (sanity)
#define CST328_REG_MODE_CMD     0xD109  // write 0xD1 0x09 to enter normal mode

static const char *TAG = "cst328";

static lv_indev_t *s_indev = NULL;
static bool s_i2c_inited = false;

// ---------- I2C helpers ----------
static esp_err_t i2c_write_bytes(uint8_t addr, const uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if(!cmd) return ESP_ERR_NO_MEM;

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, (uint8_t*)data, len, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(CST328_I2C_NUM, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_write_reg16(uint8_t addr, uint16_t reg, const uint8_t *payload, size_t len) {
    uint8_t buf[2 + 8]; // we only ever write tiny payloads
    if(len > 8) return ESP_ERR_INVALID_SIZE;

    buf[0] = (uint8_t)(reg >> 8);
    buf[1] = (uint8_t)(reg & 0xFF);
    if(payload && len) memcpy(&buf[2], payload, len);
    return i2c_write_bytes(addr, buf, 2 + len);
}

static esp_err_t i2c_read_reg16(uint8_t addr, uint16_t reg, uint8_t *out, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if(!cmd) return ESP_ERR_NO_MEM;

    // write 16-bit register address
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (uint8_t)(reg >> 8), true);
    i2c_master_write_byte(cmd, (uint8_t)(reg & 0xFF), true);

    // read payload
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if(len > 1) {
        i2c_master_read(cmd, out, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, out + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(CST328_I2C_NUM, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    return err;
}

// ---------- Low-level CST328 ops ----------
static esp_err_t cst328_hw_reset(void) {
    if(CST328_RST_GPIO < 0) return ESP_OK;

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CST328_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    gpio_set_level(CST328_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));   // >0.1ms per datasheet
    gpio_set_level(CST328_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300)); // TRON ~300ms
    return ESP_OK;
}

static esp_err_t cst328_enter_normal_mode(void) {
    uint8_t cmd[2] = { 0xD1, 0x09 };
    return i2c_write_reg16(CST328_I2C_ADDR, CST328_REG_MODE_CMD, cmd, sizeof(cmd));
}

static bool cst328_read_first_point(uint16_t *x, uint16_t *y, uint8_t *fingers) {
    uint8_t buf[8];
    if(i2c_read_reg16(CST328_I2C_ADDR, CST328_REG_POINTS_BASE, buf, sizeof(buf)) != ESP_OK) {
        return false;
    }
    // buf[0]: [7:4]=ID, [3:0]=status (0x06 == touch)
    // buf[1]: X high 8 bits, buf[2]: Y high 8 bits
    // buf[3]: [7:4]=X low 4 bits, [3:0]=Y low 4 bits
    // buf[5]: [7]=key flag, [3:0]=finger count
    uint8_t status = buf[0] & 0x0F;
    uint16_t xh = buf[1];
    uint16_t yh = buf[2];
    uint8_t  xy = buf[3];

    uint16_t _x = (xh << 4) | (xy >> 4);
    uint16_t _y = (yh << 4) | (xy & 0x0F);

    uint8_t n = buf[5] & 0x0F;
    if(fingers) *fingers = n;

    bool pressed = (status == 0x06) || (n > 0);
    if(!pressed) return false;

    // Map orientation
#if DISPLAY_SWAP_XY
    uint16_t tmp = _x; _x = _y; _y = tmp;
#endif
#if DISPLAY_MIRROR_X
    _x = (DISPLAY_WIDTH  > 0) ? (DISPLAY_WIDTH  - 1 - _x) : _x;
#endif
#if DISPLAY_MIRROR_Y
    _y = (DISPLAY_HEIGHT > 0) ? (DISPLAY_HEIGHT - 1 - _y) : _y;
#endif

    if(x) *x = _x;
    if(y) *y = _y;
    return true;
}

// ---------- LVGL read callback ----------
static void cst328_lvgl_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    (void)indev;

    // If INT pin is defined and high -> no new touch data (panel uses active-low IRQ)
#if (CST328_INT_GPIO >= 0)
    if(gpio_get_level(CST328_INT_GPIO) != 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
#endif

    uint16_t x=0, y=0; uint8_t fingers=0;
    if(cst328_read_first_point(&x, &y, &fingers)) {
        data->point.x = (lv_coord_t)x;
        data->point.y = (lv_coord_t)y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---------- Public init ----------
void lvgl_touch_init_cst328(void) {
    if(!s_i2c_inited) {
        i2c_config_t conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = CST328_I2C_SDA,
            .scl_io_num = CST328_I2C_SCL,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master.clk_speed = CST328_I2C_FREQ_HZ,
        };
        ESP_ERROR_CHECK(i2c_param_config(CST328_I2C_NUM, &conf));
        ESP_ERROR_CHECK(i2c_driver_install(CST328_I2C_NUM, conf.mode, 0, 0, 0));
        s_i2c_inited = true;
    }

#if (CST328_INT_GPIO >= 0)
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << CST328_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,     // INT is open-drain on many modules; adjust if needed
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
#endif

    cst328_hw_reset();
    // Optional: force normal mode (usually not required)
    (void)cst328_enter_normal_mode();

    // (Optional) Sanity read of magic 0xAB at 0xD006
    uint8_t magic = 0;
    if(i2c_read_reg16(CST328_I2C_ADDR, CST328_REG_MAGIC, &magic, 1) == ESP_OK) {
        ESP_LOGI(TAG, "CST328 magic=0x%02X", magic);
    }

    // Register LVGL input device
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, cst328_lvgl_read_cb);

    ESP_LOGI(TAG, "CST328 touch ready (addr=0x%02X, sda=%d scl=%d int=%d rst=%d)",
             CST328_I2C_ADDR, CST328_I2C_SDA, CST328_I2C_SCL, CST328_INT_GPIO, CST328_RST_GPIO);
}

// Backward-compat shim if your framework expects this symbol:
void lvgl_touch_init(void) {
    lvgl_touch_init_cst328();
}