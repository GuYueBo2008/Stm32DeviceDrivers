/**
 * @file    st7789.h
 * @brief   ST7789 SPI TFT 液晶驱动（STM32F407 + HAL 库）
 *
 * 支持 240x320、240x240、135x240 等常见 ST7789 面板，SPI 接口、引脚、
 * 分辨率、显示偏移、旋转方向、RGB/BGR 顺序均可配置。
 *
 * 功能：
 *   - 初始化 / 旋转 / 反显 / 休眠 / 背光控制
 *   - 全屏或任意区域刷新（RGB565）
 *   - 画点、线、矩形
 *   - ASCII 5x7 字库写字（可缩放、可透明背景）
 *   - 波形显示（网格、坐标轴、自动缩放、滚动刷新）
 *   - 点阵图显示（RGB565 位图 / 1bpp 单色点阵，可放大）
 *
 * 使用示例：
 *   static st7789_t lcd;
 *   st7789_config_t cfg = {
 *       .hspi     = &hspi1,
 *       .cs_port  = GPIOA, .cs_pin  = GPIO_PIN_4,
 *       .dc_port  = GPIOA, .dc_pin  = GPIO_PIN_3,
 *       .rst_port = GPIOA, .rst_pin = GPIO_PIN_2,
 *       .bl_port  = GPIOA, .bl_pin  = GPIO_PIN_1,
 *       .width    = 240, .height = 320,
 *       .rotation = 1, .bgr = 1, .invert = 1, .timeout = 100,
 *   };
 *   st7789_init(&lcd, &cfg);
 *   st7789_fill(&lcd, ST7789_BLACK);
 *   st7789_draw_string(&lcd, 10, 10, "Hello ST7789", ST7789_WHITE, ST7789_BLACK, 2);
 *
 * 说明：
 *   - 仅支持 ASCII 可见字符（0x20~0x7E）的 5x7 字库；如需中文请自行替换字库。
 *   - 所有颜色均为 RGB565 格式，驱动程序内部已处理高低字节顺序。
 *   - 若 CS 直接接地（硬件片选常有效），把 cs_port 设为 NULL 即可。
 */

#ifndef __ST7789_H__
#define __ST7789_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ---------------- RGB565 常用颜色 ---------------- */
#define ST7789_BLACK       0x0000U
#define ST7789_WHITE       0xFFFFU
#define ST7789_RED         0xF800U
#define ST7789_GREEN       0x07E0U
#define ST7789_BLUE        0x001FU
#define ST7789_YELLOW      0xFFE0U
#define ST7789_CYAN        0x07FFU
#define ST7789_MAGENTA     0xF81FU
#define ST7789_GRAY        0x8410U
#define ST7789_ORANGE      0xFD20U
#define ST7789_DARK_GRAY   0x4208U
#define ST7789_NAVY        0x0010U

/** RGB888 -> RGB565 */
#define ST7789_RGB565(r, g, b) \
    ((uint16_t)((((uint16_t)(r) & 0xF8U) << 8) | \
                (((uint16_t)(g) & 0xFCU) << 3) | \
                (((uint16_t)(b) & 0xF8U) >> 3)))

/** 文字背景使用该值时表示“透明背景” */
#define ST7789_COLOR_TRANSPARENT (-1)

/* ---------------- 配置 / 句柄 ---------------- */

/**
 * @brief ST7789 配置参数
 *
 * 坐标说明（以 240x320 面板为例）：
 *   rotation = 0 -> 240x320（竖屏）
 *   rotation = 1 -> 320x240（横屏）
 *   rotation = 2 -> 240x320（竖屏，倒置）
 *   rotation = 3 -> 320x240（横屏，倒置）
 */
