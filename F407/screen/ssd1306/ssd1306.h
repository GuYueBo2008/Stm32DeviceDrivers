/**
 * @file    ssd1306.h
 * @brief   SSD1306 I2C OLED 驱动（STM32F407 + HAL 库）
 *
 * 支持 128x64、128x32、96x16、64x48 等常见 SSD1306 面板，I2C 接口、地址、
 * 分辨率、对比度、镜像方向均可配置。驱动采用“显存帧缓冲 + 整屏/区域刷新”模式：
 * 所有绘图函数在 RAM 帧缓冲上操作，调用 ssd1306_refresh() 后才真正刷新到屏幕。
 *
 * 功能：
 *   - 初始化 / 开关显示 / 对比度 / 反显 / 镜像
 *   - 全屏或任意区域刷新（I2C 连续发送）
 *   - 画点、线、矩形（自动裁剪）
 *   - ASCII 5x7 字库写字（可缩放、可透明背景）
 *   - 波形显示（网格、零轴、自动缩放、滚动刷新）
 *   - 1bpp 单色点阵图显示（可放大）
 *
 * 使用示例：
 *   static ssd1306_t oled;
 *   static uint8_t fb[SSD1306_FB_SIZE(128, 64)];
 *   ssd1306_config_t cfg = {
 *       .hi2c     = &hi2c1,
 *       .i2c_addr = 0x3C,
 *       .width    = 128, .height = 64,
 *       .contrast = 0x7F, .timeout = 100,
 *   };
 *   ssd1306_init(&oled, &cfg, fb);
 *   ssd1306_clear(&oled);
 *   ssd1306_draw_string(&oled, 0, 0, "Hello SSD1306",
 *                       SSD1306_COLOR_WHITE, SSD1306_COLOR_BLACK, 1);
 *   ssd1306_refresh(&oled);
 *
 * 说明：
 *   - 仅支持 ASCII 可见字符（0x20~0x7E）的 5x7 字库；如需中文请自行替换字库。
 *   - 帧缓冲由用户提供（见 SSD1306_FB_SIZE 宏），驱动不做动态内存分配。
 *   - I2C 地址为 7 位地址（0x3C 或 0x3D），驱动内部自动左移处理读写位。
 *   - 若模块带硬件复位脚，把 rst_port/rst_pin 填上即可；无复位脚置 NULL。
 */

#ifndef __SSD1306_H__
#define __SSD1306_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ---------------- 单色颜色 ---------------- */
#define SSD1306_COLOR_BLACK    0U   /**< 像素熄灭 */
#define SSD1306_COLOR_WHITE    1U   /**< 像素点亮 */
#define SSD1306_COLOR_INVERSE  2U   /**< 翻转当前像素（XOR） */

/** 文字背景使用该值时表示“透明背景” */
#define SSD1306_COLOR_TRANSPARENT (-1)

/** 计算帧缓冲大小（字节），用于用户声明缓冲区 */
#define SSD1306_FB_SIZE(width, height) ((uint32_t)(width) * ((uint32_t)(height) / 8U))

/* ---------------- 配置 / 句柄 ---------------- */

/**
 * @brief SSD1306 配置参数
 */
typedef struct {
    I2C_HandleTypeDef *hi2c;       /**< I2C 句柄，如 &hi2c1（必填） */
    uint16_t i2c_addr;             /**< 7 位 I2C 地址，常见 0x3C（SA0=0）或 0x3D（SA0=1） */

    uint16_t width;                /**< 面板宽度（像素），如 128 */
    uint16_t height;               /**< 面板高度（像素），如 64、32、16、48 */

    GPIO_TypeDef *rst_port;        /**< 复位端口，NULL 表示无硬件复位脚 */
    uint16_t      rst_pin;         /**< 复位引脚 */

    uint8_t  contrast;             /**< 对比度 0~255，建议 0x7F */
    uint8_t  flip_h;               /**< 1: 水平镜像（列重映射） */
    uint8_t  flip_v;               /**< 1: 垂直镜像（行扫描方向） */
    uint8_t  com_pins;             /**< COM 引脚配置，0 表示按高度自动选择（0x02 或 0x12） */

    uint32_t timeout;              /**< I2C 传输超时（ms），0 则默认 100 */
} ssd1306_config_t;

