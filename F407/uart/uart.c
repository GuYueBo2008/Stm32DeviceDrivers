/**
 * @file    uart.c
 * @brief   STM32F407 UART 驱动实现（HAL + DMA + IDLE 空闲中断 + 环形缓冲区）
 *
 * 接收链路:
 *   USART1 RX --(DMA2_Stream5, Circular)--> s_dma_buf[64]
 *                                            |
 *              IDLE / 半满(HT) / 满(TC) 中断 → 按 DMA 计数器增量搬入环形缓冲区
 *                                            |
 *                                     uart_read() 从环形缓冲区取数据
 *
 * 发送链路:
 *   UART_MODE_DMA   -> HAL_UART_Transmit_DMA   (异步)
 *   UART_MODE_IRQ   -> HAL_UART_Transmit_IT    (异步)
 *   UART_MODE_BLOCK -> HAL_UART_Transmit       (阻塞)
 */

#include "uart.h"
#include <string.h>

/* CubeMX 在 usart.c 中定义的句柄 */
extern UART_HandleTypeDef huart1;

#if (UART_DMA_BUF_SIZE & (UART_DMA_BUF_SIZE - 1)) != 0
#error "UART_DMA_BUF_SIZE 必须是 2 的幂"
#endif

#if (UART_RING_BUF_SIZE & (UART_RING_BUF_SIZE - 1)) != 0
#error "UART_RING_BUF_SIZE 必须是 2 的幂"
#endif

#define RING_MASK   (UART_RING_BUF_SIZE - 1)
#define DMA_MASK    (UART_DMA_BUF_SIZE - 1)

/* ======================== 私有变量 ======================== */

/* DMA 循环接收缓冲区 */
static uint8_t  s_dma_buf[UART_DMA_BUF_SIZE];

/* 环形缓冲区 */
static UART_RingBuf_t s_ring;

/* 上一次已搬走的数据在 s_dma_buf 中的位置（处理循环 DMA 绕回） */
static volatile uint16_t s_last_pos = 0;

/* 发送忙标志 */
static volatile uint8_t  s_tx_busy = 0;

/* 接收链路就绪标志 */
static volatile uint8_t  s_rx_ready = 0;

/* ======================== 环形缓冲区内部操作 ======================== */

static inline uint16_t ring_free(void)
{
    return (uint16_t)((s_ring.tail - s_ring.head - 1) & RING_MASK);
}

uint16_t uart_available(void)
{
    return (uint16_t)((s_ring.head - s_ring.tail) & RING_MASK);
}

/**
 * @brief 写入环形缓冲区；空间不足时丢弃新数据（不会破坏已缓存数据）
 */
static void ring_write(const uint8_t *src, uint16_t len)
{
    uint16_t i;

    for (i = 0; i < len; i++) {
        if (ring_free() == 0) {
            break;      /* 溢出：丢弃剩余新数据 */
        }
        s_ring.buf[s_ring.head] = src[i];
        s_ring.head = (uint16_t)((s_ring.head + 1) & RING_MASK);
    }
}

/* ======================== 初始化 ======================== */

static int rx_start(void)
{
    s_last_pos = 0;
    s_rx_ready = 0;

    if (HAL_UARTEx_ReceiveToIdle_DMA(&UART_HANDLE, s_dma_buf,
                                     UART_DMA_BUF_SIZE) != HAL_OK) {
        return -1;
    }

    s_rx_ready = 1;
    return 0;
}

int uart_init(void)
{
    s_ring.head = 0;
    s_ring.tail = 0;
    memset(s_ring.buf, 0, sizeof(s_ring.buf));
    memset(s_dma_buf, 0, sizeof(s_dma_buf));
    s_tx_busy = 0;

    return rx_start();
}

uint8_t uart_rx_ready(void)
{
    return s_rx_ready;
}

/* ======================== 读取 ======================== */