typedef struct {
    SPI_HandleTypeDef *hspi;     /**< SPI 句柄，如 &hspi1（必填） */

    GPIO_TypeDef *cs_port;       /**< 片选端口，NULL 表示不控制 CS（CS 接地） */
    uint16_t      cs_pin;        /**< 片选引脚 */
    GPIO_TypeDef *dc_port;       /**< 数据/命令选择端口（必填） */
    uint16_t      dc_pin;        /**< 数据/命令选择引脚 */
    GPIO_TypeDef *rst_port;      /**< 复位端口，NULL 表示无复位脚（用软件复位） */
    uint16_t      rst_pin;       /**< 复位引脚 */
    GPIO_TypeDef *bl_port;       /**< 背光端口，NULL 表示无背光控制脚 */
    uint16_t      bl_pin;        /**< 背光引脚 */

    uint16_t width;              /**< 面板物理宽度（旋转 0 时），如 240 */
    uint16_t height;             /**< 面板物理高度（旋转 0 时），如 320 */
    uint16_t x_offset;           /**< 显存窗口 X 偏移（135x240 一般为 52，其他为 0） */
    uint16_t y_offset;           /**< 显存窗口 Y 偏移（135x240 一般为 40，其他为 0） */

    uint8_t  rotation;           /**< 旋转方向 0~3 */
    uint8_t  bgr;                /**< 1: BGR 顺序（多数模块）；0: RGB 顺序 */
    uint8_t  invert;             /**< 1: 打开颜色反转（多数 IPS 屏需要） */
    uint8_t  bl_active_low;      /**< 1: 背光低电平点亮；0: 高电平点亮 */

    uint32_t timeout;            /**< SPI 传输超时时间（ms），0 则默认 100 */
} st7789_config_t;

/** @brief ST7789 驱动句柄 */
typedef struct {
    st7789_config_t cfg;         /**< 配置副本 */
    uint16_t w;                  /**< 当前旋转后的逻辑宽度 */
    uint16_t h;                  /**< 当前旋转后的逻辑高度 */
    uint16_t x_off;              /**< 当前旋转下的 X 偏移 */
    uint16_t y_off;              /**< 当前旋转下的 Y 偏移 */
} st7789_t;

/* ---------------- 初始化 / 控制 ---------------- */

/**
 * @brief  初始化 ST7789
 * @param  dev 驱动句柄
 * @param  cfg 配置参数（函数内会拷贝）
 * @retval HAL_OK 成功，HAL_ERROR 参数错误
 */
HAL_StatusTypeDef st7789_init(st7789_t *dev, const st7789_config_t *cfg);

/** @brief 设置旋转方向（0~3），会重新计算逻辑宽高与显示偏移 */
void st7789_set_rotation(st7789_t *dev, uint8_t rotation);

/** @brief 背光开关（on 非 0 为开） */
void st7789_set_backlight(st7789_t *dev, uint8_t on);

/** @brief 颜色反转开关（en 非 0 为反转） */
void st7789_set_invert(st7789_t *dev, uint8_t en);

/** @brief 休眠开关（sleep 非 0 进入休眠） */
void st7789_set_sleep(st7789_t *dev, uint8_t sleep);

/* ---------------- 显存窗口 / 刷新 ---------------- */

/**
 * @brief  设置显示窗口并进入写显存状态（CASET + RASET + RAMWR）
 * @note   调用后连续调用 st7789_write_pixels() 即可填充该窗口。
 */
