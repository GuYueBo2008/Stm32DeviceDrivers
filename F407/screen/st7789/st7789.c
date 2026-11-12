/**
 * @file    st7789.c
 * @brief   ST7789 SPI TFT 驱动实现（STM32F407 + HAL 库）
 */

#include "st7789.h"
#include <string.h>

/* ======================= 内部参数 ======================= */

#define ST7789_CHUNK_PIXELS   128U   /* 像素发送缓冲区大小（单位：像素） */
#define ST7789_FONT_W         5U     /* 字库字宽 */
#define ST7789_FONT_H         7U     /* 字库字高 */
#define ST7789_FONT_SCALE_MAX 8U     /* 最大放大倍数 */
#define ST7789_BMP_CHUNK      32U    /* 1bpp 位图每批处理的像素数 */
#define ST7789_BMP_SCALE_MAX  4U

/* ST7789 指令 */
#define ST7789_CMD_SWRESET    0x01U
#define ST7789_CMD_SLPOUT     0x11U
#define ST7789_CMD_SLPIN      0x10U
#define ST7789_CMD_NORON      0x13U
#define ST7789_CMD_INVON      0x21U
#define ST7789_CMD_INVOFF     0x20U
#define ST7789_CMD_DISPON     0x29U
#define ST7789_CMD_CASET      0x2AU
#define ST7789_CMD_RASET      0x2BU
#define ST7789_CMD_RAMWR      0x2CU
#define ST7789_CMD_MADCTL     0x36U
#define ST7789_CMD_COLMOD     0x3AU

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

/* 旋转方向对应的 MADCTL 基础值 */
static const uint8_t s_madctl_rot[4] = {0x00U, 0x60U, 0xC0U, 0xA0U};

/* ======================= 内部函数 ======================= */

static inline void cs_low(st7789_t *dev)
{
    if (dev->cfg.cs_port) {
        HAL_GPIO_WritePin(dev->cfg.cs_port, dev->cfg.cs_pin, GPIO_PIN_RESET);
    }
}

static inline void cs_high(st7789_t *dev)
{
    if (dev->cfg.cs_port) {
        HAL_GPIO_WritePin(dev->cfg.cs_port, dev->cfg.cs_pin, GPIO_PIN_SET);
    }
}

static inline void dc_cmd(st7789_t *dev)
{
    HAL_GPIO_WritePin(dev->cfg.dc_port, dev->cfg.dc_pin, GPIO_PIN_RESET);
}