/** @brief SSD1306 驱动句柄 */
typedef struct {
    ssd1306_config_t cfg;          /**< 配置副本 */
    uint8_t  *fb;                  /**< 帧缓冲指针（由用户提供） */
    uint16_t  fb_size;             /**< 帧缓冲大小 = width * (height/8) */
    uint16_t  pages;               /**< 页数 = height / 8 */
} ssd1306_t;

/* ---------------- 初始化 / 控制 ---------------- */

/**
 * @brief  初始化 SSD1306
 * @param  dev          驱动句柄
 * @param  cfg          配置参数（函数内会拷贝）
 * @param  framebuffer  用户提供的帧缓冲，大小为 SSD1306_FB_SIZE(width, height)
 * @retval HAL_OK 成功，HAL_ERROR 参数错误
 */
HAL_StatusTypeDef ssd1306_init(ssd1306_t *dev, const ssd1306_config_t *cfg,
                               uint8_t *framebuffer);

/** @brief 开关显示（on 非 0 为开） */
void ssd1306_set_display_on(ssd1306_t *dev, uint8_t on);

/** @brief 设置对比度 0~255 */
void ssd1306_set_contrast(ssd1306_t *dev, uint8_t contrast);

/** @brief 反显开关（invert 非 0 为反显，不影响帧缓冲内容） */
void ssd1306_set_invert(ssd1306_t *dev, uint8_t invert);

/* ---------------- 帧缓冲 / 刷新 ---------------- */

/** @brief 清空帧缓冲（所有像素熄灭） */
void ssd1306_clear(ssd1306_t *dev);

/** @brief 填充帧缓冲（color: SSD1306_COLOR_BLACK/WHITE） */
void ssd1306_fill(ssd1306_t *dev, uint8_t color);

/**
 * @brief  把整个帧缓冲刷新到屏幕
 * @note   所有绘图函数只修改帧缓冲，需调用本函数才会显示。
 */
void ssd1306_refresh(ssd1306_t *dev);

/**
 * @brief  刷新指定区域的帧缓冲到屏幕
 * @param  x,y,w,h 区域坐标与尺寸（会自动对齐到页边界，越界会裁剪）
 * @note   区域刷新仍按页发送；若性能敏感，优先使用 ssd1306_refresh()。
 */
void ssd1306_refresh_region(ssd1306_t *dev, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h);

/* ---------------- 基本绘图（操作帧缓冲） ---------------- */

void ssd1306_draw_pixel(ssd1306_t *dev, int16_t x, int16_t y, uint8_t color);
void ssd1306_draw_hline(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, uint8_t color);
void ssd1306_draw_vline(ssd1306_t *dev, int16_t x, int16_t y, int16_t h, uint8_t color);
void ssd1306_draw_rect(ssd1306_t *dev, int16_t x, int16_t y,
                       int16_t w, int16_t h, uint8_t color);
void ssd1306_draw_line(ssd1306_t *dev, int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1, uint8_t color);

/* ---------------- 文字 ---------------- */

/**
 * @brief  绘制一个 ASCII 字符（5x7 字库）
 * @param  x,y    左上角坐标
 * @param  c      字符（0x20~0x7E，其他显示为 '?'）
 * @param  fg     前景色（SSD1306_COLOR_BLACK/WHITE/INVERSE）
 * @param  bg     背景色；传 SSD1306_COLOR_TRANSPARENT(-1) 表示透明
 * @param  scale  放大倍数 1~8
 * @retval 字符宽度（像素）
 */