uint16_t uart_read(uint8_t *buf, uint16_t size)
{
    uint16_t available;
    uint16_t to_read;
    uint16_t i;

    if (buf == NULL || size == 0) {
        return 0;
    }

    available = uart_available();
    to_read = (available < size) ? available : size;

    for (i = 0; i < to_read; i++) {
        buf[i] = s_ring.buf[s_ring.tail];
        s_ring.tail = (uint16_t)((s_ring.tail + 1) & RING_MASK);
    }
    return to_read;
}

uint16_t uart_read_timeout(uint8_t *buf, uint16_t size, uint32_t timeout)
{
    uint32_t start;
    uint16_t got;

    if (timeout == 0) {
        return uart_read(buf, size);
    }

    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout) {
        got = uart_read(buf, size);
        if (got > 0) {
            return got;
        }
    }
    return 0;
}

void uart_flush_rx(void)
{
    s_ring.tail = s_ring.head;
}

/* ======================== 发送 ======================== */

int uart_send(const uint8_t *data, uint16_t len, UART_SendMode_t mode)
{
    HAL_StatusTypeDef st;

    if (data == NULL || len == 0) {
        return -1;
    }

    switch (mode) {
    case UART_MODE_BLOCK:
        st = HAL_UART_Transmit(&UART_HANDLE, (uint8_t *)data, len,
                               UART_TX_TIMEOUT);
        return (st == HAL_OK) ? 0 : -3;

    case UART_MODE_DMA:
        if (s_tx_busy) {
            return -2;
        }
        s_tx_busy = 1;
        st = HAL_UART_Transmit_DMA(&UART_HANDLE, (uint8_t *)data, len);
        if (st != HAL_OK) {
            s_tx_busy = 0;
            return (st == HAL_BUSY) ? -2 : -4;
        }
        return 0;

    case UART_MODE_IRQ:
        if (s_tx_busy) {
            return -2;
        }
        s_tx_busy = 1;
        st = HAL_UART_Transmit_IT(&UART_HANDLE, (uint8_t *)data, len);
        if (st != HAL_OK) {
            s_tx_busy = 0;
            return (st == HAL_BUSY) ? -2 : -4;
        }
        return 0;

    default:
        return -1;
    }
}

uint8_t uart_tx_busy(void)
{
    return s_tx_busy;
}

/* ======================== HAL 回调 ======================== */

/**
 * @brief IDLE / 半满(HT) / 满(TC) 事件统一入口
 *
 * @note  HAL 给出的 Size 参数在循环 DMA 下不可靠（IDLE 后它会以整个缓冲区
 *        长度计算，导致重复搬运），因此这里改用 DMA 计数器自行推算增量。
 */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    uint16_t cur;

    (void)Size;

    if (huart->Instance != UART_INSTANCE || huart->hdmarx == NULL) {
        return;
    }

    /* 已写入位置 = 缓冲区长度 - DMA 剩余计数，掩码处理绕回 */
    cur = (uint16_t)((UART_DMA_BUF_SIZE -
                      __HAL_DMA_GET_COUNTER(huart->hdmarx)) & DMA_MASK);

    if (cur == s_last_pos) {
        return;
    }

    if (cur > s_last_pos) {
        ring_write(&s_dma_buf[s_last_pos], (uint16_t)(cur - s_last_pos));
    } else {
        /* 循环 DMA 绕回：先搬尾部，再搬头部 */
        ring_write(&s_dma_buf[s_last_pos],
                   (uint16_t)(UART_DMA_BUF_SIZE - s_last_pos));
        ring_write(&s_dma_buf[0], cur);
    }

    s_last_pos = cur;
}

/**
 * @brief 发送完成（DMA / 中断模式共用）
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UART_INSTANCE) {
        s_tx_busy = 0;
    }
}

/**
 * @brief 错误处理：溢出/噪声等异常后重启接收，避免接收永久停摆
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != UART_INSTANCE) {
        return;
    }

    s_tx_busy = 0;

    HAL_UART_AbortReceive(huart);
    (void)rx_start();
}
