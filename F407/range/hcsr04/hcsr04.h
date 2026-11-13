/**
 * @file    hcsr04.h
 * @brief   HC-SR04 超声波传感器驱动 (STM32F4xx HAL)
 *
 *          硬件接线：
 *            TRIG -> 任意 GPIO 输出 (推挽)
 *            ECHO -> 任意 GPIO 输入 (浮空/上拉) + 通用定时器输入捕获通道
 *          使用方法：
 *            1. 在 CubeMX 中启用一个定时器作为"微秒计时器"（或复用已有），
 *               配置为 1MHz 计数频率 (Prescaler = TIM_CLK_MHz - 1)，
 *               并开启 ECHO 引脚对应的输入捕获通道 (捕获上升沿+下降沿)。
 *            2. 在本文件下方的 USER CONFIG 区域修改宏以匹配你的工程。
 *            3. 调用 HCSR04_Init() 完成初始化。
 *            4. 主循环中调用 HCSR04_GetDistance() 获取距离 (cm)。
 */

#ifndef __HCSR04_H
#define __HCSR04_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "stm32f4xx_hal.h"

/* ================================================================
 *  USER CONFIG: 按你的硬件 / CubeMX 配置修改以下宏
 * ================================================================ */

/* ------- TRIG 触发引脚 (GPIO 输出) ------- */
#define HCSR04_TRIG_PORT        GPIOA
#define HCSR04_TRIG_PIN         GPIO_PIN_0

/* ------- ECHO 回波引脚 (定时器输入捕获通道) ------- */
#define HCSR04_ECHO_PORT        GPIOA
#define HCSR04_ECHO_PIN         GPIO_PIN_1

/* ------- 微秒定时器句柄 (CubeMX 生成的定时器句柄) ------- */
/* 该定时器需配置为：计数频率 = 1 MHz (即每 tick = 1 us)   */
#define HCSR04_TIMER_HANDLE     htim2
/* 驱动自己声明 extern, 保证编译过; 若外部已声明过也不冲突 (extern 允许重复) */
extern TIM_HandleTypeDef HCSR04_TIMER_HANDLE;

/* ------- ECHO 对应的定时器通道 ------- */
/* 可选: TIM_CHANNEL_1 / TIM_CHANNEL_2 / TIM_CHANNEL_3 / TIM_CHANNEL_4 */
#define HCSR04_ECHO_CHANNEL     TIM_CHANNEL_2

/* ------- 定时器输入捕获 DMA (可选, 设为 0 则使用轮询方式) ------- */
/* 若启用 DMA 捕获, 在 CubeMX 中为对应通道配置 DMA Normal 模式,
   然后把这里填入 DMA 句柄 (例如 hdma_tim2_ch2)                */
#define HCSR04_USE_DMA          0
#if HCSR04_USE_DMA
  #define HCSR04_DMA_HANDLE     hdma_tim2_ch2
  extern DMA_HandleTypeDef HCSR04_DMA_HANDLE;
#else
  #define HCSR04_DMA_HANDLE     0
#endif

/* ------- 声速系数 & 测量量程 ------- */
/* 声速按 340 m/s, 往返 => 每 cm 需要 2e-2 / 340 s = 58.82 us    */
/* 即: distance_cm = echo_us / HCSR04_US_PER_CM                 */
#define HCSR04_US_PER_CM        58.82f

/* HC-SR04 最小/最大量程 (cm). 超出视为无效, 驱动返回 0 */
#define HCSR04_RANGE_MIN_CM     2.0f
#define HCSR04_RANGE_MAX_CM     400.0f

/* ================================================================
 *  API
 * ================================================================ */

/**
 * @brief 初始化 HC-SR04 驱动
 *        1) 配置 TRIG 为推挽输出
 *        2) 启动 ECHO 通道的输入捕获 (含 DMA, 若启用)
 *        3) 启动定时器并复位计数器
 * @note  调用前请确保 htim 已在 main.c 中 MX_TIMx_Init() 过
 */
void HCSR04_Init(void);

/**
 * @brief 获取一次超声波测距结果 (非阻塞, 约需 25ms 以内返回)
 * @return 距离, 单位 cm (float).
 *         - 正常:  2.0 ~ 400.0
 *         - 失败:  0.0f (超时/无效/未收到回波)
 * @note  内部包含发送 10us TRIG 脉冲 + 等待捕获完成的全过程,
 *        请勿在高频中断中调用.
 */
float HCSR04_GetDistance(void);

/**
 * @brief 关闭输入捕获 (省电)
 */
void HCSR04_DeInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __HCSR04_H */