uint16_t ssd1306_draw_char(ssd1306_t *dev, int16_t x, int16_t y, char c,
                           uint8_t fg, int32_t bg, uint8_t scale);

/**
 * @brief  绘制字符串（支持 '\n' 换行，仅 ASCII）
 * @param  x,y    左上角坐标
 * @param  bg     背景色；SSD1306_COLOR_TRANSPARENT 表示透明
 * @param  scale  放大倍数 1~8
 */
void ssd1306_draw_string(ssd1306_t *dev, int16_t x, int16_t y, const char *s,
                         uint8_t fg, int32_t bg, uint8_t scale);

/** @brief 计算字符串像素宽度（不处理换行） */
uint16_t ssd1306_string_width(const char *s, uint8_t scale);

/* ---------------- 点阵图 ---------------- */

/**
 * @brief  显示 1bpp 单色点阵图
 * @param  x,y    左上角坐标
 * @param  w,h    点阵宽高（像素）
 * @param  data   点阵数据，每行 (w+7)/8 字节，高位在前，1 表示亮点
 * @param  fg     亮点颜色
 * @param  bg     背景颜色
 * @param  scale  放大倍数 1~4
 */
void ssd1306_draw_bitmap(ssd1306_t *dev, int16_t x, int16_t y,
                         uint16_t w, uint16_t h, const uint8_t *data,
                         uint8_t fg, uint8_t bg, uint8_t scale);

/* ---------------- 波形显示 ---------------- */

/** @brief 波形控件配置 */
typedef struct {
    int16_t  x, y;               /**< 控件左上角 */
    uint16_t w, h;               /**< 控件宽高 */
    int16_t  y_min, y_max;       /**< 数据纵轴范围（自动缩放到控件高度） */

    int16_t *buf;                /**< 采样值缓冲区（用户提供） */
    uint16_t cap;                /**< 缓冲区容量（一般等于控件宽度 w） */

    uint8_t  fg_color;           /**< 波形颜色 */
    uint8_t  bg_color;           /**< 背景色 */
    uint8_t  grid_color;         /**< 网格颜色 */
    uint8_t  axis_color;         /**< 坐标轴（y=0）颜色 */
    uint8_t  show_grid;          /**< 1: 显示网格 */
} ssd1306_wave_cfg_t;

/** @brief 波形控件句柄 */
typedef struct {
    ssd1306_wave_cfg_t cfg;
    uint16_t count;              /**< 当前有效采样点数 */
} ssd1306_wave_t;

/** @brief 初始化波形控件（仅拷贝配置，不绘制） */
void ssd1306_wave_init(ssd1306_wave_t *wave, const ssd1306_wave_cfg_t *cfg);

/** @brief 清空波形并重绘背景、网格、坐标轴 */
void ssd1306_wave_clear(ssd1306_t *dev, ssd1306_wave_t *wave);

/**
 * @brief  追加一个采样值并刷新波形（满后左移滚动）
 * @param  value 采样值，超出 y_min~y_max 会自动限幅
 * @note   本函数只更新帧缓冲，需调用 ssd1306_refresh() 才显示。
 */
void ssd1306_wave_push(ssd1306_t *dev, ssd1306_wave_t *wave, int16_t value);

/** @brief 用一组数据替换当前波形并重绘 */
void ssd1306_wave_set_data(ssd1306_t *dev, ssd1306_wave_t *wave,
                           const int16_t *values, uint16_t count);

/**
 * @brief  简单波形显示（无需控件句柄，直接画一组数据）
 * @param  values 采样值数组
 * @param  count  采样点数
 * @param  y_min,y_max 数据范围
 */
void ssd1306_draw_waveform(ssd1306_t *dev, int16_t x, int16_t y,
                           uint16_t w, uint16_t h,
                           const int16_t *values, uint16_t count,
                           int16_t y_min, int16_t y_max,
                           uint8_t fg, uint8_t bg);

#ifdef __cplusplus
}
#endif

#endif /* __SSD1306_H__ */
