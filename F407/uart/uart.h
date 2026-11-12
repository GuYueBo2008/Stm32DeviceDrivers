/**
 * @file    uart.h
 * @brief   STM32F407 UART 驱动 —— DMA 收发 + IDLE 空闲中断 + 环形缓冲区
 * @note    基于 STM32F4xx Standard Peripheral Library (SPL)
 *
 *          发送策略: DMA（默认） / 阻塞轮询 / 中断，可通过 uart_send() 的
 *          参数 send_mode 切换
 *
 *          接收策略: DMA 循环搬运 + IDLE 空闲中断判定一帧结束，数据写入
 *          环形缓冲区；main 侧通过 uart_read() 从缓冲区取数据
 */

#ifndef __UART_H
#define __UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx.h"
#include <stddef.h>
#include <stdint.h>

/* ======================== 可修改的硬件配置 ======================== */

/* 选择 USART 实例，默认 USART1 (PA9 TX / PA10 RX) */
#ifndef UARTx
#define UARTx               USART1
#define UARTx_CLK           RCC_APB2Periph_USART1
#define UARTx_IRQn          USART1_IRQn
#define UARTx_IRQHandler    USART1_IRQHandler

#define UART_GPIO_CLK       RCC_AHB1Periph_GPIOA
#define UART_GPIO_AF        GPIO_AF_USART1
#define UART_TX_PIN         GPIO_Pin_9
#define UART_RX_PIN         GPIO_Pin_10
#define UART_TX_PORT        GPIOA
#define UART_RX_PORT        GPIOA
#endif

/* DMA 配置 (USART1_RX = DMA2_Stream5_CH4, USART1_TX = DMA2_Stream7_CH4) */
#ifndef UART_DMA_RX_STREAM
#define UART_DMA_CLK        RCC_AHB1Periph_DMA2
#define UART_DMA_RX_STREAM  DMA2_Stream5
#define UART_DMA_RX_CH      DMA_Channel_4
#define UART_DMA_RX_IRQn    DMA2_Stream5_IRQn
#define UART_DMA_RX_IRQHandler DMA2_Stream5_IRQHandler

#define UART_DMA_TX_STREAM  DMA2_Stream7
#define UART_DMA_TX_CH      DMA_Channel_4
#define UART_DMA_TX_IRQn    DMA2_Stream7_IRQn
#define UART_DMA_TX_IRQHandler DMA2_Stream7_IRQHandler
#endif

/* 默认波特率 */
#ifndef UART_BAUDRATE
#define UART_BAUDRATE       115200
#endif

/* DMA 单次搬运的缓冲区长度（半满/满中断各搬一半） */
#ifndef UART_DMA_BUF_SIZE
#define UART_DMA_BUF_SIZE   64
#endif

/* 环形缓冲区大小（必须是 2 的幂，便于 & 运算取模） */
#ifndef UART_RING_BUF_SIZE
#define UART_RING_BUF_SIZE  256
#endif

/* ======================== 数据结构 ======================== */

/**
 * @brief 环形缓冲区
 */
typedef struct {
    uint8_t  buf[UART_RING_BUF_SIZE];
    volatile uint16_t head;   /* 写入位置 (中断侧) */
    volatile uint16_t tail;   /* 读取位置 (main 侧) */
} UART_RingBuf_t;

/**
 * @brief 发送模式
 */
typedef enum {
    UART_MODE_DMA   = 0,   /**< DMA 发送（异步，立即返回） */
    UART_MODE_BLOCK = 1,   /**< 阻塞轮询（等待发完） */
    UART_MODE_IRQ   = 2    /**< 中断发送（异步，立即返回） */
} UART_SendMode_t;

/* ======================== 对外 API ======================== */

/**
 * @brief 初始化 UART（GPIO + USART + DMA + NVIC）
 *        启动 DMA 循环接收，打开 IDLE 空闲中断
 */
void uart_init(void);

/**
 * @brief 发送数据
 * @param data 待发送数据指针
 * @param len  待发送长度（字节）
 * @param mode 发送模式 (DMA / 阻塞 / 中断)
 * @retval 0 成功, -1 参数错误, -2 DMA 忙, -3 超时(阻塞模式)
 */
int uart_send(const uint8_t *data, uint16_t len, UART_SendMode_t mode);

/**
 * @brief 从环形缓冲区读取数据（非阻塞）
 * @param buf  接收缓冲区
 * @param size 缓冲区最大容量
 * @retval 实际读取字节数 (0 表示暂无数据)
 */
uint16_t uart_read(uint8_t *buf, uint16_t size);

/**
 * @brief 从环形缓冲区读取数据（带超时的阻塞读取）
 * @param buf     接收缓冲区
 * @param size    缓冲区最大容量
 * @param timeout 超时 (毫秒, 0 = 非阻塞)
 * @retval 实际读取字节数 (0 表示超时)
 */
uint16_t uart_read_timeout(uint8_t *buf, uint16_t size, uint32_t timeout);

/**
 * @brief 环形缓冲区中当前可读字节数
 */
uint16_t uart_available(void);

/**
 * @brief 清空环形缓冲区
 */
void uart_flush_rx(void);

/**
 * @brief DMA TX 是否正在发送（DMA 模式下用于查询发送完成）
 */
uint8_t uart_tx_busy(void);

/* ======================== 中断回调（需在中断服务中调用） ======================== */

/**
 * @brief USART IDLE 空闲中断处理（一帧结束后将 DMA 数据搬入环形缓冲区）
 */
void uart_idle_irq_handler(void);

/**
 * @brief DMA RX 半满/满中断处理
 */
void uart_dma_rx_irq_handler(void);

/**
 * @brief DMA TX 完成中断处理
 */
void uart_dma_tx_irq_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_H */
