/**
 * @file    ssd1306_example.c
 * @brief   SSD1306 驱动在 STM32F407 上的使用示例（I2C1，128x64 面板）
 *
 * 接线示例（可自行修改，只需与 ssd1306_config_t 一致）：
 *   SSD1306_SCL  -> PB8 (I2C1_SCL)
 *   SSD1306_SDA  -> PB9 (I2C1_SDA)
 *   SSD1306_RES  -> PB7  （可选，无复位脚时 rst_port 填 NULL）
 *   VCC -> 3.3V   GND -> GND
 *
 * I2C 参数（CubeMX 中配置 I2C1）：
 *   Mode         : I2C
 *   Speed        : Fast Mode (400 kHz)  或 Standard Mode (100 kHz)
 *   Address Mode : 7-bit
 *   自身地址     : 不用管（主机模式）
 *
 * 把本文件加入工程后，在 main.c 中依次调用：
 *   ssd1306_demo_init();
 *   ssd1306_demo_text();
 *   ssd1306_demo_wave();
 *   ssd1306_demo_bitmap();
 *
 * 注意：所有绘图函数只修改帧缓冲，必须调用 ssd1306_refresh() 才会刷新到屏幕。
 */

#include "ssd1306.h"
#include <math.h>
#include <stdio.h>

extern I2C_HandleTypeDef hi2c1;

static ssd1306_t oled;

/* 帧缓冲：128x64 -> 128 * 8 = 1024 字节 */
static uint8_t fb[SSD1306_FB_SIZE(128, 64)];

/* 波形采样缓冲区（容量建议等于波形控件宽度） */
static int16_t wave_buf[100];
static ssd1306_wave_t wave;

/* 16x16 心形点阵（每行 2 字节，高位在左） */
static const uint8_t heart16[16 * 2] = {
    0x00, 0x00,
    0x1C, 0x38,
    0x3E, 0x7C,
    0x7F, 0xFE,
    0x7F, 0xFE,
    0x7F, 0xFE,
    0x3F, 0xFC,
    0x1F, 0xF8,
    0x0F, 0xF0,
    0x07, 0xE0,
    0x03, 0xC0,
    0x01, 0x80,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
    0x00, 0x00,
};

/* 初始化：按实际接线与面板参数修改 */
void ssd1306_demo_init(void)
{
    ssd1306_config_t cfg = {
        .hi2c     = &hi2c1,
        .i2c_addr = 0x3C,             /* SA0=0; 若地址为 0x3D 请改这里 */

        .width  = 128,                /* 面板宽度 */
        .height = 64,                 /* 面板高度（必须为 8 的倍数） */

        .rst_port = GPIOB, .rst_pin = GPIO_PIN_7,   /* 无复位脚时填 NULL */

        .contrast = 0x7F,             /* 对比度 0~255 */
        .flip_h   = 0,                /* 水平镜像 */
        .flip_v   = 0,                /* 垂直镜像 */
        .com_pins = 0,                /* 0 = 按高度自动选择 */

        .timeout = 100,
    };

    (void)ssd1306_init(&oled, &cfg, fb);
}

/* 刷新屏幕 + 写字 */
void ssd1306_demo_text(void)
{
    /* 清空帧缓冲 */
    ssd1306_clear(&oled);

    /* 写字（前景白，背景黑），scale=1 */
    ssd1306_draw_string(&oled, 0, 0, "STM32F407 + SSD1306",
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 1);

    /* 透明背景（直接在已有内容上叠加） */
    ssd1306_draw_string(&oled, 0, 12, "128x64 I2C OLED",
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT, 1);

    /* 放大 2 倍的字 */
    ssd1306_draw_string(&oled, 0, 24, "BIG",
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 2);

    /* ASCII 符号 */
    ssd1306_draw_string(&oled, 0, 44, "!@#$%^&*()_+",
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT, 1);

    /* 画框 */
    ssd1306_draw_rect(&oled, 0, 0, 128, 64, SSD1306_COLOR_WHITE);

    /* 刷新到屏幕 */
    ssd1306_refresh(&oled);
}

/* 波形显示 */
void ssd1306_demo_wave(void)
{
    ssd1306_wave_cfg_t wc = {
        .x = 0, .y = 0,
        .w = 128, .h = 64,
        .y_min = -100, .y_max = 100,
        .buf = wave_buf, .cap = 100,
        .bg_color   = SSD1306_COLOR_BLACK,
        .fg_color   = SSD1306_COLOR_WHITE,
        .grid_color = SSD1306_COLOR_WHITE,
        .axis_color = SSD1306_COLOR_WHITE,
        .show_grid  = 1,
    };
    int i;

    ssd1306_wave_init(&wave, &wc);
    ssd1306_wave_clear(&oled, &wave);

    /* 推入 100 个采样点（正弦波近似） */
    for (i = 0; i < 100; i++) {
        int16_t v = (int16_t)(80 * sin(i * 3.14159 / 25.0));
        ssd1306_wave_push(&oled, &wave, v);
    }

    /* 波形函数只改帧缓冲，最后统一刷新 */
    ssd1306_refresh(&oled);
}

/* 点阵图显示 */
void ssd1306_demo_bitmap(void)
{
    ssd1306_clear(&oled);

    /* 1bpp 点阵图：心形，放大 2 倍 -> 32x32 */
    ssd1306_draw_bitmap(&oled, 8, 8, 16, 16, heart16,
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 2);

    /* 叠加文字 */
    ssd1306_draw_string(&oled, 50, 16, "Heart",
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_TRANSPARENT, 1);

    /* 画几条线作为装饰 */
    ssd1306_draw_hline(&oled, 0, 56, 128, SSD1306_COLOR_WHITE);
    ssd1306_draw_vline(&oled, 64, 0, 64, SSD1306_COLOR_WHITE);

    ssd1306_refresh(&oled);
}

/* 简单静态波形（不保存状态，直接画一组数据） */
void ssd1306_demo_wave_simple(void)
{
    static int16_t data[100];
    int i;

    for (i = 0; i < 100; i++) {
        data[i] = (int16_t)(((i % 20) < 10) ? 60 : -60);   /* 方波 */
    }

    ssd1306_clear(&oled);
    ssd1306_draw_waveform(&oled, 0, 0, 128, 64,
                          data, 100, -100, 100,
                          SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK);
    ssd1306_refresh(&oled);
}

/* 区域刷新演示：只刷新上半屏，提高帧率 */
void ssd1306_demo_partial_refresh(void)
{
    int i;

    ssd1306_clear(&oled);
    ssd1306_draw_string(&oled, 0, 0, "Partial Refresh",
                        SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 1);
    ssd1306_refresh(&oled);

    /* 只在第 2 行（y=12~23）反复刷新数字 */
    for (i = 0; i < 100; i++) {
        char buf[16];
        ssd1306_draw_string(&oled, 0, 12, "          ",
                            SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 1);
        snprintf(buf, sizeof(buf), "cnt=%d", i);
        ssd1306_draw_string(&oled, 0, 12, buf,
                            SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 1);
        /* 只刷新 y=12 起、高 12 的区域 */
        ssd1306_refresh_region(&oled, 0, 12, 128, 12);
        HAL_Delay(50);
    }
}
