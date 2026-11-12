/**
 * @file    ssd1306.c
 * @brief   SSD1306 I2C OLED 驱动实现（STM32F407 + HAL 库）
 */

#include "ssd1306.h"
#include <string.h>

/* ======================= 内部参数 ======================= */

#define SSD1306_FONT_W         5U
#define SSD1306_FONT_H         7U
#define SSD1306_FONT_SCALE_MAX 8U
#define SSD1306_BMP_SCALE_MAX  4U

/* SSD1306 指令 */
#define SSD1306_CMD_DISPLAY_OFF        0xAEU
#define SSD1306_CMD_DISPLAY_ON         0xAFU
#define SSD1306_CMD_SET_MEMORY_MODE    0x20U
#define SSD1306_CMD_SET_COLUMN_ADDR    0x21U
#define SSD1306_CMD_SET_PAGE_ADDR      0x22U
#define SSD1306_CMD_SET_START_LINE     0x40U
#define SSD1306_CMD_SET_CONTRAST       0x81U
#define SSD1306_CMD_SEGMENT_REMAP      0xA0U
#define SSD1306_CMD_ENTIRE_DISPLAY_RESUME 0xA4U
#define SSD1306_CMD_SET_NORMAL         0xA6U
#define SSD1306_CMD_SET_INVERSE        0xA7U
#define SSD1306_CMD_SET_MULTIPLEX      0xA8U
#define SSD1306_CMD_SET_DISPLAY_OFFSET 0xD3U
#define SSD1306_CMD_SET_COM_SCAN_DIR   0xC0U
#define SSD1306_CMD_SET_DISPLAY_CLK    0xD5U
#define SSD1306_CMD_SET_PRECHARGE      0xD9U
#define SSD1306_CMD_SET_COM_PINS       0xDAU
#define SSD1306_CMD_SET_VCOMH          0xDBU
#define SSD1306_CMD_CHARGE_PUMP        0x8DU

/* I2C 控制字节 */
#define SSD1306_CTRL_CMD               0x00U   /* 后续字节为命令 */
#define SSD1306_CTRL_DATA              0x40U   /* 后续字节为数据 */

/* ======================= 5x7 ASCII 字库 ======================= */
/* 每个字符 5 列，每列 1 字节，bit0 为最上面一行 */
static const uint8_t s_font5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* 0x20 ' '  */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* 0x21 '!'  */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* 0x22 '"'  */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* 0x23 '#'  */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* 0x24 '$'  */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* 0x25 '%'  */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* 0x26 '&'  */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* 0x27 '\'' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* 0x28 '('  */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* 0x29 ')'  */
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, /* 0x2A '*'  */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* 0x2B '+'  */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* 0x2C ','  */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* 0x2D '-'  */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* 0x2E '.'  */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* 0x2F '/'  */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0x30 '0'  */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 0x31 '1'  */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 0x32 '2'  */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 0x33 '3'  */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 0x34 '4'  */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 0x35 '5'  */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 0x36 '6'  */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 0x37 '7'  */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 0x38 '8'  */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 0x39 '9'  */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* 0x3A ':'  */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* 0x3B ';'  */
    {0x00, 0x08, 0x14, 0x22, 0x41}, /* 0x3C '<'  */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* 0x3D '='  */
    {0x41, 0x22, 0x14, 0x08, 0x00}, /* 0x3E '>'  */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* 0x3F '?'  */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* 0x40 '@'  */
    {0x7C, 0x12, 0x11, 0x12, 0x7C}, /* 0x41 'A'  */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* 0x42 'B'  */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* 0x43 'C'  */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 0x44 'D'  */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* 0x45 'E'  */
    {0x7F, 0x09, 0x09, 0x01, 0x01}, /* 0x46 'F'  */
    {0x3E, 0x41, 0x41, 0x51, 0x32}, /* 0x47 'G'  */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 0x48 'H'  */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* 0x49 'I'  */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* 0x4A 'J'  */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* 0x4B 'K'  */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* 0x4C 'L'  */
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, /* 0x4D 'M'  */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 0x4E 'N'  */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 0x4F 'O'  */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* 0x50 'P'  */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 0x51 'Q'  */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* 0x52 'R'  */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* 0x53 'S'  */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* 0x54 'T'  */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 0x55 'U'  */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 0x56 'V'  */
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, /* 0x57 'W'  */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* 0x58 'X'  */
    {0x03, 0x04, 0x78, 0x04, 0x03}, /* 0x59 'Y'  */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* 0x5A 'Z'  */
    {0x00, 0x00, 0x7F, 0x41, 0x41}, /* 0x5B '['  */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* 0x5C '\'  */
    {0x41, 0x41, 0x7F, 0x00, 0x00}, /* 0x5D ']'  */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* 0x5E '^'  */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* 0x5F '_'  */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* 0x60 '`'  */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* 0x61 'a'  */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* 0x62 'b'  */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* 0x63 'c'  */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* 0x64 'd'  */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* 0x65 'e'  */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* 0x66 'f'  */
    {0x08, 0x14, 0x54, 0x54, 0x3C}, /* 0x67 'g'  */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* 0x68 'h'  */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* 0x69 'i'  */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* 0x6A 'j'  */
    {0x00, 0x7F, 0x10, 0x28, 0x44}, /* 0x6B 'k'  */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* 0x6C 'l'  */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* 0x6D 'm'  */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* 0x6E 'n'  */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* 0x6F 'o'  */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* 0x70 'p'  */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* 0x71 'q'  */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* 0x72 'r'  */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* 0x73 's'  */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* 0x74 't'  */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* 0x75 'u'  */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* 0x76 'v'  */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* 0x77 'w'  */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* 0x78 'x'  */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* 0x79 'y'  */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* 0x7A 'z'  */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* 0x7B '{'  */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* 0x7C '|'  */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* 0x7D '}'  */
    {0x08, 0x08, 0x2A, 0x1C, 0x08}, /* 0x7E '~'  */
};

