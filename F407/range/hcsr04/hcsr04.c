/**
 * @file    hcsr04.c
 * @brief   HC-SR04 超声波传感器驱动 (STM32F4xx HAL)
 *
 *          默认使用 "定时器输入捕获 + 轮询" 方式测量 ECHO 脉宽,
 *          不需要中断/DMA, 易于集成.
 *
 *          若在 hcsr04.h 中设置 HCSR04_USE_DMA = 1 并配置好 DMA 句柄,
 *          本驱动会切换到 DMA 捕获方式, 避免 CPU 阻塞.
 *
 * @warning 本驱动假定对应定时器已被 CubeMX 初始化为 1 MHz 计数频率
 *          (Prescaler = TIM_CLK_MHz - 1,  CounterMode = Up).
 */

#include "hcsr04.h"
#include <math.h>

/* ================================================================
 *  内部定义 & 类型
 * ================================================================ */

#define HCSR04_TIMER    HCSR04_TIMER_HANDLE
#define HCSR04_TRIG_H() HAL_GPIO_WritePin(HCSR04_TRIG_PORT, HCSR04_TRIG_PIN, GPIO_PIN_SET)
#define HCSR04_TRIG_L() HAL_GPIO_WritePin(HCSR04_TRIG_PORT, HCSR04_TRIG_PIN, GPIO_PIN_RESET)
#define HCSR04_ECHO_R() HAL_GPIO_ReadPin(HCSR04_ECHO_PORT,  HCSR04_ECHO_PIN)

/* 轮询等待的超时上限 (次数). 以最保守 400cm 量程 => ~23.5ms ECHO 脉宽
   再乘以 2 倍裕度. 在 1us per tick 时, 单次 for 循环约 1us.        */
#define HCSR04_WAIT_TIMEOUT_US   60000U

/* DMA 捕获缓冲大小: 2 个点即可 (上升沿+下降沿各 1 次捕获) */
#define HCSR04_DMA_BUF_SIZE      2U

/* 计算两个 tick 的差值 (自动处理 32-bit 计数器溢出回绕) */
static inline uint32_t hcsr04_tick_diff(uint32_t end_tick, uint32_t start_tick)
{
    /* 补码减法天然支持回绕, 无需判断 end < start */
    return end_tick - start_tick;
}

/* ================================================================
 *  模块级状态 (仅 DMA 模式需要持久化)
 * ================================================================ */
#if HCSR04_USE_DMA
static uint32_t hcsr04_dma_buf[HCSR04_DMA_BUF_SIZE];
static volatile uint8_t hcsr04_dma_done = 0;
#endif

/* ================================================================
 *  初始化 / 反初始化
 * ================================================================ */

void HCSR04_Init(void)
{
    /* 1. TRIG 拉低, 确保上电瞬间不会误触发 */
    HCSR04_TRIG_L();

    /* 2. 启动定时器计数器 (假定 CubeMX 已把该 TIM 配为 1MHz 主频) */
    __HAL_TIM_SET_COUNTER(&HCSR04_TIMER, 0);
    HAL_TIM_Base_Start(&HCSR04_TIMER);

    /* 3. ECHO 通道: DMA 模式下启用输入捕获 + DMA; 轮询模式下只要计数器在跑,
       捕获通道本身由 CubeMX 初始化时配置好即可, 驱动里不再显式 Start */
#if HCSR04_USE_DMA
    hcsr04_dma_done = 0;
    HAL_TIM_IC_Start_DMA(&HCSR04_TIMER, HCSR04_ECHO_CHANNEL,
                         (uint32_t*)hcsr04_dma_buf, HCSR04_DMA_BUF_SIZE);
#else
    (void)HCSR04_ECHO_CHANNEL;  /* 轮询模式不使用定时器捕获 */
#endif
}

void HCSR04_DeInit(void)
{
#if HCSR04_USE_DMA
    HAL_TIM_IC_Stop_DMA(&HCSR04_TIMER, HCSR04_ECHO_CHANNEL);
#endif
    HAL_TIM_Base_Stop(&HCSR04_TIMER);
    HCSR04_TRIG_L();
}

/* ================================================================
 *  测距核心: TRIG + 等待 ECHO + 换算距离
 * ================================================================ */