static inline void dc_data(st7789_t *dev)
{
    HAL_GPIO_WritePin(dev->cfg.dc_port, dev->cfg.dc_pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef write_cmd(st7789_t *dev, uint8_t cmd)
{
    HAL_StatusTypeDef st;
    cs_low(dev);
    dc_cmd(dev);
    st = HAL_SPI_Transmit(dev->cfg.hspi, &cmd, 1U, dev->cfg.timeout);
    cs_high(dev);
    return st;
}

static HAL_StatusTypeDef write_data(st7789_t *dev, const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef st;
    if (len == 0U) {
        return HAL_OK;
    }
    cs_low(dev);
    dc_data(dev);
    st = HAL_SPI_Transmit(dev->cfg.hspi, (uint8_t *)data, len, dev->cfg.timeout);
    cs_high(dev);
    return st;
}

static void cmd_with_data(st7789_t *dev, uint8_t cmd, const uint8_t *data, uint16_t len)
{
    (void)write_cmd(dev, cmd);
    (void)write_data(dev, data, len);
}

/* 更新逻辑宽高与显示偏移 */
static void update_geometry(st7789_t *dev)
{
    uint8_t rot = dev->cfg.rotation & 0x03U;

    if (rot & 0x01U) {
        dev->w = dev->cfg.height;
        dev->h = dev->cfg.width;
        dev->x_off = dev->cfg.y_offset;
        dev->y_off = dev->cfg.x_offset;
    } else {
        dev->w = dev->cfg.width;
        dev->h = dev->cfg.height;
        dev->x_off = dev->cfg.x_offset;
        dev->y_off = dev->cfg.y_offset;
    }
}

/* 按当前旋转发送 MADCTL */
static void write_madctl(st7789_t *dev)
{
    uint8_t mad = s_madctl_rot[dev->cfg.rotation & 0x03U];
    if (dev->cfg.bgr) {
        mad |= 0x08U;
    }
    cmd_with_data(dev, ST7789_CMD_MADCTL, &mad, 1U);
}

/* 向当前窗口填充同一颜色 */
static void send_color(st7789_t *dev, uint16_t color, uint32_t count)
{
    uint8_t buf[ST7789_CHUNK_PIXELS * 2U];
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFU);
    uint32_t i;

    for (i = 0; i < ST7789_CHUNK_PIXELS; i++) {
        buf[2U * i] = hi;
        buf[2U * i + 1U] = lo;
    }

    while (count > 0U) {
        uint32_t n = (count > ST7789_CHUNK_PIXELS) ? ST7789_CHUNK_PIXELS : count;
        (void)write_data(dev, buf, (uint16_t)(n * 2U));
        count -= n;
    }
}

/* 硬件/软件复位 */
static void st7789_reset(st7789_t *dev)
{
    if (dev->cfg.rst_port) {
        HAL_GPIO_WritePin(dev->cfg.rst_port, dev->cfg.rst_pin, GPIO_PIN_RESET);
        HAL_Delay(10);
        HAL_GPIO_WritePin(dev->cfg.rst_port, dev->cfg.rst_pin, GPIO_PIN_SET);
        HAL_Delay(120);
    } else {
        (void)write_cmd(dev, ST7789_CMD_SWRESET);
        HAL_Delay(150);
    }
}

/* ======================= 初始化 / 控制 ======================= */

HAL_StatusTypeDef st7789_init(st7789_t *dev, const st7789_config_t *cfg)
{
    uint8_t param;
    static const uint8_t porctrl[]  = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t gctrl[]    = {0x35};
    static const uint8_t vcoms[]    = {0x19};
    static const uint8_t lcmctrl[]  = {0x2C};
    static const uint8_t vdvrh_en[] = {0x01};
    static const uint8_t vrhs[]     = {0x12};
    static const uint8_t vdvs[]     = {0x20};
    static const uint8_t frctrl2[]  = {0x0F};
    static const uint8_t pwctrl1[]  = {0xA4, 0xA1};
    static const uint8_t pgamma[]   = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
                                       0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    static const uint8_t ngamma[]   = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
                                       0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};

    if ((dev == NULL) || (cfg == NULL) || (cfg->hspi == NULL) ||
        (cfg->dc_port == NULL) || (cfg->width == 0U) || (cfg->height == 0U)) {
        return HAL_ERROR;
    }

    dev->cfg = *cfg;
    dev->cfg.rotation &= 0x03U;
    if (dev->cfg.timeout == 0U) {
        dev->cfg.timeout = 100U;
    }
    update_geometry(dev);

    cs_high(dev);
    if (dev->cfg.bl_port) {
        st7789_set_backlight(dev, 0);
    }

    st7789_reset(dev);

    (void)write_cmd(dev, ST7789_CMD_SWRESET);
    HAL_Delay(150);
    (void)write_cmd(dev, ST7789_CMD_SLPOUT);
    HAL_Delay(120);

    param = 0x55U;                                  /* 16 位/像素 RGB565 */
    cmd_with_data(dev, ST7789_CMD_COLMOD, &param, 1U);
    write_madctl(dev);

    cmd_with_data(dev, 0xB2U, porctrl, (uint16_t)sizeof(porctrl));
    cmd_with_data(dev, 0xB7U, gctrl, (uint16_t)sizeof(gctrl));
    cmd_with_data(dev, 0xBBU, vcoms, (uint16_t)sizeof(vcoms));
    cmd_with_data(dev, 0xC0U, lcmctrl, (uint16_t)sizeof(lcmctrl));
    cmd_with_data(dev, 0xC2U, vdvrh_en, (uint16_t)sizeof(vdvrh_en));
    cmd_with_data(dev, 0xC3U, vrhs, (uint16_t)sizeof(vrhs));
    cmd_with_data(dev, 0xC4U, vdvs, (uint16_t)sizeof(vdvs));
    cmd_with_data(dev, 0xC6U, frctrl2, (uint16_t)sizeof(frctrl2));
    cmd_with_data(dev, 0xD0U, pwctrl1, (uint16_t)sizeof(pwctrl1));
    cmd_with_data(dev, 0xE0U, pgamma, (uint16_t)sizeof(pgamma));
    cmd_with_data(dev, 0xE1U, ngamma, (uint16_t)sizeof(ngamma));

    (void)write_cmd(dev, dev->cfg.invert ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
    (void)write_cmd(dev, ST7789_CMD_NORON);
    (void)write_cmd(dev, ST7789_CMD_DISPON);
    HAL_Delay(20);

    st7789_fill(dev, ST7789_BLACK);

    if (dev->cfg.bl_port) {
        st7789_set_backlight(dev, 1);
    }
    return HAL_OK;
}

void st7789_set_rotation(st7789_t *dev, uint8_t rotation)
{
    if (dev == NULL) {
        return;
    }
    dev->cfg.rotation = rotation & 0x03U;
    update_geometry(dev);
    write_madctl(dev);
}

void st7789_set_backlight(st7789_t *dev, uint8_t on)
{
    GPIO_PinState state;

    if ((dev == NULL) || (dev->cfg.bl_port == NULL)) {
        return;
    }
    if (on) {
        state = dev->cfg.bl_active_low ? GPIO_PIN_RESET : GPIO_PIN_SET;
    } else {
        state = dev->cfg.bl_active_low ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    HAL_GPIO_WritePin(dev->cfg.bl_port, dev->cfg.bl_pin, state);
}

void st7789_set_invert(st7789_t *dev, uint8_t en)
{
    if (dev == NULL) {
        return;
    }
    dev->cfg.invert = en ? 1U : 0U;
    (void)write_cmd(dev, en ? ST7789_CMD_INVON : ST7789_CMD_INVOFF);
}

void st7789_set_sleep(st7789_t *dev, uint8_t sleep)
{
    if (dev == NULL) {
        return;
    }
    if (sleep) {
        (void)write_cmd(dev, ST7789_CMD_SLPIN);
        HAL_Delay(120);
    } else {
        (void)write_cmd(dev, ST7789_CMD_SLPOUT);
        HAL_Delay(120);
    }
}

/* ======================= 显存窗口 / 刷新 ======================= */

void st7789_set_window(st7789_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t buf[4];
    uint16_t xs = (uint16_t)(x0 + dev->x_off);
    uint16_t xe = (uint16_t)(x1 + dev->x_off);
    uint16_t ys = (uint16_t)(y0 + dev->y_off);
    uint16_t ye = (uint16_t)(y1 + dev->y_off);

    (void)write_cmd(dev, ST7789_CMD_CASET);
    buf[0] = (uint8_t)(xs >> 8); buf[1] = (uint8_t)(xs & 0xFFU);
    buf[2] = (uint8_t)(xe >> 8); buf[3] = (uint8_t)(xe & 0xFFU);
    (void)write_data(dev, buf, 4U);

    (void)write_cmd(dev, ST7789_CMD_RASET);
    buf[0] = (uint8_t)(ys >> 8); buf[1] = (uint8_t)(ys & 0xFFU);
    buf[2] = (uint8_t)(ye >> 8); buf[3] = (uint8_t)(ye & 0xFFU);
    (void)write_data(dev, buf, 4U);

    (void)write_cmd(dev, ST7789_CMD_RAMWR);
}

void st7789_write_pixels(st7789_t *dev, const uint16_t *colors, uint32_t count)
{
    uint8_t buf[ST7789_CHUNK_PIXELS * 2U];

    if ((dev == NULL) || (colors == NULL)) {
        return;
    }
    while (count > 0U) {
        uint32_t n = (count > ST7789_CHUNK_PIXELS) ? ST7789_CHUNK_PIXELS : count;
        uint32_t i;

        for (i = 0; i < n; i++) {
            buf[2U * i]      = (uint8_t)(colors[i] >> 8);
            buf[2U * i + 1U] = (uint8_t)(colors[i] & 0xFFU);
        }
        (void)write_data(dev, buf, (uint16_t)(n * 2U));
        colors += n;
        count -= n;
    }
}

void st7789_flush(st7789_t *dev, uint16_t x, uint16_t y,
                  uint16_t w, uint16_t h, const uint16_t *pixels)
{
    uint16_t x1, y1;

    if ((dev == NULL) || (pixels == NULL) || (w == 0U) || (h == 0U)) {
        return;
    }
    if ((x >= dev->w) || (y >= dev->h)) {
        return;
    }
    if ((uint32_t)x + w > dev->w) {
        w = (uint16_t)(dev->w - x);
    }
    if ((uint32_t)y + h > dev->h) {
        h = (uint16_t)(dev->h - y);
    }
    x1 = (uint16_t)(x + w - 1U);
    y1 = (uint16_t)(y + h - 1U);

    st7789_set_window(dev, x, y, x1, y1);
    st7789_write_pixels(dev, pixels, (uint32_t)w * h);
}

void st7789_fill_rect(st7789_t *dev, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint16_t color)
{
    int32_t x0, y0, x1, y1, area;

    if ((dev == NULL) || (w <= 0) || (h <= 0)) {
        return;
    }
    x0 = x;
    y0 = y;
    x1 = (int32_t)x + w - 1;
    y1 = (int32_t)y + h - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)dev->w) x1 = (int32_t)dev->w - 1;
    if (y1 >= (int32_t)dev->h) y1 = (int32_t)dev->h - 1;
    if ((x0 > x1) || (y0 > y1)) {
        return;
    }

    st7789_set_window(dev, (uint16_t)x0, (uint16_t)y0, (uint16_t)x1, (uint16_t)y1);
    area = (x1 - x0 + 1) * (y1 - y0 + 1);
    send_color(dev, color, (uint32_t)area);
}

