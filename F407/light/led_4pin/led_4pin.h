/**
 * @file    led_4pin.h
 * @brief   4 脚 RGB 三色 LED 驱动（STM32F407 + HAL 库）
 *
 * 4 脚 RGB LED 由 R、G、B 三个发光二极管和一个公共端组成：
 *   - 共阴（共地）：公共端接 GND，R/G/B 引脚输出高电平点亮；
 *   - 共阳：公共端接 VCC，R/G/B 引脚输出低电平点亮。
 * 驱动会根据 led_4pin_common_t 自动处理点亮电平，调用者只需传入正常语义的
 * RGB 值（0 = 灭，255 = 最亮），无需关心共阴/共阳的极性。
 *
 * 每个颜色通道可独立选择驱动方式：
 *   - GPIO：仅开关控制，value > 0 即点亮；
 *   - PWM ：按 0~255 线性调光，建议把定时器 ARR 设为 255（8 位分辨率）。
 *
 * 使用示例（共阴，GPIO 方式）：
 *   led_4pin_t led;
 *   led_4pin_init(&led, LED_4PIN_COMMON_CATHODE);
 *   led_4pin_bind_gpio(&led, LED_4PIN_R, GPIOE, GPIO_PIN_9);
 *   led_4pin_bind_gpio(&led, LED_4PIN_G, GPIOE, GPIO_PIN_11);
 *   led_4pin_bind_gpio(&led, LED_4PIN_B, GPIOE, GPIO_PIN_13);
 *   led_4pin_set_rgb(&led, 255, 128, 0);      // 橙色
 *
 * 使用示例（共阳，PWM 调光）：
 *   led_4pin_t led;
 *   led_4pin_init(&led, LED_4PIN_COMMON_ANODE);
 *   led_4pin_bind_pwm(&led, LED_4PIN_R, &htim3, TIM_CHANNEL_1);
 *   led_4pin_bind_pwm(&led, LED_4PIN_G, &htim3, TIM_CHANNEL_2);
 *   led_4pin_bind_pwm(&led, LED_4PIN_B, &htim3, TIM_CHANNEL_3);
 *   led_4pin_set_rgb(&led, 0, 0, 255);        // 蓝色
 *
 * 说明：
 *   - GPIO 引脚与定时器 PWM 通道均需先在 CubeMX / 初始化代码中配置好，
 *     本驱动只负责输出电平和占空比，不做外设初始化；
 *   - 使用 PWM 时假定通道工作在 PWM 模式 1（CubeMX 默认），且 ARR 已配置；
 *   - 绑定 PWM 通道时驱动会调用 HAL_TIM_PWM_Start() 启动输出，熄灭仅把
 *     占空比置为 0，不会停止定时器；
 *   - 公共端需按共阴/共阳正确接线，R/G/B 引脚请串联限流电阻，避免超过
 *     F407 单脚 25mA 的灌/拉电流能力。
 */
#ifndef __LED_4PIN_H__
#define __LED_4PIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "stm32f4xx_hal.h"

/** 通道数量（R/G/B） */
#define LED_4PIN_CHANNEL_COUNT      3U
/** 亮度最大值（0~255） */
#define LED_4PIN_VALUE_MAX          255U

/** 驱动返回值 */
typedef enum
{
    LED_4PIN_OK = 0,                /**< 操作成功 */
    LED_4PIN_ERROR,                 /**< 硬件操作失败 */
    LED_4PIN_ERROR_PARAM            /**< 参数错误 */
} led_4pin_status_t;

/** 公共端接法 */
typedef enum
{
    LED_4PIN_COMMON_CATHODE = 0,    /**< 共阴（共地）：高电平点亮 */
    LED_4PIN_COMMON_ANODE   = 1     /**< 共阳：低电平点亮 */
} led_4pin_common_t;

/** 颜色通道索引 */
typedef enum
{
    LED_4PIN_R = 0,                 /**< 红色通道 */
    LED_4PIN_G = 1,                 /**< 绿色通道 */
    LED_4PIN_B = 2                  /**< 蓝色通道 */
} led_4pin_color_t;

