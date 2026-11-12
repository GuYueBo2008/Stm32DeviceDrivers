/**
 * @file    main.c
 * @brief   UART 驱动使用示例（CubeMX + HAL 工程）—— echo 回显
 *
 * 接线:
 *    PA9  (USART1_TX) -> USB-TTL 的 RX
 *    PA10 (USART1_RX) <- USB-TTL 的 TX
 *    GND              -> GND（必须共地）
 *
 * @note 本文件是完整示例。若放进 CubeMX 生成的工程，只需在 USER CODE 区段
 *       补上 include 与 uart_init()/收发调用，其余生成代码保持不变。
 */

#include "main.h"
#include "uart.h"
#include <string.h>

/* CubeMX 生成的初始化函数声明（生成代码中通常在 main.h / usart.h 里） */
extern void SystemClock_Config(void);
extern void MX_GPIO_Init(void);
extern void MX_DMA_Init(void);
extern void MX_USART1_UART_Init(void);

int main(void)
{
    uint8_t rx_buf[128];
    uint16_t n;
    const char *dma_msg = "[DMA] hello from STM32F407\r\n";

    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();              /* 注意：DMA 必须先于 USART 初始化 */
    MX_USART1_UART_Init();

    /* 启动 DMA + IDLE 空闲中断接收 */
    if (uart_init() != 0) {
        /* 启动失败：通常是 CubeMX 里没开 DMA / USART1 全局中断 */
        while (1) {
        }
    }

    /* 阻塞发送：返回即表示已发完，适合启动日志 */
    uart_puts("\r\n===== UART HAL Driver Ready =====\r\n");
    uart_puts("Baud: 115200, 8N1. Type anything, I'll echo it.\r\n");
    /* DMA 发送：立即返回，注意 dma_msg 在发完前必须保持有效 */
    uart_send((const uint8_t *)dma_msg, (uint16_t)strlen(dma_msg),
              UART_MODE_DMA);

    while (1) {
        /* 从环形缓冲区取一帧（50ms 超时） */
        n = uart_read_timeout(rx_buf, sizeof(rx_buf), 50);
        if (n > 0) {
            /* 回显：阻塞模式，简单可靠 */
            uart_send(rx_buf, n, UART_MODE_BLOCK);
        }

        /* 需要等 DMA 发完再做下一步时可以这样查询：
         *   while (uart_tx_busy()) { }
         */
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