void st7789_fill(st7789_t *dev, uint16_t color)
{
    if (dev == NULL) {
        return;
    }
    st7789_fill_rect(dev, 0, 0, (int16_t)dev->w, (int16_t)dev->h, color);
}

void st7789_clear(st7789_t *dev)
{
    st7789_fill(dev, ST7789_BLACK);
}

/* ======================= 基本绘图 ======================= */

void st7789_draw_pixel(st7789_t *dev, int16_t x, int16_t y, uint16_t color)
{
    uint8_t buf[2];

    if ((dev == NULL) || (x < 0) || (y < 0) || (x >= (int16_t)dev->w) || (y >= (int16_t)dev->h)) {
        return;
    }
    st7789_set_window(dev, (uint16_t)x, (uint16_t)y, (uint16_t)x, (uint16_t)y);
    buf[0] = (uint8_t)(color >> 8);
    buf[1] = (uint8_t)(color & 0xFFU);
    (void)write_data(dev, buf, 2U);
}

void st7789_draw_hline(st7789_t *dev, int16_t x, int16_t y, int16_t w, uint16_t color)
{
    int32_t x0, x1;

    if ((dev == NULL) || (w <= 0) || (y < 0) || (y >= (int16_t)dev->h)) {
        return;
    }
    x0 = x;
    x1 = (int32_t)x + w - 1;
    if (x0 < 0) x0 = 0;
    if (x1 >= (int32_t)dev->w) x1 = (int32_t)dev->w - 1;
    if (x0 > x1) {
        return;
    }
    st7789_set_window(dev, (uint16_t)x0, (uint16_t)y, (uint16_t)x1, (uint16_t)y);
    send_color(dev, color, (uint32_t)(x1 - x0 + 1));
}