/* ======================= 内部函数 ======================= */

static HAL_StatusTypeDef write_cmd(ssd1306_t *dev, uint8_t cmd)
{
    return HAL_I2C_Mem_Write(dev->cfg.hi2c,
                             (uint16_t)(dev->cfg.i2c_addr << 1),
                             SSD1306_CTRL_CMD, I2C_MEMADD_SIZE_8BIT,
                             &cmd, 1U, dev->cfg.timeout);
}

static HAL_StatusTypeDef write_data(ssd1306_t *dev, const uint8_t *data, uint16_t len)
{
    if (len == 0U) {
        return HAL_OK;
    }
    return HAL_I2C_Mem_Write(dev->cfg.hi2c,
                             (uint16_t)(dev->cfg.i2c_addr << 1),
                             SSD1306_CTRL_DATA, I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)data, len, dev->cfg.timeout);
}

/* 硬件复位 */
static void ssd1306_reset(ssd1306_t *dev)
{
    if (dev->cfg.rst_port) {
        HAL_GPIO_WritePin(dev->cfg.rst_port, dev->cfg.rst_pin, GPIO_PIN_RESET);
        HAL_Delay(10);
        HAL_GPIO_WritePin(dev->cfg.rst_port, dev->cfg.rst_pin, GPIO_PIN_SET);
        HAL_Delay(20);
    }
}

/* 自动选择 COM 引脚配置 */
static uint8_t auto_com_pins(uint16_t height)
{
    if (height <= 32U) {
        return 0x02U;
    }
    return 0x12U;   /* height == 48 / 64 */
}

/* ======================= 初始化 / 控制 ======================= */

