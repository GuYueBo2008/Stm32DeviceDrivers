/**
 * @file    st7789_example.c
 * @brief   ST7789 驱动在 STM32F407 上的使用示例（SPI1，240x320 面板）
 *
 * 接线示例（可自行修改，只需与 st7789_config_t 一致）：
 *   ST7789_SCL  -> PA5 (SPI1_SCK)
 *   ST7789_SDA  -> PA7 (SPI1_MOSI)
 *   ST7789_CS   -> PA4
 *   ST7789_DC   -> PA3
 *   ST7789_RES  -> PA2
 *   ST7789_BLK  -> PA1
 *   VCC -> 3.3V   GND -> GND
 *
 * SPI 参数（CubeMX 中配置 SPI1）：
 *   Mode        : Master, 1Line TX（只发不收）
 *   Data Size   : 8 Bits
 *   Prescaler   : 4 分频以内（<= 42MHz，ST7789 一般可跑 40~80MHz）
 *   CPOL/CPHA   : Low / 1Edge
 *   NSS         : Software（由驱动控制 CS）
 *
 * 把本文件加入工程后，在 main.c 中依次调用：
 *   st7789_demo_init();
 *   st7789_demo_text();
 *   st7789_demo_wave();
 *   st7789_demo_bitmap();
 */

#include "st7789.h"

extern SPI_HandleTypeDef hspi1;

static st7789_t lcd;

/* 波形采样缓冲区（容量建议等于波形控件宽度） */
static int16_t wave_buf[200];
static st7789_wave_t wave;

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
void st7789_demo_init(void)
{
    st7789_config_t cfg = {
        .hspi = &hspi1,

        .cs_port  = GPIOA, .cs_pin  = GPIO_PIN_4,
        .dc_port  = GPIOA, .dc_pin  = GPIO_PIN_3,
        .rst_port = GPIOA, .rst_pin = GPIO_PIN_2,
        .bl_port  = GPIOA, .bl_pin  = GPIO_PIN_1,

        .width  = 240,          /* 面板物理宽度 */
        .height = 320,          /* 面板物理高度 */
        .x_offset = 0,          /* 135x240 面板填 52 / 40 */
        .y_offset = 0,

        .rotation = 1,          /* 横屏 320x240 */
        .bgr = 1,               /* 多数模块为 BGR */
        .invert = 1,            /* IPS 屏一般需要反显 */
        .bl_active_low = 0,     /* 背光高电平点亮 */
        .timeout = 100,
    };

    (void)st7789_init(&lcd, &cfg);
}

/* 刷新屏幕 + 写字 */
void st7789_demo_text(void)
{
    /* 全屏刷新为黑色 */
    st7789_fill(&lcd, ST7789_BLACK);

    /* 前景/背景色写法，scale=2 放大两倍 */
    st7789_draw_string(&lcd, 10, 10, "STM32F407 + ST7789",
                       ST7789_WHITE, ST7789_BLACK, 2);

    /* 透明背景 */
    st7789_draw_string(&lcd, 10, 35, "240x320 SPI TFT",
                       ST7789_GREEN, ST7789_COLOR_TRANSPARENT, 2);

    st7789_draw_string(&lcd, 10, 60, "ASCII: !@#$%^&*()_+",
                       ST7789_YELLOW, ST7789_COLOR_TRANSPARENT, 1);

    /* 大号字 */
    st7789_draw_string(&lcd, 10, 80, "BIG", ST7789_RED, ST7789_BLACK, 4);

    /* 画框 */
    st7789_draw_rect(&lcd, 4, 4, 312, 112, ST7789_NAVY);
}

/* 波形显示 */
void st7789_demo_wave(void)
{
    st7789_wave_cfg_t wc = {
        .x = 10, .y = 120,
        .w = 180, .h = 95,
        .y_min = -100, .y_max = 100,
        .buf = wave_buf, .cap = 200,
        .bg_color = ST7789_BLACK,
        .fg_color = ST7789_CYAN,
        .grid_color = 0x0320,           /* 暗绿色网格 */
        .axis_color = ST7789_GRAY,
        .show_grid = 1,
    };
    int i;

    st7789_wave_init(&wave, &wc);
    st7789_wave_clear(&lcd, &wave);

    /* 推入 200 个采样点（这里用三角波演示） */
    for (i = 0; i < 200; i++) {
        int16_t v = (i < 100) ? (int16_t)((i - 50) * 2) : (int16_t)((150 - i) * 2);
        st7789_wave_push(&lcd, &wave, v);
    }
}

/* 点阵图显示 */
void st7789_demo_bitmap(void)
{
    uint16_t icon[8 * 8];
    uint16_t y, x;

    /* 1bpp 点阵图：心形，放大 2 倍 -> 32x32 */
    st7789_draw_bitmap_1bpp(&lcd, 210, 125, 16, 16, heart16,
                            ST7789_RED, ST7789_BLACK, 2);

    /* RGB565 位图：动态生成一个 8x8 渐变色块 */
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            icon[y * 8 + x] = ST7789_RGB565((uint8_t)(x * 32),
                                            (uint8_t)(y * 32), 128);
        }
    }
    st7789_draw_bitmap(&lcd, 210, 170, 8, 8, icon);

    /* 区域刷新演示：底部 320x15 彩条 */
    {
        static uint16_t bars[320 * 15];
        int i;
        for (i = 0; i < 320 * 15; i++) {
            bars[i] = ST7789_RGB565((uint8_t)((i % 320) * 255 / 319), 0, 0);
        }
        st7789_flush(&lcd, 0, 225, 320, 15, bars);
    }
}

/* 简单静态波形（不保存状态，直接画一组数据） */
void st7789_demo_wave_simple(void)
{
    static int16_t data[100];
    int i;

    for (i = 0; i < 100; i++) {
        data[i] = (int16_t)(((i % 20) < 10) ? 60 : -60);   /* 方波 */
    }
    st7789_draw_waveform(&lcd, 210, 120, 100, 60,
                         data, 100, -100, 100,
                         ST7789_ORANGE, ST7789_BLACK);
}
