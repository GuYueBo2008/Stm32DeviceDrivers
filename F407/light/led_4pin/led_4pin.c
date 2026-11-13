/**
 * @file    led_4pin.c
 * @brief   4 脚 RGB 三色 LED 驱动（STM32F407 + HAL 库）
 */
#include "led_4pin.h"

/** 判断颜色通道是否合法 */
static uint8_t led_4pin_color_is_valid(led_4pin_color_t color)
{
    return ((uint32_t)color < LED_4PIN_CHANNEL_COUNT) ? 1U : 0U;
}

/**
 * @brief  按亮度值驱动单个通道，内部统一处理共阴/共阳的极性
 */
static void led_4pin_apply(const led_4pin_t *led, uint8_t index, uint8_t value)
{
    const led_4pin_channel_t *ch = &led->ch[index];

    if (ch->htim != NULL)
    {
        /* 一个 PWM 周期的计数值，CCR = period 即 100% 占空比 */
        uint32_t period = (uint32_t)ch->htim->Init.Period + 1U;
        uint32_t duty   = ((uint32_t)value * period) / LED_4PIN_VALUE_MAX;

        /* 共阳时占空比取反：0 输出常高（熄灭），255 输出常低（最亮） */
        if (led->common == LED_4PIN_COMMON_ANODE)
        {
            duty = period - duty;
        }

        __HAL_TIM_SET_COMPARE(ch->htim, ch->channel, duty);
    }
    else if (ch->port != NULL)
    {
        uint8_t on = (value > 0U) ? 1U : 0U;

        /* 共阳时点亮电平取反 */
        if (led->common == LED_4PIN_COMMON_ANODE)
        {
            on = (on != 0U) ? 0U : 1U;
        }

        HAL_GPIO_WritePin(ch->port, ch->pin,
                          (on != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    else
    {
        /* 通道未绑定，忽略 */
    }
}

led_4pin_status_t led_4pin_init(led_4pin_t *led, led_4pin_common_t common)
{
    uint8_t i;

    if ((led == NULL) ||
        ((common != LED_4PIN_COMMON_CATHODE) && (common != LED_4PIN_COMMON_ANODE)))
    {
        return LED_4PIN_ERROR_PARAM;
    }

    led->common = common;

    for (i = 0U; i < LED_4PIN_CHANNEL_COUNT; i++)
    {
        led->ch[i].port    = NULL;
        led->ch[i].pin     = 0U;
        led->ch[i].htim    = NULL;
        led->ch[i].channel = 0U;
        led->rgb[i]        = 0U;
    }

    return LED_4PIN_OK;
}

led_4pin_status_t led_4pin_bind_gpio(led_4pin_t *led,
                                     led_4pin_color_t color,
                                     GPIO_TypeDef *port,
                                     uint16_t pin)
{
    led_4pin_channel_t *ch;

    if ((led == NULL) || (port == NULL) || (pin == 0U) ||
        (led_4pin_color_is_valid(color) == 0U))
    {
        return LED_4PIN_ERROR_PARAM;
    }

    ch          = &led->ch[color];
    ch->port    = port;
    ch->pin     = pin;
    ch->htim    = NULL;
    ch->channel = 0U;

    /* 绑定后默认熄灭 */
    led_4pin_set_channel(led, color, 0U);

    return LED_4PIN_OK;
}

led_4pin_status_t led_4pin_bind_pwm(led_4pin_t *led,
                                    led_4pin_color_t color,
                                    TIM_HandleTypeDef *htim,
                                    uint32_t channel)
{
    led_4pin_channel_t *ch;

    if ((led == NULL) || (htim == NULL) ||
        ((channel != TIM_CHANNEL_1) && (channel != TIM_CHANNEL_2) &&
         (channel != TIM_CHANNEL_3) && (channel != TIM_CHANNEL_4)) ||
        (led_4pin_color_is_valid(color) == 0U))
    {
        return LED_4PIN_ERROR_PARAM;
    }

    ch          = &led->ch[color];
    ch->port    = NULL;
    ch->pin     = 0U;
    ch->htim    = htim;
    ch->channel = channel;

    /* 先写入熄灭占空比，再启动 PWM，避免上电闪一下 */
    led_4pin_set_channel(led, color, 0U);

    if (HAL_TIM_PWM_Start(htim, channel) != HAL_OK)
    {
        ch->htim = NULL;
        return LED_4PIN_ERROR;
    }

    return LED_4PIN_OK;
}

void led_4pin_set_channel(led_4pin_t *led, led_4pin_color_t color, uint8_t value)
{
    if ((led == NULL) || (led_4pin_color_is_valid(color) == 0U))
    {
        return;
    }

    /* 未绑定任何接口的通道直接忽略 */
    if ((led->ch[color].port == NULL) && (led->ch[color].htim == NULL))
    {
        return;
    }

    led_4pin_apply(led, (uint8_t)color, value);
    led->rgb[color] = value;
}

void led_4pin_set_rgb(led_4pin_t *led, uint8_t r, uint8_t g, uint8_t b)
{
    if (led == NULL)
    {
        return;
    }

    led_4pin_set_channel(led, LED_4PIN_R, r);
    led_4pin_set_channel(led, LED_4PIN_G, g);
    led_4pin_set_channel(led, LED_4PIN_B, b);
}

void led_4pin_off(led_4pin_t *led)
{
    led_4pin_set_rgb(led, 0U, 0U, 0U);
}