void st7789_draw_vline(st7789_t *dev, int16_t x, int16_t y, int16_t h, uint16_t color)
{
    int32_t y0, y1;

    if ((dev == NULL) || (h <= 0) || (x < 0) || (x >= (int16_t)dev->w)) {
        return;
    }
    y0 = y;
    y1 = (int32_t)y + h - 1;
    if (y0 < 0) y0 = 0;
    if (y1 >= (int32_t)dev->h) y1 = (int32_t)dev->h - 1;
    if (y0 > y1) {
        return;
    }
    st7789_set_window(dev, (uint16_t)x, (uint16_t)y0, (uint16_t)x, (uint16_t)y1);
    send_color(dev, color, (uint32_t)(y1 - y0 + 1));
}

void st7789_draw_rect(st7789_t *dev, int16_t x, int16_t y,
                      int16_t w, int16_t h, uint16_t color)
{
    if ((dev == NULL) || (w <= 0) || (h <= 0)) {
        return;
    }
    st7789_draw_hline(dev, x, y, w, color);
    st7789_draw_hline(dev, x, (int16_t)(y + h - 1), w, color);
    st7789_draw_vline(dev, x, y, h, color);
    st7789_draw_vline(dev, (int16_t)(x + w - 1), y, h, color);
}

void st7789_draw_line(st7789_t *dev, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, uint16_t color)
{
    int32_t dx, dy, sx, sy, err;

    if (dev == NULL) {
        return;
    }
    if (y0 == y1) {
        st7789_draw_hline(dev, (x0 < x1) ? x0 : x1, y0,
                          (int16_t)((x0 < x1) ? (x1 - x0 + 1) : (x0 - x1 + 1)), color);
        return;
    }
    if (x0 == x1) {
        st7789_draw_vline(dev, x0, (y0 < y1) ? y0 : y1,
                          (int16_t)((y0 < y1) ? (y1 - y0 + 1) : (y0 - y1 + 1)), color);
        return;
    }

    dx = (x0 < x1) ? ((int32_t)x1 - x0) : ((int32_t)x0 - x1);
    dy = (y0 < y1) ? ((int32_t)y1 - y0) : ((int32_t)y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;) {
        st7789_draw_pixel(dev, x0, y0, color);
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

uint16_t st7789_draw_char(st7789_t *dev, int16_t x, int16_t y, char c,
                          uint16_t fg, int32_t bg, uint8_t scale)
{
    const uint8_t *glyph;
    uint8_t line, col, sy;

    if (dev == NULL) {
        return 0;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    if (scale > ST7789_FONT_SCALE_MAX) {
        scale = ST7789_FONT_SCALE_MAX;
    }
    if (((uint8_t)c < 0x20U) || ((uint8_t)c > 0x7EU)) {
        c = '?';
    }
    glyph = s_font5x7[(uint8_t)c - 0x20U];

    if ((bg < 0) || (x < 0) || (y < 0) ||
        ((int32_t)x + (int32_t)ST7789_FONT_W * scale > (int32_t)dev->w) ||
        ((int32_t)y + (int32_t)ST7789_FONT_H * scale > (int32_t)dev->h)) {
        /* 透明背景或字符超出屏幕：逐个像素绘制（fill_rect 内部会裁剪） */
        for (line = 0; line < ST7789_FONT_H; line++) {
            for (col = 0; col < ST7789_FONT_W; col++) {
                uint8_t on = (uint8_t)((glyph[col] >> line) & 0x01U);
                if (!on && (bg < 0)) {
                    continue;
                }
                st7789_fill_rect(dev,
                                 (int16_t)(x + col * scale),
                                 (int16_t)(y + line * scale),
                                 (int16_t)scale, (int16_t)scale,
                                 on ? fg : (uint16_t)bg);
            }
        }
    } else {
        uint16_t rowbuf[ST7789_FONT_W * ST7789_FONT_SCALE_MAX];
        uint16_t bgc = (uint16_t)bg;

        for (line = 0; line < ST7789_FONT_H; line++) {
            for (col = 0; col < ST7789_FONT_W; col++) {
                uint16_t color = (glyph[col] & (1U << line)) ? fg : bgc;
                uint8_t s;
                for (s = 0; s < scale; s++) {
                    rowbuf[col * scale + s] = color;
                }
            }
            for (sy = 0; sy < scale; sy++) {
                st7789_set_window(dev, (uint16_t)x,
                                  (uint16_t)(y + line * scale + sy),
                                  (uint16_t)(x + ST7789_FONT_W * scale - 1U),
                                  (uint16_t)(y + line * scale + sy));
                st7789_write_pixels(dev, rowbuf, ST7789_FONT_W * scale);
            }
        }
    }
    return (uint16_t)(ST7789_FONT_W * scale);
}

void st7789_draw_string(st7789_t *dev, int16_t x, int16_t y, const char *s,
                        uint16_t fg, int32_t bg, uint8_t scale)
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
            cy = (int16_t)(cy + (ST7789_FONT_H + 1U) * scale);
            s++;
            continue;
        }
        if (*s == '\r') {
            s++;
            continue;
        }
        (void)st7789_draw_char(dev, cx, cy, *s, fg, bg, scale);
        cx = (int16_t)(cx + (ST7789_FONT_W + 1U) * scale);
        s++;
    }
}