float HCSR04_GetDistance(void)
{
    uint32_t start_tick = 0;
    uint32_t end_tick   = 0;
    uint32_t echo_us    = 0;

    /* ---- 步骤 1: 发送至少 10us 的 TRIG 高脉冲 ---- */
    HCSR04_TRIG_H();
    /* 使用 HAL 提供的延时函数. 若工程中启用了 DWT, 更建议换用
       HAL_Delay_us() (STM32CubeF4 v1.27+ 原生支持) 或自定义 DWT 延时 */
    /* 这里保守使用 HAL_Delay() 并配合 GPIO 翻转; 若 us 级精度不够,
       请改为 DWT 延时. */
    /* 为保证 10us 精度, 用一个空的 NOP 循环来补足 */
    {
        volatile uint32_t us_loop = 0;
        /* 经验值: STM32F4 @84MHz, 一条空循环约 4-5 周期,
           这里取 200 次约 10us (需按实际主频微调) */
        for (us_loop = 0; us_loop < 200; us_loop++) {
            __NOP();
        }
    }
    HCSR04_TRIG_L();

    /* ---- 步骤 2: 等待 ECHO 引脚变高 (上升沿) ---- */
    {
        uint32_t wait = 0;
        while ((HCSR04_ECHO_R() == GPIO_PIN_RESET) && (wait < HCSR04_WAIT_TIMEOUT_US)) {
            wait++;
            __NOP();
        }
        if (wait >= HCSR04_WAIT_TIMEOUT_US) {
            /* 没有收到 ECHO 上升沿, 模块未响应 */
            return 0.0f;
        }
    }

    /* 记录上升沿时刻 (也可直接读取 TIM->CCRx, 但轮询 GPIO 简单可靠) */
    start_tick = __HAL_TIM_GET_COUNTER(&HCSR04_TIMER);

    /* ---- 步骤 3: 等待 ECHO 引脚变低 (下降沿) ---- */
    {
        uint32_t wait = 0;
        while ((HCSR04_ECHO_R() == GPIO_PIN_SET) && (wait < HCSR04_WAIT_TIMEOUT_US)) {
            wait++;
            __NOP();
        }
        if (wait >= HCSR04_WAIT_TIMEOUT_US) {
            /* ECHO 一直保持高电平, 大概率超出量程或前方障碍物太近 */
            return 0.0f;
        }
    }

    end_tick = __HAL_TIM_GET_COUNTER(&HCSR04_TIMER);

    /* ---- 步骤 4: 换算 & 量程过滤 ---- */
    echo_us = hcsr04_tick_diff(end_tick, start_tick);

    /* 计数器复位, 避免长期运行后的溢出 (每测一次复位一次) */
    __HAL_TIM_SET_COUNTER(&HCSR04_TIMER, 0);

    float distance_cm = (float)echo_us / HCSR04_US_PER_CM;

    if (distance_cm < HCSR04_RANGE_MIN_CM || distance_cm > HCSR04_RANGE_MAX_CM) {
        return 0.0f;
    }
    return distance_cm;
}

/* ================================================================
 *  DMA 模式: 发送 TRIG + 依赖 DMA 回调填充 hcsr04_dma_buf
 * ================================================================ */
#if HCSR04_USE_DMA

void HCSR04_GetDistance_DMA(float *out_distance_cm)
{
    if (out_distance_cm == NULL) return;

    hcsr04_dma_done = 0;

    /* 重置 DMA 状态 (若 HAL 未自动处理) */
    /* 此处依赖 HAL_TIM_IC_Start_DMA 自动清空 DMA 传输计数 */

    /* 发送 TRIG */
    HCSR04_TRIG_H();
    {
        volatile uint32_t us_loop = 0;
        for (us_loop = 0; us_loop < 200; us_loop++) { __NOP(); }
    }
    HCSR04_TRIG_L();

    /* 等待 DMA 完成 (DMA 捕获 2 个点: 上升沿 + 下降沿) */
    uint32_t wait = 0;
    while (!hcsr04_dma_done && wait < HCSR04_WAIT_TIMEOUT_US * 2) {
        wait++; __NOP();
    }

    if (!hcsr04_dma_done) { *out_distance_cm = 0.0f; return; }

    uint32_t echo_us = hcsr04_tick_diff(hcsr04_dma_buf[1], hcsr04_dma_buf[0]);
    float d = (float)echo_us / HCSR04_US_PER_CM;
    *out_distance_cm = (d < HCSR04_RANGE_MIN_CM || d > HCSR04_RANGE_MAX_CM) ? 0.0f : d;
}

/* 在你的 main.c 或 stm32f4xx_it.c 里, 需要把 HAL_TIM_IC_CaptureCallback
   中的 DMA 完成标志置位. 为方便使用, 这里提供一个弱定义,
   实际工程里按需要覆盖: */
__weak void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == HCSR04_TIMER.Instance &&
        htim->Channel == HCSR04_ECHO_CHANNEL) {
        hcsr04_dma_done = 1;
    }
}

#endif /* HCSR04_USE_DMA */
