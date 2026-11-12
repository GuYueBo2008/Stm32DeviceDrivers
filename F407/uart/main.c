/**
 * @file    main.c
 * @brief   UART 驱动使用示例 —— 跑一个简单的 echo 程序
 *
 * 接线:
 *    PA9  (USART1_TX)  -> USB-TTL 的 RX
 *    PA10 (USART1_RX)  <- USB-TTL 的 TX
 *    GND              -> GND   (必须共地！)
 *
 * 功能:
 *    上电后打印一条欢迎信息；PC 端任意发数据，板子会原样回显。
 */

#include "uart.h"
#include <string.h>

/* 简单的毫秒级延时（替换为工程里已有的延时函数，比如 SysTick） */
static void delay_ms(volatile uint32_t ms)
{
    volatile uint32_t i, j;
    for (i = 0; i < ms; i++)
        for (j = 0; j < 16000; j++);
}

/* 用阻塞模式发送字符串 —— 最省事的封装 */
static int uart_puts(const char *s)
{
    return uart_send((const uint8_t *)s, (uint16_t)strlen(s), UART_MODE_BLOCK);
}

/* 用阻塞模式发送单个字符 */
static int uart_putc(char c)
{
    return uart_send((const uint8_t *)&c, 1, UART_MODE_BLOCK);
}

int main(void)
{
    uint8_t rx_buf[128];
    uint16_t n;

    /* 1. 初始化 UART (GPIO + USART + DMA + NVIC) */
    uart_init();

    /* 2. 启动信息 —— 阻塞模式发，保证打印完整 */
    uart_puts("\r\n===== UART DMA Driver Ready =====\r\n");
    uart_puts("Baud: 115200, 8N1. Type anything, I'll echo it.\r\n");

    /* 3. 用 DMA 模式发一条 —— 立即返回，不等发完
     *    uart_tx_busy() 可用于查询发送是否结束 */
    const char *dma_msg = "[DMA] hello from STM32F407\r\n";
    uart_send((const uint8_t *)dma_msg, (uint16_t)strlen(dma_msg), UART_MODE_DMA);

    /* 主循环: 从环形缓冲区取数据 -> 回显 */
    while (1) {
        /* 带超时读取，避免死循环空转 */
        n = uart_read_timeout(rx_buf, sizeof(rx_buf), 100);

        if (n > 0) {
            /* 直接用阻塞模式回显 —— 简单可靠 */
            uart_send(rx_buf, n, UART_MODE_BLOCK);

            /* 顺便打印个计数，演示 uart_send DMA + 查询 busy */
            static uint32_t echo_cnt = 0;
            echo_cnt++;
            if (echo_cnt % 10 == 0) {
                uart_puts("\r\n[echo count: ]");
                /* 这里可以换成 printf("%lu", echo_cnt)，
                 * 工程若无重定向则自行写一个整数转字符串 */
            }
        }

        /* 空闲时做点别的也行，喂狗等 */
        delay_ms(1);
    }
}