uint16_t st7789_string_width(const char *s, uint8_t scale)
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
    return (uint16_t)((n * (ST7789_FONT_W + 1U) - 1U) * scale);
}

/* ======================= 点阵图 ======================= */

void st7789_draw_bitmap(st7789_t *dev, int16_t x, int16_t y,
                        uint16_t w, uint16_t h, const uint16_t *pixels)
{
    int32_t x0, y0, x1, y1, row;

    if ((dev == NULL) || (pixels == NULL) || (w == 0U) || (h == 0U)) {
        return;
    }

    /* 完全在屏幕内：一次性连续写入，速度最快 */
    if ((x >= 0) && (y >= 0) &&
        ((int32_t)x + w <= (int32_t)dev->w) &&
        ((int32_t)y + h <= (int32_t)dev->h)) {
        st7789_set_window(dev, (uint16_t)x, (uint16_t)y,
                          (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
        st7789_write_pixels(dev, pixels, (uint32_t)w * h);
        return;
    }

    /* 越界时逐行裁剪 */
    x0 = x;
    x1 = (int32_t)x + w - 1;
    y0 = y;
    y1 = (int32_t)y + h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)dev->w) x1 = (int32_t)dev->w - 1;
    if (y1 >= (int32_t)dev->h) y1 = (int32_t)dev->h - 1;
    if ((x0 > x1) || (y0 > y1)) {
        return;
    }

    for (row = y0; row <= y1; row++) {
        const uint16_t *src = pixels + (row - (int32_t)y) * (int32_t)w + (x0 - (int32_t)x);
        st7789_set_window(dev, (uint16_t)x0, (uint16_t)row, (uint16_t)x1, (uint16_t)row);
        st7789_write_pixels(dev, src, (uint32_t)(x1 - x0 + 1));
    }
}

