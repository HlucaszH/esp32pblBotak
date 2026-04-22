// impedance_meter.c
// Miernik impedancji - metoda trzech woltomierzy
// ESP32-C3 SuperMini + OLED 0.96" SSD1306 128x64

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "driver/i2c.h"

// ===================== KONFIGURACJA =====================

// GPIO0 = ADC1_CH0 -> U  (napięcie całkowite)
// GPIO1 = ADC1_CH1 -> UN (napięcie na RN)
// GPIO2 = ADC1_CH2 -> UX (napięcie na ZX)
#define ADC_CH_U    ADC1_CHANNEL_0
#define ADC_CH_UN   ADC1_CHANNEL_1
#define ADC_CH_UX   ADC1_CHANNEL_2
#define ADC_ATTEN   ADC_ATTEN_DB_12
#define ADC_SAMPLES 64

// Rezystancja wzorcowa [Ohm] - zmień na rzeczywistą wartość RN!
#define RN 100.0f

// I2C - OLED
#define I2C_SCL_IO   8
#define I2C_SDA_IO   10
#define I2C_NUM      I2C_NUM_0
#define I2C_FREQ_HZ  400000
#define OLED_ADDR    0x3C
#define OLED_W       128
#define OLED_PAGES   8

// ===================== BUFOR OLED =====================
static uint8_t oled_buf[OLED_W * OLED_PAGES];

// ===================== FONT 8x8 - TYLKO UŻYTE ZNAKI =====================
// Indeksowane przez ASCII, NULL = nieużywany znak (wyświetli spację)
// Używane: spacja(32) .(46) :(58) 0-9(48-57) k(107) R(82) Z(90) O(79)

typedef struct { uint8_t ascii; uint8_t bmp[8]; } Glyph;

static const Glyph glyphs[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
    {'.', {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}},
    {':', {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}},
    {'0', {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}},
    {'1', {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}},
    {'2', {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}},
    {'3', {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}},
    {'4', {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}},
    {'5', {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}},
    {'6', {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}},
    {'7', {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}},
    {'8', {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}},
    {'9', {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}},
    {'O', {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}},
    {'R', {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}},
    {'Z', {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}},
    {'k', {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}},
};
#define GLYPHS_COUNT (sizeof(glyphs) / sizeof(glyphs[0]))

// ===================== I2C / OLED =====================

static esp_adc_cal_characteristics_t adc_chars;

static esp_err_t i2c_init(void) {
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_IO,
        .scl_io_num       = I2C_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_NUM, &conf);
    return i2c_driver_install(I2C_NUM, conf.mode, 0, 0, 0);
}

static void oled_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x00, cmd};
    i2c_master_write_to_device(I2C_NUM, OLED_ADDR, buf, 2, pdMS_TO_TICKS(10));
}

static void oled_init(void) {
    vTaskDelay(pdMS_TO_TICKS(100));
    const uint8_t init_seq[] = {
        0xAE,               // display off
        0xD5, 0x80,         // clock div
        0xA8, 0x3F,         // multiplex
        0xD3, 0x00,         // offset
        0x40,               // start line
        0x8D, 0x14,         // charge pump on
        0x20, 0x00,         // horizontal addr mode
        0xA1,               // seg remap
        0xC8,               // com scan dir
        0xDA, 0x12,         // com pins
        0x81, 0xCF,         // contrast
        0xD9, 0xF1,         // precharge
        0xDB, 0x40,         // vcomh
        0xA4,               // display from RAM
        0xA6,               // normal (not inverted)
        0xAF                // display on
    };
    for (size_t i = 0; i < sizeof(init_seq); i++) oled_cmd(init_seq[i]);
}