HAL_StatusTypeDef ssd1306_init(ssd1306_t *dev, const ssd1306_config_t *cfg,
                               uint8_t *framebuffer)
{
    uint8_t com_pins;
    uint8_t seg_remap;
    uint8_t com_scan;

    if ((dev == NULL) || (cfg == NULL) || (cfg->hi2c == NULL) ||
        (framebuffer == NULL) || (cfg->width == 0U) || (cfg->height == 0U) ||
        ((cfg->height % 8U) != 0U)) {
        return HAL_ERROR;
    }

    dev->cfg = *cfg;
    dev->cfg.i2c_addr &= 0x7FU;
    if (dev->cfg.timeout == 0U) {
        dev->cfg.timeout = 100U;
    }

    dev->fb = framebuffer;
    dev->pages = (uint16_t)(cfg->height / 8U);
    dev->fb_size = (uint16_t)((uint32_t)cfg->width * dev->pages);

    com_pins = (cfg->com_pins != 0U) ? cfg->com_pins : auto_com_pins(cfg->height);
    seg_remap = cfg->flip_h ? 0xA1U : 0xA0U;
    com_scan  = cfg->flip_v ? 0xC8U : 0xC0U;

    ssd1306_reset(dev);

    (void)write_cmd(dev, SSD1306_CMD_DISPLAY_OFF);

    (void)write_cmd(dev, SSD1306_CMD_SET_MULTIPLEX);
    (void)write_cmd(dev, (uint8_t)(cfg->height - 1U));

    (void)write_cmd(dev, SSD1306_CMD_SET_DISPLAY_OFFSET);
    (void)write_cmd(dev, 0x00U);

    (void)write_cmd(dev, SSD1306_CMD_SET_START_LINE | 0x00U);

    (void)write_cmd(dev, seg_remap);                       /* 列重映射 */
    (void)write_cmd(dev, com_scan);                        /* 行扫描方向 */

    (void)write_cmd(dev, SSD1306_CMD_SET_COM_PINS);
    (void)write_cmd(dev, com_pins);

    (void)write_cmd(dev, SSD1306_CMD_SET_CONTRAST);
    (void)write_cmd(dev, cfg->contrast);

    (void)write_cmd(dev, SSD1306_CMD_ENTIRE_DISPLAY_RESUME); /* 恢复 GDDRAM 显示 */
    (void)write_cmd(dev, SSD1306_CMD_SET_NORMAL);

    (void)write_cmd(dev, SSD1306_CMD_SET_DISPLAY_CLK);
    (void)write_cmd(dev, 0x80U);

    (void)write_cmd(dev, SSD1306_CMD_CHARGE_PUMP);
    (void)write_cmd(dev, 0x14U);                            /* 使能电荷泵 */

    (void)write_cmd(dev, SSD1306_CMD_SET_PRECHARGE);
    (void)write_cmd(dev, 0xF1U);

    (void)write_cmd(dev, SSD1306_CMD_SET_VCOMH);
    (void)write_cmd(dev, 0x40U);

    (void)write_cmd(dev, SSD1306_CMD_SET_MEMORY_MODE);
    (void)write_cmd(dev, 0x00U);                            /* 水平寻址模式 */

    ssd1306_clear(dev);
    ssd1306_refresh(dev);

    (void)write_cmd(dev, SSD1306_CMD_DISPLAY_ON);

    return HAL_OK;
}

void ssd1306_set_display_on(ssd1306_t *dev, uint8_t on)
{
    if (dev == NULL) {
        return;
    }
    (void)write_cmd(dev, on ? SSD1306_CMD_DISPLAY_ON : SSD1306_CMD_DISPLAY_OFF);
}

void ssd1306_set_contrast(ssd1306_t *dev, uint8_t contrast)
{
    if (dev == NULL) {
        return;
    }
    dev->cfg.contrast = contrast;
    (void)write_cmd(dev, SSD1306_CMD_SET_CONTRAST);
    (void)write_cmd(dev, contrast);
}

void ssd1306_set_invert(ssd1306_t *dev, uint8_t invert)
{
    if (dev == NULL) {
        return;
    }
    (void)write_cmd(dev, invert ? SSD1306_CMD_SET_INVERSE : SSD1306_CMD_SET_NORMAL);
}

/* ======================= 帧缓冲 / 刷新 ======================= */

void ssd1306_clear(ssd1306_t *dev)
{
    if ((dev == NULL) || (dev->fb == NULL)) {
        return;
    }
    memset(dev->fb, 0x00, dev->fb_size);
}

void ssd1306_fill(ssd1306_t *dev, uint8_t color)
{
    if ((dev == NULL) || (dev->fb == NULL)) {
        return;
    }
    memset(dev->fb, color ? 0xFF : 0x00, dev->fb_size);
}

