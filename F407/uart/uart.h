/**
 * @file    uart.h
 * @brief   STM32F407 UART 驱动（HAL + DMA + IDLE 空闲中断 + 环形缓冲区）
 *
 * @note    适用于 CubeMX 生成的 HAL 工程。
 *          依赖 STM32F4xx HAL v1.24.0 及以上（HAL_UARTEx_ReceiveToIdle_DMA）。
 *
 *  ==================== CubeMX 配置要点 ====================
 *  USART1  Mode = Asynchronous, 115200 8N1, 无校验/无流控
 *          引脚 PA9 = TX, PA10 = RX
 *          NVIC 中必须使能 USART1 global interrupt（IDLE 依赖）
 *
 *  DMA Settings:
 *    USART1_RX -> DMA2 Stream5 / Channel4 / Circular / Byte->Byte / Memory 自增
 *    USART1_TX -> DMA2 Stream7 / Channel4 / Normal   / Byte->Byte / Memory 自增
 *    NVIC 中使能 DMA2 Stream5、DMA2 Stream7 global interrupt
 *
 *  ==================== 使用顺序 ====================
 *      HAL_Init();
 *      SystemClock_Config();
 *      MX_GPIO_Init();
 *      MX_DMA_Init();              // 必须先于 USART
 *      MX_USART1_UART_Init();
 *      uart_init();                // 启动 DMA + IDLE 接收
 *      ...
 *      uart_send(...); uart_read(...);
 */

#ifndef __UART_H
#define __UART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ======================== 可修改的配置 ======================== */

/* 使用的串口句柄与实例（换 USART2 时同步改这两个宏 + CubeMX 配置） */
#ifndef UART_HANDLE
#define UART_HANDLE             huart1
#endif

#ifndef UART_INSTANCE
#define UART_INSTANCE           USART1
#endif

/* DMA 搬运缓冲区长度，必须为 2 的幂（内部用位与取模处理绕回） */
#ifndef UART_DMA_BUF_SIZE
#define UART_DMA_BUF_SIZE       64
#endif

/* 环形缓冲区大小，必须为 2 的幂 */
#ifndef UART_RING_BUF_SIZE
#define UART_RING_BUF_SIZE      256
#endif

/* 阻塞发送的超时时间 (ms) */
#ifndef UART_TX_TIMEOUT
#define UART_TX_TIMEOUT         100
#endif

/* ======================== 数据结构 ======================== */

/**
 * @brief 环形缓冲区（单生产者=中断，单消费者=主循环，无锁）
 */
typedef struct {
    uint8_t  buf[UART_RING_BUF_SIZE];
    volatile uint16_t head;   /* 写指针（中断侧） */
    volatile uint16_t tail;   /* 读指针（主循环侧） */
} UART_RingBuf_t;

/**
 * @brief 发送模式
 */
typedef enum {
    UART_MODE_DMA   = 0,   /**< DMA 发送，异步立即返回（buffer 必须保持有效） */
    UART_MODE_BLOCK = 1,   /**< 阻塞发送，返回即发完 */
    UART_MODE_IRQ   = 2    /**< 中断发送，异步立即返回 */
} UART_SendMode_t;

/* ======================== 对外 API ======================== */

/**
 * @brief 启动 DMA + IDLE 空闲中断接收
 * @note  必须在 MX_DMA_Init() / MX_USART1_UART_Init() 之后调用
 * @retval 0 成功, -1 启动失败（DMA 或串口句柄未就绪）
 */
int uart_init(void);

/**
 * @brief 发送数据
 * @param data 数据指针
 * @param len  长度（字节）
 * @param mode 发送模式
 * @retval 0 成功, -1 参数错误, -2 发送忙, -3 阻塞模式超时, -4 启动失败
 */
int uart_send(const uint8_t *data, uint16_t len, UART_SendMode_t mode);

/**
 * @brief 非阻塞读取环形缓冲区
 * @retval 实际读取字节数（0 表示暂无数据）
 */
uint16_t uart_read(uint8_t *buf, uint16_t size);

/**
 * @brief 带超时读取（基于 HAL_GetTick，需要 SysTick 在跑）
 * @param timeout 超时毫秒数，0 表示非阻塞
 * @retval 实际读取字节数（0 表示超时）
 */
uint16_t uart_read_timeout(uint8_t *buf, uint16_t size, uint32_t timeout);

/** @brief 环形缓冲区当前可读字节数 */
uint16_t uart_available(void);

/** @brief 清空环形缓冲区 */
void uart_flush_rx(void);

/** @brief DMA/中断发送是否仍在进行 */
uint8_t uart_tx_busy(void);

/** @brief 接收链路是否已就绪 */
uint8_t uart_rx_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* __UART_H */
