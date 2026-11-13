/**
 * @file    main.c
 * @brief   HC-SR04 测距 + UART 输出示例（CubeMX + HAL 工程）
 *
 * 依赖:
 *    本目录 hcsr04.c / hcsr04.h
 *    ../uart/uart.c / ../uart/uart.h   (firmware 目录 F407/uart)
 *
 * 接线:
 *    HC-SR04 VCC  -> 5V
 *    HC-SR04 GND  -> GND
 *    HC-SR04 TRIG -> PA0  (GPIO_Output)
 *    HC-SR04 ECHO -> PA1  (TIM2_CH2, 需 5V->3.3V 分压)   ⚠️
 *    PA9  (USART1_TX) -> USB-TTL 的 RX
 *    PA10 (USART1_RX) <- USB-TTL 的 TX
 *    所有 GND 必须共地
 *
 * CubeMX 需要的外设:
 *    USART1  115200 8N1 + DMA(RX Circular / TX Normal) + USART1 全局中断
 *    TIM2    Internal Clock, CH2 = Input Capture direct mode,
 *            Prescaler = APB1定时器时钟(MHz) - 1 (84MHz -> 83),
 *            Counter Period = 0xFFFFFFFF, Polarity = Rising Edge
 *    PA0     GPIO_Output
 *
 * @note 本文件是完整示例。放进 CubeMX 生成的工程时，只需把下面 USER CODE
 *       部分的内容搬到生成的 main.c 对应区段，其余生成代码保持不变。
 */

#include "main.h"
#include "hcsr04.h"
#include "uart.h"
#include <stdio.h>

/* CubeMX 生成的初始化函数声明 */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_DMA_Init(void);
extern void MX_USART1_UART_Init(void);
extern void MX_TIM2_Init(void);

int main(void)
{
    char line[64];
    float dist;

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();              /* 注意：DMA 必须先于 USART 初始化 */
    MX_USART1_UART_Init();
    MX_TIM2_Init();             /* TIM2 作为 HC-SR04 的 1MHz 计时基准 */

    /* 启动 UART 的 DMA + IDLE 接收 */
    if (uart_init() != 0) {
        /* 启动失败：通常是 CubeMX 里没开 DMA 或 USART1 全局中断 */
        Error_Handler();
    }

    /* 初始化超声波驱动 */
    HCSR04_Init();

    uart_puts("\r\n===== HC-SR04 + UART Ready =====\r\n");
    uart_puts("Baud: 115200, 8N1. Distance in cm.\r\n");

    while (1) {
        dist = HCSR04_GetDistance();

        if (dist > 0.0f) {
            int n = snprintf(line, sizeof(line), "Distance: %.2f cm\r\n", dist);
            if (n > 0) {
                /* uart_puts 为阻塞发送，返回即已发完 */
                uart_puts(line);
            }
        } else {
            uart_puts("Distance: out of range\r\n");
        }

        /* HC-SR04 两次触发之间建议 >= 50ms，这里留 60ms */
        HAL_Delay(60);
    }
}

/**
 * @brief 若工程里没有 CubeMX 生成的错误处理，可保留此实现
 */
void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}