void st7789_draw_bitmap_1bpp(st7789_t *dev, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, const uint8_t *data,
                             uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t stride;
    uint16_t rowbuf[ST7789_BMP_CHUNK * ST7789_BMP_SCALE_MAX];
    uint16_t row, sx;

    if ((dev == NULL) || (data == NULL) || (w == 0U) || (h == 0U)) {
        return;
    }
    if (scale == 0U) {
        scale = 1U;
    }
    if (scale > ST7789_BMP_SCALE_MAX) {
        scale = ST7789_BMP_SCALE_MAX;
    }
    if ((x < 0) || (y < 0) ||
        ((int32_t)x + (int32_t)w * scale > (int32_t)dev->w) ||
        ((int32_t)y + (int32_t)h * scale > (int32_t)dev->h)) {
        return; /* 点阵图需完整位于屏幕内 */
    }

    stride = (uint16_t)((w + 7U) / 8U);

    for (row = 0; row < h; row++) {
        const uint8_t *src = data + (uint32_t)row * stride;
        int16_t py = (int16_t)(y + (int32_t)row * scale);

        for (sx = 0; sx < w; sx = (uint16_t)(sx + ST7789_BMP_CHUNK)) {
            uint16_t n = (uint16_t)(w - sx);
            uint16_t i, s;
            int16_t px;

            if (n > ST7789_BMP_CHUNK) {
                n = ST7789_BMP_CHUNK;
            }
            for (i = 0; i < n; i++) {
                uint16_t bitpos = (uint16_t)(sx + i);
                uint8_t on = (uint8_t)((src[bitpos >> 3] >> (7U - (bitpos & 7U))) & 0x01U);
                uint16_t color = on ? fg : bg;
                for (s = 0; s < scale; s++) {
                    rowbuf[i * scale + s] = color;
                }
            }
            px = (int16_t)(x + (int32_t)sx * scale);

            st7789_set_window(dev, (uint16_t)px, (uint16_t)py,
                              (uint16_t)(px + (int32_t)n * scale - 1),
                              (uint16_t)(py + scale - 1));
            for (s = 0; s < scale; s++) {
                st7789_write_pixels(dev, rowbuf, (uint32_t)n * scale);
            }
        }
    }
}