/** 单个颜色通道的接口描述 */
typedef struct
{
    GPIO_TypeDef *port;             /**< GPIO 端口，PWM 方式时为 NULL */
    uint16_t      pin;              /**< GPIO 引脚，PWM 方式时为 0 */
    TIM_HandleTypeDef *htim;        /**< 定时器句柄，GPIO 方式时为 NULL */
    uint32_t      channel;          /**< PWM 通道，例如 TIM_CHANNEL_1 */
} led_4pin_channel_t;

/** RGB LED 句柄，支持多实例 */
typedef struct
{
    led_4pin_common_t    common;                    /**< 公共端接法 */
    led_4pin_channel_t   ch[LED_4PIN_CHANNEL_COUNT];/**< 三个颜色通道接口 */
    uint8_t              rgb[LED_4PIN_CHANNEL_COUNT];/**< 当前亮度值（0~255） */
} led_4pin_t;

/**
 * @brief  初始化 RGB LED 驱动
 * @note   会清空所有通道绑定信息，必须在 led_4pin_bind_gpio() /
 *         led_4pin_bind_pwm() 之前调用。
 * @param  led    LED 句柄
 * @param  common 公共端接法（共阴 / 共阳）
 * @retval LED_4PIN_OK 初始化成功
 * @retval LED_4PIN_ERROR_PARAM 参数错误
 */
led_4pin_status_t led_4pin_init(led_4pin_t *led, led_4pin_common_t common);

/**
 * @brief  将某个颜色通道绑定到 GPIO 引脚（开关控制）
 * @note   引脚需已配置为推挽输出；绑定后该通道默认熄灭。
 * @param  led   LED 句柄
 * @param  color 颜色通道（LED_4PIN_R / G / B）
 * @param  port  GPIO 端口，例如 GPIOE
 * @param  pin   GPIO 引脚，例如 GPIO_PIN_9
 * @retval LED_4PIN_OK 绑定成功
 * @retval LED_4PIN_ERROR_PARAM 参数错误
 */
led_4pin_status_t led_4pin_bind_gpio(led_4pin_t *led,
                                     led_4pin_color_t color,
                                     GPIO_TypeDef *port,
                                     uint16_t pin);

/**
 * @brief  将某个颜色通道绑定到定时器 PWM 通道（0~255 调光）
 * @note   定时器需已完成 PWM 初始化（HAL_TIM_PWM_Init + 通道配置），
 *         建议 ARR = 255。绑定时驱动会启动该通道 PWM 并默认熄灭。
 * @param  led     LED 句柄
 * @param  color   颜色通道（LED_4PIN_R / G / B）
 * @param  htim    定时器句柄，例如 &htim3
 * @param  channel PWM 通道（TIM_CHANNEL_1 ~ TIM_CHANNEL_4）
 * @retval LED_4PIN_OK 绑定成功
 * @retval LED_4PIN_ERROR_PARAM 参数错误
 * @retval LED_4PIN_ERROR PWM 启动失败
 */
led_4pin_status_t led_4pin_bind_pwm(led_4pin_t *led,
                                    led_4pin_color_t color,
                                    TIM_HandleTypeDef *htim,
                                    uint32_t channel);

/**
 * @brief  设置单个颜色通道的亮度
 * @note   未绑定的通道会被忽略。GPIO 方式下 value > 0 即点亮，
 *         PWM 方式下按 value / 255 线性输出占空比。
 * @param  led   LED 句柄
 * @param  color 颜色通道（LED_4PIN_R / G / B）
 * @param  value 亮度值，0（灭）~ 255（最亮）
 */
void led_4pin_set_channel(led_4pin_t *led, led_4pin_color_t color, uint8_t value);

/**
 * @brief  设置 RGB 三色亮度
 * @param  led LED 句柄
 * @param  r   红色亮度，0 ~ 255
 * @param  g   绿色亮度，0 ~ 255
 * @param  b   蓝色亮度，0 ~ 255
 */
void led_4pin_set_rgb(led_4pin_t *led, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  熄灭 LED（三个通道亮度全部置 0）
 * @param  led LED 句柄
 */
void led_4pin_off(led_4pin_t *led);

#ifdef __cplusplus
}
#endif

#endif /* __LED_4PIN_H__ */