void ssd1306_refresh(ssd1306_t *dev)
{
    if ((dev == NULL) || (dev->fb == NULL)) {
        return;
    }
    (void)write_cmd(dev, SSD1306_CMD_SET_COLUMN_ADDR);
    (void)write_cmd(dev, 0x00U);
    (void)write_cmd(dev, (uint8_t)(dev->cfg.width - 1U));

    (void)write_cmd(dev, SSD1306_CMD_SET_PAGE_ADDR);
    (void)write_cmd(dev, 0x00U);
    (void)write_cmd(dev, (uint8_t)(dev->pages - 1U));

    (void)write_data(dev, dev->fb, dev->fb_size);
}

void ssd1306_refresh_region(ssd1306_t *dev, uint16_t x, uint16_t y,
                            uint16_t w, uint16_t h)
{
    uint16_t col_start, col_end;
    uint16_t page_start, page_end;
    uint16_t page;

    if ((dev == NULL) || (dev->fb == NULL) || (w == 0U) || (h == 0U)) {
        return;
    }

    /* 裁剪到屏幕内 */
    if (x >= dev->cfg.width) {
        return;
    }
    if (y >= dev->cfg.height) {
        return;
    }
    if ((uint32_t)x + w > dev->cfg.width) {
        w = (uint16_t)(dev->cfg.width - x);
    }
    if ((uint32_t)y + h > dev->cfg.height) {
        h = (uint16_t)(dev->cfg.height - y);
    }

    col_start = x;
    col_end   = (uint16_t)(x + w - 1U);
    page_start = (uint16_t)(y / 8U);
    page_end   = (uint16_t)((y + h - 1U) / 8U);

    for (page = page_start; page <= page_end; page++) {
        const uint8_t *src = dev->fb + (uint32_t)page * dev->cfg.width + col_start;
        uint16_t len = (uint16_t)(col_end - col_start + 1U);

        (void)write_cmd(dev, SSD1306_CMD_SET_COLUMN_ADDR);
        (void)write_cmd(dev, (uint8_t)col_start);
        (void)write_cmd(dev, (uint8_t)col_end);

        (void)write_cmd(dev, SSD1306_CMD_SET_PAGE_ADDR);
        (void)write_cmd(dev, (uint8_t)page);
        (void)write_cmd(dev, (uint8_t)page);

        (void)write_data(dev, src, len);
    }
}

/* ======================= 基本绘图（操作帧缓冲） ======================= */

void ssd1306_draw_pixel(ssd1306_t *dev, int16_t x, int16_t y, uint8_t color)
{
    uint16_t idx;
    uint8_t  bit;

    if ((dev == NULL) || (dev->fb == NULL) ||
        (x < 0) || (y < 0) ||
        (x >= (int16_t)dev->cfg.width) || (y >= (int16_t)dev->cfg.height)) {
        return;
    }

    idx = (uint16_t)((uint16_t)(y / 8) * dev->cfg.width + (uint16_t)x);
    bit = (uint8_t)(1U << (y & 0x07));

    switch (color) {
    case SSD1306_COLOR_WHITE:
        dev->fb[idx] |= bit;
        break;
    case SSD1306_COLOR_BLACK:
        dev->fb[idx] &= (uint8_t)~bit;
        break;
    case SSD1306_COLOR_INVERSE:
        dev->fb[idx] ^= bit;
        break;
    default:
        break;
    }
}

void ssd1306_draw_hline(ssd1306_t *dev, int16_t x, int16_t y, int16_t w, uint8_t color)
{
    int16_t i;
    int16_t x0, x1;

    if ((dev == NULL) || (w <= 0) || (y < 0) || (y >= (int16_t)dev->cfg.height)) {
        return;
    }
    x0 = x;
    x1 = (int16_t)(x + w - 1);
    if (x0 < 0) {
        x0 = 0;
    }
    if (x1 >= (int16_t)dev->cfg.width) {
        x1 = (int16_t)(dev->cfg.width - 1);
    }
    if (x0 > x1) {
        return;
    }
    for (i = x0; i <= x1; i++) {
        ssd1306_draw_pixel(dev, i, y, color);
    }
}

