/**
 * @file    buzzer_passive.c
 * @brief   无源蜂鸣器驱动（STM32F407 + HAL 库）
 */
#include "buzzer_passive.h"

/** 单次 PWM 周期允许的最大计数值（按 16 位定时器计算，兼容所有定时器） */
#define BUZZER_PASSIVE_MAX_PERIOD   65535U

/**
 * @brief  根据目标频率配置定时器 PSC / ARR / CCR，输出 50% 占空比方波
 * @note   调用前应确保 PWM 输出已停止。
 */
static void BuzzerPassive_SetFrequency(BuzzerPassive_HandleTypeDef *hbz, uint32_t freq_hz)
{
    TIM_HandleTypeDef *htim = hbz->htim;
    uint32_t total_counts;
    uint32_t prescaler;
    uint32_t period;
    uint32_t compare;

    /* 一个 PWM 周期所需的总计数值（四舍五入） */
    total_counts = (hbz->timer_clock_hz + (freq_hz / 2U)) / freq_hz;

    /* 通过预分频把 ARR 限制在 16 位范围内 */
    prescaler = (total_counts - 1U) / (BUZZER_PASSIVE_MAX_PERIOD + 1U);
    period    = total_counts / (prescaler + 1U);

    if (period == 0U)
    {
        period = 1U;
    }
    period -= 1U;

    /* 50% 占空比 */
    compare = (period + 1U) / 2U;

    /* 同步 HAL 句柄中的参数，便于其他 HAL 接口使用 */
    htim->Init.Prescaler = prescaler;
    htim->Init.Period    = period;

    __HAL_TIM_SET_PRESCALER(htim, prescaler);
    __HAL_TIM_SET_AUTORELOAD(htim, period);
    __HAL_TIM_SET_COMPARE(htim, hbz->channel, compare);
    __HAL_TIM_SET_COUNTER(htim, 0U);

    /* 产生更新事件，使 PSC / ARR 影子寄存器立即生效 */
    HAL_TIM_GenerateEvent(htim, TIM_EVENTSOURCE_UPDATE);
}

BuzzerPassive_StatusTypeDef BuzzerPassive_Init(BuzzerPassive_HandleTypeDef *hbz,
                                               TIM_HandleTypeDef *htim,
                                               uint32_t channel,
                                               uint32_t timer_clock_hz)
{
    if ((hbz == NULL) || (htim == NULL) || (timer_clock_hz == 0U))
    {
        return BUZZER_PASSIVE_ERROR_PARAM;
    }

    if ((channel != TIM_CHANNEL_1) && (channel != TIM_CHANNEL_2) &&
        (channel != TIM_CHANNEL_3) && (channel != TIM_CHANNEL_4))
    {
        return BUZZER_PASSIVE_ERROR_PARAM;
    }

    hbz->htim           = htim;
    hbz->channel        = channel;
    hbz->timer_clock_hz = timer_clock_hz;
    hbz->is_playing     = 0U;

    /* 确保上电后处于静音状态 */
    BuzzerPassive_Stop(hbz);

    return BUZZER_PASSIVE_OK;
}

BuzzerPassive_StatusTypeDef BuzzerPassive_Play(BuzzerPassive_HandleTypeDef *hbz,
                                               uint32_t freq_hz)
{
    uint32_t total_counts;

    if ((hbz == NULL) || (hbz->htim == NULL))
    {
        return BUZZER_PASSIVE_ERROR_PARAM;
    }

    if ((freq_hz < BUZZER_PASSIVE_FREQ_MIN) || (freq_hz > BUZZER_PASSIVE_FREQ_MAX))
    {
        BuzzerPassive_Stop(hbz);
        return BUZZER_PASSIVE_ERROR_PARAM;
    }

    /* 频率过高时一个周期内不足 2 个计数值，无法输出方波 */
    total_counts = hbz->timer_clock_hz / freq_hz;
    if (total_counts < 2U)
    {
        BuzzerPassive_Stop(hbz);
        return BUZZER_PASSIVE_ERROR_PARAM;
    }

    /* 若正在播放，先停止当前声音 */
    BuzzerPassive_Stop(hbz);

    BuzzerPassive_SetFrequency(hbz, freq_hz);

    if (HAL_TIM_PWM_Start(hbz->htim, hbz->channel) != HAL_OK)
    {
        return BUZZER_PASSIVE_ERROR;
    }

    hbz->is_playing = 1U;

    return BUZZER_PASSIVE_OK;
}

void BuzzerPassive_Stop(BuzzerPassive_HandleTypeDef *hbz)
{
    if ((hbz == NULL) || (hbz->htim == NULL))
    {
        return;
    }

    /* 先清零占空比，再关闭 PWM 输出，保证引脚回到低电平 */
    __HAL_TIM_SET_COMPARE(hbz->htim, hbz->channel, 0U);
    (void)HAL_TIM_PWM_Stop(hbz->htim, hbz->channel);

    hbz->is_playing = 0U;
}