/* ======================= 波形显示 ======================= */

static int16_t wave_map_y(const st7789_wave_cfg_t *cfg, int16_t value)
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

static void wave_draw(st7789_t *dev, st7789_wave_t *wave)
{
    const st7789_wave_cfg_t *cfg;
    uint16_t i;
    int16_t prev_x, prev_y;

    if ((dev == NULL) || (wave == NULL)) {
        return;
    }
    cfg = &wave->cfg;
    if ((cfg->w == 0U) || (cfg->h == 0U)) {
        return;
    }

    /* 背景 */
    st7789_fill_rect(dev, cfg->x, cfg->y, (int16_t)cfg->w, (int16_t)cfg->h, cfg->bg_color);

    /* 网格 */
    if (cfg->show_grid) {
        for (i = 1; i < 4U; i++) {
            int16_t gy = (int16_t)((int32_t)cfg->y + (int32_t)cfg->h * i / 4);
            st7789_draw_hline(dev, cfg->x, gy, (int16_t)cfg->w, cfg->grid_color);
        }
    }

    /* 零轴 */
    if ((cfg->y_min < 0) && (cfg->y_max > 0)) {
        st7789_draw_hline(dev, cfg->x, wave_map_y(cfg, 0), (int16_t)cfg->w, cfg->axis_color);
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
            px = (int16_t)((int32_t)cfg->x + (int32_t)i * ((int32_t)cfg->w - 1) / ((int32_t)wave->count - 1));
        } else {
            px = (int16_t)((int32_t)cfg->x + i);
        }
        py = wave_map_y(cfg, cfg->buf[i]);

        top = (prev_y < py) ? prev_y : py;
        bot = (prev_y > py) ? prev_y : py;
        st7789_draw_vline(dev, prev_x, top, (int16_t)(bot - top + 1), cfg->fg_color);

        prev_x = px;
        prev_y = py;
    }
    st7789_draw_vline(dev, prev_x, prev_y, 1, cfg->fg_color);
}

void st7789_wave_init(st7789_wave_t *wave, const st7789_wave_cfg_t *cfg)
{
    if ((wave == NULL) || (cfg == NULL)) {
        return;
    }
    wave->cfg = *cfg;
    wave->count = 0U;
}

void st7789_wave_clear(st7789_t *dev, st7789_wave_t *wave)
{
    if (wave == NULL) {
        return;
    }
    wave->count = 0U;
    wave_draw(dev, wave);
}

void st7789_wave_push(st7789_t *dev, st7789_wave_t *wave, int16_t value)
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

void st7789_wave_set_data(st7789_t *dev, st7789_wave_t *wave,
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

void st7789_draw_waveform(st7789_t *dev, int16_t x, int16_t y,
                          uint16_t w, uint16_t h,
                          const int16_t *values, uint16_t count,
                          int16_t y_min, int16_t y_max,
                          uint16_t fg, uint16_t bg)
{
    st7789_wave_t wave;

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