void ssd1306_draw_vline(ssd1306_t *dev, int16_t x, int16_t y, int16_t h, uint8_t color)
{
    int16_t i;
    int16_t y0, y1;

    if ((dev == NULL) || (h <= 0) || (x < 0) || (x >= (int16_t)dev->cfg.width)) {
        return;
    }
    y0 = y;
    y1 = (int16_t)(y + h - 1);
    if (y0 < 0) {
        y0 = 0;
    }
    if (y1 >= (int16_t)dev->cfg.height) {
        y1 = (int16_t)(dev->cfg.height - 1);
    }
    if (y0 > y1) {
        return;
    }
    for (i = y0; i <= y1; i++) {
        ssd1306_draw_pixel(dev, x, i, color);
    }
}

void ssd1306_draw_rect(ssd1306_t *dev, int16_t x, int16_t y,
                       int16_t w, int16_t h, uint8_t color)
{
    if ((dev == NULL) || (w <= 0) || (h <= 0)) {
        return;
    }
    ssd1306_draw_hline(dev, x, y, w, color);
    ssd1306_draw_hline(dev, x, (int16_t)(y + h - 1), w, color);
    ssd1306_draw_vline(dev, x, y, h, color);
    ssd1306_draw_vline(dev, (int16_t)(x + w - 1), y, h, color);
}

void ssd1306_draw_line(ssd1306_t *dev, int16_t x0, int16_t y0,
                       int16_t x1, int16_t y1, uint8_t color)
{
    int32_t dx, dy, sx, sy, err;

    if (dev == NULL) {
        return;
    }
    if (y0 == y1) {
        ssd1306_draw_hline(dev, (x0 < x1) ? x0 : x1, y0,
                           (int16_t)((x0 < x1) ? (x1 - x0 + 1) : (x0 - x1 + 1)), color);
        return;
    }
    if (x0 == x1) {
        ssd1306_draw_vline(dev, x0, (y0 < y1) ? y0 : y1,
                           (int16_t)((y0 < y1) ? (y1 - y0 + 1) : (y0 - y1 + 1)), color);
        return;
    }

    dx = (x0 < x1) ? ((int32_t)x1 - x0) : ((int32_t)x0 - x1);
    dy = (y0 < y1) ? ((int32_t)y1 - y0) : ((int32_t)y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;) {
        ssd1306_draw_pixel(dev, x0, y0, color);
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        {
            int32_t e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x0 = (int16_t)(x0 + sx);
            }
            if (e2 < dx) {
                err += dx;
                y0 = (int16_t)(y0 + sy);
            }
        }
    }
}

/* ======================= 文字 ======================= */

uint16_t ssd1306_draw_char(ssd1306_t *dev, int16_t x, int16_t y, char c,
                           uint8_t fg, int32_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t line, col;

    if (dev == NULL) {
        return 0U;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    if (scale > SSD1306_FONT_SCALE_MAX) {
        scale = SSD1306_FONT_SCALE_MAX;
    }
    if (((uint8_t)c < 0x20U) || ((uint8_t)c > 0x7EU)) {
        c = '?';
    }
    glyph = s_font5x7[(uint8_t)c - 0x20U];

    for (line = 0; line < SSD1306_FONT_H; line++) {
        for (col = 0; col < SSD1306_FONT_W; col++) {
            uint8_t on = (uint8_t)((glyph[col] >> line) & 0x01U);
            uint8_t color;

            if (on) {
                color = fg;
            } else if (bg < 0) {
                continue;   /* 透明背景，跳过 */
            } else {
                color = (uint8_t)bg;
            }

            if (scale == 1U) {
                ssd1306_draw_pixel(dev,
                                   (int16_t)(x + col),
                                   (int16_t)(y + line),
                                   color);
            } else {
                int16_t px = (int16_t)(x + col * scale);
                int16_t py = (int16_t)(y + line * scale);
                uint8_t sy, sx;
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        ssd1306_draw_pixel(dev,
                                           (int16_t)(px + sx),
                                           (int16_t)(py + sy),
                                           color);
                    }
                }
            }
        }
    }
    return (uint16_t)(SSD1306_FONT_W * scale);
}

