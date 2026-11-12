/**
 * @file    buzzer_passive.h
 * @brief   无源蜂鸣器驱动（STM32F407 + HAL 库）
 * @note    无源蜂鸣器需要外部方波驱动，本驱动通过定时器 PWM 通道输出
 *          50% 占空比的方波，方波频率决定音调。
 *
 *          使用前需在 CubeMX / 初始化代码中完成：
 *            1. 将蜂鸣器引脚配置为对应定时器 PWM 通道的复用推挽输出；
 *            2. 调用 HAL_TIM_PWM_Init() 完成定时器 PWM 初始化。
 *          驱动内部通过修改 PSC / ARR 切换频率，无需再次 Init 定时器。
 *
 *          示例（TIM4_CH1，TIM4 计数时钟 84MHz）：
 *            BuzzerPassive_HandleTypeDef buzzer;
 *            BuzzerPassive_Init(&buzzer, &htim4, TIM_CHANNEL_1, 84000000U);
 *
 *            BuzzerPassive_Play(&buzzer, 2000U);   // 播放 2kHz
 *            BuzzerPassive_Play(&buzzer, 4000U);   // 切换为 4kHz（自动停止原声音）
 *            BuzzerPassive_Stop(&buzzer);          // 停止播放
 */
#ifndef __BUZZER_PASSIVE_H__
#define __BUZZER_PASSIVE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "stm32f4xx_hal.h"

/** 允许播放的最低频率（Hz） */
#define BUZZER_PASSIVE_FREQ_MIN     20U
/** 允许播放的最高频率（Hz） */
#define BUZZER_PASSIVE_FREQ_MAX     20000U

/** 驱动返回值 */
typedef enum
{
    BUZZER_PASSIVE_OK = 0,          /**< 操作成功 */
    BUZZER_PASSIVE_ERROR,           /**< 硬件操作失败 */
    BUZZER_PASSIVE_ERROR_PARAM      /**< 参数错误 */
} BuzzerPassive_StatusTypeDef;

/** 无源蜂鸣器句柄，支持多实例 */
typedef struct
{
    TIM_HandleTypeDef *htim;        /**< 定时器句柄，例如 &htim4 */
    uint32_t channel;               /**< PWM 通道，例如 TIM_CHANNEL_1 */
    uint32_t timer_clock_hz;        /**< 定时器计数时钟（Hz），非 APB 总线时钟 */
    uint8_t  is_playing;            /**< 当前是否正在播放 */
} BuzzerPassive_HandleTypeDef;

/**
 * @brief  初始化无源蜂鸣器驱动
 * @param  hbz            蜂鸣器句柄
 * @param  htim           已由 HAL_TIM_PWM_Init() 初始化的定时器句柄
 * @param  channel        PWM 通道（TIM_CHANNEL_1 ~ TIM_CHANNEL_4）
 * @param  timer_clock_hz 定时器计数时钟频率（Hz）
 *                        F407 中 APB1 定时器（TIM2~TIM7）在 APB1 分频不为 1 时
 *                        为 PCLK1 * 2，APB2 定时器（TIM1/TIM8/TIM9~TIM11）
 *                        为 PCLK2 * 2，请按实际工程时钟填写。
 * @retval BUZZER_PASSIVE_OK 初始化成功
 * @retval BUZZER_PASSIVE_ERROR_PARAM 参数错误
 */
BuzzerPassive_StatusTypeDef BuzzerPassive_Init(BuzzerPassive_HandleTypeDef *hbz,
                                               TIM_HandleTypeDef *htim,
                                               uint32_t channel,
                                               uint32_t timer_clock_hz);

/**
 * @brief  播放指定频率的声音
 * @note   若当前正在播放，会先停止当前声音再以新频率播放。
 *         频率超出 BUZZER_PASSIVE_FREQ_MIN ~ BUZZER_PASSIVE_FREQ_MAX 时，
 *         会停止播放并返回错误。
 * @param  hbz     蜂鸣器句柄
 * @param  freq_hz 目标频率（Hz）
 * @retval BUZZER_PASSIVE_OK 播放成功
 * @retval BUZZER_PASSIVE_ERROR_PARAM 参数错误（同时已停止播放）
 * @retval BUZZER_PASSIVE_ERROR PWM 启动失败
 */
BuzzerPassive_StatusTypeDef BuzzerPassive_Play(BuzzerPassive_HandleTypeDef *hbz,
                                               uint32_t freq_hz);

/**
 * @brief  停止播放
 * @param  hbz 蜂鸣器句柄
 */
void BuzzerPassive_Stop(BuzzerPassive_HandleTypeDef *hbz);

#ifdef __cplusplus
}
#endif

#endif /* __BUZZER_PASSIVE_H__ */