void st7789_set_window(st7789_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

/**
 * @brief  向当前窗口连续写入 RGB565 像素
 * @param  count 像素个数
 */
void st7789_write_pixels(st7789_t *dev, const uint16_t *colors, uint32_t count);

/**
 * @brief  区域刷新：把一块 RGB565 图像数据送到指定区域
 * @param  x,y,w,h 目标区域
 * @param  pixels  图像数据，按行存放，长度为 w*h
 */
void st7789_flush(st7789_t *dev, uint16_t x, uint16_t y,
                  uint16_t w, uint16_t h, const uint16_t *pixels);

/** @brief 填充矩形（自动裁剪到屏幕内） */
void st7789_fill_rect(st7789_t *dev, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint16_t color);

/** @brief 全屏填充 */
void st7789_fill(st7789_t *dev, uint16_t color);

/** @brief 清屏（黑色） */
void st7789_clear(st7789_t *dev);

/* ---------------- 基本绘图 ---------------- */

void st7789_draw_pixel(st7789_t *dev, int16_t x, int16_t y, uint16_t color);
void st7789_draw_hline(st7789_t *dev, int16_t x, int16_t y, int16_t w, uint16_t color);
void st7789_draw_vline(st7789_t *dev, int16_t x, int16_t y, int16_t h, uint16_t color);
void st7789_draw_rect(st7789_t *dev, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint16_t color);
void st7789_draw_line(st7789_t *dev, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, uint16_t color);

/* ---------------- 文字 ---------------- */

/**
 * @brief  绘制一个 ASCII 字符（5x7 字库）
 * @param  x,y    左上角坐标
 * @param  c      字符（0x20~0x7E，其他显示为 '?'）
 * @param  fg     前景色
 * @param  bg     背景色；传 ST7789_COLOR_TRANSPARENT(-1) 表示透明
 * @param  scale  放大倍数 1~8
 * @retval 字符宽度（像素）
 */
uint16_t st7789_draw_char(st7789_t *dev, int16_t x, int16_t y, char c,
                          uint16_t fg, int32_t bg, uint8_t scale);

/**
 * @brief  绘制字符串（支持 '\n' 换行，仅 ASCII）
 * @param  x,y    左上角坐标
 * @param  bg     背景色；ST7789_COLOR_TRANSPARENT 表示透明
 * @param  scale  放大倍数 1~8
 */
void st7789_draw_string(st7789_t *dev, int16_t x, int16_t y, const char *s,
                        uint16_t fg, int32_t bg, uint8_t scale);

/** @brief 计算字符串像素宽度（不处理换行） */
uint16_t st7789_string_width(const char *s, uint8_t scale);

/* ---------------- 点阵图 ---------------- */

/**
 * @brief  显示 RGB565 位图
 * @param  x,y     左上角坐标
 * @param  w,h     图像宽高（像素）
 * @param  pixels  RGB565 数据，按行存放，长度为 w*h
 */
void st7789_draw_bitmap(st7789_t *dev, int16_t x, int16_t y,
                        uint16_t w, uint16_t h, const uint16_t *pixels);

/**
 * @brief  显示 1bpp 单色点阵图（常用于图标、字模）
 * @param  x,y    左上角坐标
 * @param  w,h    点阵宽高（像素）
 * @param  data   点阵数据，每行 (w+7)/8 字节，高位在前，1 表示亮点
 * @param  fg     亮点颜色
 * @param  bg     背景颜色
 * @param  scale  放大倍数 1~4
 * @note   图像必须完整位于屏幕内，否则不绘制。
 */
void st7789_draw_bitmap_1bpp(st7789_t *dev, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, const uint8_t *data,
                             uint16_t fg, uint16_t bg, uint8_t scale);

/* ---------------- 波形显示 ---------------- */

/** @brief 波形控件配置 */
typedef struct {
    int16_t  x, y;               /**< 控件左上角 */
    uint16_t w, h;               /**< 控件宽高 */
    int16_t  y_min, y_max;       /**< 数据纵轴范围（自动缩放到控件高度） */

    int16_t *buf;                /**< 采样值缓冲区（用户提供） */
    uint16_t cap;                /**< 缓冲区容量（一般等于控件宽度 w） */

    uint16_t bg_color;           /**< 背景色 */
    uint16_t fg_color;           /**< 波形颜色 */
    uint16_t grid_color;         /**< 网格颜色 */
    uint16_t axis_color;         /**< 坐标轴（y=0）颜色 */
    uint8_t  show_grid;          /**< 1: 显示网格 */
} st7789_wave_cfg_t;

/** @brief 波形控件句柄 */
typedef struct {
    st7789_wave_cfg_t cfg;
    uint16_t count;              /**< 当前有效采样点数 */
} st7789_wave_t;

/** @brief 初始化波形控件（仅拷贝配置，不绘制） */
void st7789_wave_init(st7789_wave_t *wave, const st7789_wave_cfg_t *cfg);

/** @brief 清空波形并重绘背景、网格、坐标轴 */
void st7789_wave_clear(st7789_t *dev, st7789_wave_t *wave);

/**
 * @brief  追加一个采样值并刷新波形（满后左移滚动）
 * @param  value 采样值，超出 y_min~y_max 会自动限幅
 */
void st7789_wave_push(st7789_t *dev, st7789_wave_t *wave, int16_t value);

/** @brief 用一组数据替换当前波形并重绘 */
void st7789_wave_set_data(st7789_t *dev, st7789_wave_t *wave,
                          const int16_t *values, uint16_t count);

/**
 * @brief  简单波形显示（无需控件句柄，直接画一组数据）
 * @param  values 采样值数组
 * @param  count  采样点数
 * @param  y_min,y_max 数据范围
 */
void st7789_draw_waveform(st7789_t *dev, int16_t x, int16_t y,
                          uint16_t w, uint16_t h,
                          const int16_t *values, uint16_t count,
                          int16_t y_min, int16_t y_max,
                          uint16_t fg, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif /* __ST7789_H__ */