static void oled_flush(void) {
    oled_cmd(0x21); oled_cmd(0x00); oled_cmd(0x7F); // col 0-127
    oled_cmd(0x22); oled_cmd(0x00); oled_cmd(0x07); // page 0-7
    uint8_t tmp[33];
    tmp[0] = 0x40;
    for (int i = 0; i < OLED_W * OLED_PAGES; i += 32) {
        memcpy(&tmp[1], &oled_buf[i], 32);
        i2c_master_write_to_device(I2C_NUM, OLED_ADDR, tmp, 33, pdMS_TO_TICKS(20));
    }
}

static void oled_clear(void) {
    memset(oled_buf, 0, sizeof(oled_buf));
}

static const uint8_t* find_glyph(char c) {
    for (size_t i = 0; i < GLYPHS_COUNT; i++)
        if (glyphs[i].ascii == (uint8_t)c) return glyphs[i].bmp;
    return glyphs[0].bmp; // fallback: spacja
}

// col: 0-15, page: 0-7
static void oled_draw_char(int col, int page, char c) {
    const uint8_t *g = find_glyph(c);
    int x = col * 8;
    for (int px = 0; px < 8 && (x + px) < OLED_W; px++)
        oled_buf[page * OLED_W + x + px] = g[px];
}

static void oled_draw_str(int col, int page, const char *str) {
    while (*str && col < 16)
        oled_draw_char(col++, page, *str++);
}

// Pozioma linia na górze danej strony (1 piksel)
static void oled_hline(int page) {
    for (int i = 0; i < OLED_W; i++)
        oled_buf[page * OLED_W + i] |= 0x01;
}

// ===================== ADC =====================

static void adc_init(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC_CH_U,  ADC_ATTEN);
    adc1_config_channel_atten(ADC_CH_UN, ADC_ATTEN);
    adc1_config_channel_atten(ADC_CH_UX, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN,
                              ADC_WIDTH_BIT_12, 1100, &adc_chars);
}

static float adc_read_mv(adc1_channel_t ch) {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) sum += adc1_get_raw(ch);
    return (float)esp_adc_cal_raw_to_voltage(sum / ADC_SAMPLES, &adc_chars);
}

// ===================== OBLICZENIA =====================

// Formatuje wartość Ohm -> "123.4" lub "1.23k", wpisuje do buf
// Zwraca wskaźnik na buf
static const char* fmt_ohm(char *buf, int len, float val) {
    if (fabsf(val) >= 1000.0f)
        snprintf(buf, len, "%.2fk", val / 1000.0f);
    else
        snprintf(buf, len, "%.1f", val);
    return buf;
}

// ===================== GŁÓWNA PĘTLA =====================

void app_main(void) {
    ESP_ERROR_CHECK(i2c_init());
    oled_init();
    adc_init();

    char zx_str[12], rx_str[12];
    char line[18];

    while (1) {
        float U  = adc_read_mv(ADC_CH_U);
        float UN = adc_read_mv(ADC_CH_UN);
        float UX = adc_read_mv(ADC_CH_UX);

        oled_clear();

        if (UN < 10.0f || UX < 10.0f || U < 10.0f) {
            // Brak sygnału
            oled_draw_str(1, 3, "No signal");
        } else {
            // Zx = RN * (UX / UN)
            float Zx = RN * (UX / UN);

            // cos(phi) = (U^2 - UN^2 - UX^2) / (2 * UN * UX)
            float cos_phi = (U*U - UN*UN - UX*UX) / (2.0f * UN * UX);
            if (cos_phi >  1.0f) cos_phi =  1.0f;
            if (cos_phi < -1.0f) cos_phi = -1.0f;

            // Rx = Zx * cos(phi)
            float Rx = Zx * cos_phi;

            // Wyświetl Zx (page 1-2) i Rx (page 4-5)
            oled_hline(1);
            snprintf(line, sizeof(line), "Zx: %s", fmt_ohm(zx_str, sizeof(zx_str), Zx));
            oled_draw_str(0, 2, line);

            oled_hline(4);
            snprintf(line, sizeof(line), "Rx: %s", fmt_ohm(rx_str, sizeof(rx_str), Rx));
            oled_draw_str(0, 5, line);
        }

        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}