void ssd1306_draw_string(ssd1306_t *dev, int16_t x, int16_t y, const char *s,
                         uint8_t fg, int32_t bg, uint8_t scale)
{
    int16_t cx = x;
    int16_t cy = y;

    if ((dev == NULL) || (s == NULL)) {
        return;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    while (*s != '\0') {
        if (*s == '\n') {
            cx = x;
            cy = (int16_t)(cy + (SSD1306_FONT_H + 1U) * scale);
            s++;
            continue;
        }
        if (*s == '\r') {
            s++;
            continue;
        }
        (void)ssd1306_draw_char(dev, cx, cy, *s, fg, bg, scale);
        cx = (int16_t)(cx + (SSD1306_FONT_W + 1U) * scale);
        s++;
    }
}

uint16_t ssd1306_string_width(const char *s, uint8_t scale)
{
    uint16_t n = 0U;

    if (s == NULL) {
        return 0U;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    while (*s != '\0') {
        if (*s != '\n') {
            n++;
        }
        s++;
    }
    if (n == 0U) {
        return 0U;
    }
    return (uint16_t)((n * (SSD1306_FONT_W + 1U) - 1U) * scale);
}

/* ======================= 点阵图 ======================= */

void ssd1306_draw_bitmap(ssd1306_t *dev, int16_t x, int16_t y,
                         uint16_t w, uint16_t h, const uint8_t *data,
                         uint8_t fg, uint8_t bg, uint8_t scale)
{
    uint16_t stride;
    uint16_t row, col;

    if ((dev == NULL) || (data == NULL) || (w == 0U) || (h == 0U)) {
        return;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    if (scale > SSD1306_BMP_SCALE_MAX) {
        scale = SSD1306_BMP_SCALE_MAX;
    }

    stride = (uint16_t)((w + 7U) / 8U);

    for (row = 0; row < h; row++) {
        const uint8_t *src = data + (uint32_t)row * stride;
        int16_t py = (int16_t)(y + (int32_t)row * scale);

        for (col = 0; col < w; col++) {
            uint8_t on = (uint8_t)((src[col >> 3] >> (7U - (col & 0x07U))) & 0x01U);
            uint8_t color = on ? fg : bg;
            int16_t px = (int16_t)(x + (int32_t)col * scale);

            if (scale == 1U) {
                ssd1306_draw_pixel(dev, px, py, color);
            } else {
                uint8_t sy, sx;
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        ssd1306_draw_pixel(dev,
                                           (int16_t)(px + sx),
                                           (int16_t)(py + sy),
                                           color);
                    }
                }
            }
        }
    }
}

/* ======================= 波形显示 ======================= */

static int16_t wave_map_y(const ssd1306_wave_cfg_t *cfg, int16_t value)
{
    int32_t span = (int32_t)cfg->y_max - (int32_t)cfg->y_min;
    int32_t off;

    if (span <= 0) {
        span = 1;
    }
    if (value < cfg->y_min) {
        value = cfg->y_min;
    }
    if (value > cfg->y_max) {
        value = cfg->y_max;
    }
    off = ((int32_t)value - (int32_t)cfg->y_min) * ((int32_t)cfg->h - 1) / span;
    return (int16_t)((int32_t)cfg->y + (int32_t)cfg->h - 1 - off);
}

static void wave_draw(ssd1306_t *dev, ssd1306_wave_t *wave)
{
    const ssd1306_wave_cfg_t *cfg;
    uint16_t i;
    int16_t prev_x, prev_y;

    if ((dev == NULL) || (wave == NULL)) {
        return;
    }
    cfg = &wave->cfg;
    if ((cfg->w == 0U) || (cfg->h == 0U)) {
        return;
    }

    /* 背景：用矩形逐像素填充实色区域 */
    {
        int16_t bx, by;
        for (by = 0; by < (int16_t)cfg->h; by++) {
            for (bx = 0; bx < (int16_t)cfg->w; bx++) {
                ssd1306_draw_pixel(dev, (int16_t)(cfg->x + bx),
                                   (int16_t)(cfg->y + by), cfg->bg_color);
            }
        }
    }

    /* 网格 */
    if (cfg->show_grid) {
        for (i = 1; i < 4U; i++) {
            int16_t gy = (int16_t)((int32_t)cfg->y + (int32_t)cfg->h * i / 4);
            ssd1306_draw_hline(dev, cfg->x, gy, (int16_t)cfg->w, cfg->grid_color);
        }
    }

    /* 零轴 */
    if ((cfg->y_min < 0) && (cfg->y_max > 0)) {
        ssd1306_draw_hline(dev, cfg->x, wave_map_y(cfg, 0),
                           (int16_t)cfg->w, cfg->axis_color);
    }

    if ((cfg->buf == NULL) || (wave->count < 2U)) {
        return;
    }

    /* 折线：逐列画竖线，把相邻点连起来 */
    prev_x = cfg->x;
    prev_y = wave_map_y(cfg, cfg->buf[0]);
    for (i = 1; i < wave->count; i++) {
        int16_t px;
        int16_t py;
        int16_t top;
        int16_t bot;

        if (wave->count > cfg->w) {
            px = (int16_t)((int32_t)cfg->x +
                           (int32_t)i * ((int32_t)cfg->w - 1) / ((int32_t)wave->count - 1));
        } else {
            px = (int16_t)((int32_t)cfg->x + i);
        }
        py = wave_map_y(cfg, cfg->buf[i]);

        top = (prev_y < py) ? prev_y : py;
        bot = (prev_y > py) ? prev_y : py;
        ssd1306_draw_vline(dev, prev_x, top, (int16_t)(bot - top + 1), cfg->fg_color);

        prev_x = px;
        prev_y = py;
    }
    ssd1306_draw_vline(dev, prev_x, prev_y, 1, cfg->fg_color);
}

void ssd1306_wave_init(ssd1306_wave_t *wave, const ssd1306_wave_cfg_t *cfg)
{
    if ((wave == NULL) || (cfg == NULL)) {
        return;
    }
    wave->cfg = *cfg;
    wave->count = 0U;
}

void ssd1306_wave_clear(ssd1306_t *dev, ssd1306_wave_t *wave)
{
    if (wave == NULL) {
        return;
    }
    wave->count = 0U;
    wave_draw(dev, wave);
}

void ssd1306_wave_push(ssd1306_t *dev, ssd1306_wave_t *wave, int16_t value)
{
    if ((wave == NULL) || (wave->cfg.buf == NULL) || (wave->cfg.cap < 2U)) {
        return;
    }

    if (wave->count < wave->cfg.cap) {
        wave->cfg.buf[wave->count++] = value;
    } else {
        /* 缓冲区已满：整体左移一个点，实现滚动 */
        memmove(wave->cfg.buf, wave->cfg.buf + 1,
                (size_t)(wave->cfg.cap - 1U) * sizeof(int16_t));
        wave->cfg.buf[wave->cfg.cap - 1U] = value;
        wave->count = wave->cfg.cap;
    }
    wave_draw(dev, wave);
}

void ssd1306_wave_set_data(ssd1306_t *dev, ssd1306_wave_t *wave,
                           const int16_t *values, uint16_t count)
{
    uint16_t n;

    if ((wave == NULL) || (values == NULL) || (wave->cfg.buf == NULL)) {
        return;
    }
    n = (count > wave->cfg.cap) ? wave->cfg.cap : count;
    if (n > 0U) {
        memcpy(wave->cfg.buf, values, (size_t)n * sizeof(int16_t));
    }
    wave->count = n;
    wave_draw(dev, wave);
}

void ssd1306_draw_waveform(ssd1306_t *dev, int16_t x, int16_t y,
                           uint16_t w, uint16_t h,
                           const int16_t *values, uint16_t count,
                           int16_t y_min, int16_t y_max,
                           uint8_t fg, uint8_t bg)
{
    ssd1306_wave_t wave;

    if ((values == NULL) || (count == 0U)) {
        return;
    }
    memset(&wave, 0, sizeof(wave));
    wave.cfg.x = x;
    wave.cfg.y = y;
    wave.cfg.w = w;
    wave.cfg.h = h;
    wave.cfg.y_min = y_min;
    wave.cfg.y_max = y_max;
    wave.cfg.buf = (int16_t *)values;
    wave.cfg.cap = count;
    wave.cfg.bg_color = bg;
    wave.cfg.fg_color = fg;
    wave.cfg.grid_color = bg;
    wave.cfg.axis_color = bg;
    wave.cfg.show_grid = 0U;
    wave.count = count;

    wave_draw(dev, &wave);
}
