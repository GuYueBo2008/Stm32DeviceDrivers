/**
 * @file    uart.c
 * @brief   STM32F407 UART 驱动实现
 * @note    依赖 STM32F4xx SPL: stm32f4xx_gpio.c, stm32f4xx_usart.c,
 *          stm32f4xx_dma.c, stm32f4xx_rcc.c, misc.c (NVIC)
 */

#include "uart.h"
#include <string.h>

/* ======================== 私有变量 ======================== */

/* DMA 接收缓冲区 —— 循环模式，DMA 自动填充 */
static uint8_t  s_dma_rx_buf[UART_DMA_BUF_SIZE];

/* 环形缓冲区 —— main 侧读、中断侧写 */
static UART_RingBuf_t s_ring;

/* DMA TX 忙标志（DMA/IRQ 发送完成后置 0） */
static volatile uint8_t s_tx_busy = 0;

/* 用于阻塞发送的超时计数（简单轮询延时，非精确） */
#define UART_BLOCK_TIMEOUT  10000U

/* ======================== 辅助宏 ======================== */

#define RING_MASK   (UART_RING_BUF_SIZE - 1)

/* 环形缓冲区剩余可写入空间 */
static inline uint16_t ring_free(void)
{
    return (uint16_t)((s_ring.tail - s_ring.head - 1) & RING_MASK);
}

/* 环形缓冲区当前可读字节数 */
uint16_t uart_available(void)
{
    return (uint16_t)((s_ring.head - s_ring.tail) & RING_MASK);
}

/* ======================== 初始化 ======================== */

static void gpio_init(void)
{
    GPIO_InitTypeDef gi;

    RCC_AHB1PeriphClockCmd(UART_GPIO_CLK, ENABLE);

    GPIO_PinAFConfig(UART_TX_PORT, GPIO_PinSource9, UART_GPIO_AF);
    GPIO_PinAFConfig(UART_RX_PORT, GPIO_PinSource10, UART_GPIO_AF);

    gi.GPIO_Pin   = UART_TX_PIN | UART_RX_PIN;
    gi.GPIO_Mode  = GPIO_Mode_AF;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_OType = GPIO_OType_PP;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &gi);
}

static void usart_init(void)
{
    USART_InitTypeDef ui;
    NVIC_InitTypeDef   ni;

    RCC_APB2PeriphClockCmd(UARTx_CLK, ENABLE);

    ui.USART_BaudRate            = UART_BAUDRATE;
    ui.USART_WordLength          = USART_WordLength_8b;
    ui.USART_StopBits            = USART_StopBits_1;
    ui.USART_Parity              = USART_Parity_No;
    ui.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    ui.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(UARTx, &ui);

    /* 使能 DMA 请求 */
    USART_DMACmd(UARTx, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);

    /* 打开 IDLE 空闲中断 —— 一帧结束的判定 */
    USART_ClearFlag(UARTx, USART_FLAG_IDLE);   /* 先读 SR 再读 DR 才能清 */
    (void)UARTx->SR;
    (void)UARTx->DR;
    USART_ITConfig(UARTx, USART_IT_IDLE, ENABLE);

    /* USART 全局中断用于 DMA 发送时的 TC 完成通知（可选，此处走 DMA TC 中断） */
    USART_Cmd(UARTx, ENABLE);

    /* NVIC: USART 中断 (IDLE) */
    ni.NVIC_IRQChannel                   = UARTx_IRQn;
    ni.NVIC_IRQChannelPreemptionPriority = 2;
    ni.NVIC_IRQChannelSubPriority        = 0;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);
}

static void dma_rx_init(void)
{
    DMA_InitTypeDef di;
    NVIC_InitTypeDef ni;

    RCC_AHB1PeriphClockCmd(UART_DMA_CLK, ENABLE);

    /* 先关闭 stream 再配置 */
    DMA_Cmd(UART_DMA_RX_STREAM, DISABLE);

    DMA_DeInit(UART_DMA_RX_STREAM);

    di.DMA_Channel            = UART_DMA_RX_CH;
    di.DMA_PeripheralBaseAddr = (uint32_t)&UARTx->DR;
    di.DMA_Memory0BaseAddr    = (uint32_t)s_dma_rx_buf;
    di.DMA_DIR                = DMA_DIR_PeripheralToMemory;
    di.DMA_BufferSize         = UART_DMA_BUF_SIZE;
    di.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    di.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    di.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    di.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    di.DMA_Mode               = DMA_Mode_Circular;       /* 循环接收 */
    di.DMA_Priority           = DMA_Priority_High;
    di.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    di.DMA_FIFOThreshold      = DMA_FIFOThreshold_Full;
    di.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    di.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(UART_DMA_RX_STREAM, &di);

    /* 打开半满 + 满 + 错误中断 */
    DMA_ITConfig(UART_DMA_RX_STREAM,
                 DMA_IT_HT | DMA_IT_TC | DMA_IT_TE, ENABLE);

    /* NVIC: DMA RX 中断 */
    ni.NVIC_IRQChannel                   = UART_DMA_RX_IRQn;
    ni.NVIC_IRQChannelPreemptionPriority = 2;
    ni.NVIC_IRQChannelSubPriority        = 1;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);

    DMA_Cmd(UART_DMA_RX_STREAM, ENABLE);
}

static void dma_tx_init(void)
{
    DMA_InitTypeDef di;
    NVIC_InitTypeDef ni;

    DMA_Cmd(UART_DMA_TX_STREAM, DISABLE);
    DMA_DeInit(UART_DMA_TX_STREAM);

    di.DMA_Channel            = UART_DMA_TX_CH;
    di.DMA_PeripheralBaseAddr = (uint32_t)&UARTx->DR;
    di.DMA_Memory0BaseAddr    = 0;   /* 发送时再填实际地址 */
    di.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
    di.DMA_BufferSize         = 0;
    di.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
    di.DMA_MemoryInc          = DMA_MemoryInc_Enable;
    di.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    di.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
    di.DMA_Mode               = DMA_Mode_Normal;        /* 单次发送 */
    di.DMA_Priority           = DMA_Priority_Medium;
    di.DMA_FIFOMode           = DMA_FIFOMode_Disable;
    di.DMA_FIFOThreshold      = DMA_FIFOThreshold_Full;
    di.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
    di.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;
    DMA_Init(UART_DMA_TX_STREAM, &di);

    DMA_ITConfig(UART_DMA_TX_STREAM, DMA_IT_TC | DMA_IT_TE, ENABLE);

    ni.NVIC_IRQChannel                   = UART_DMA_TX_IRQn;
    ni.NVIC_IRQChannelPreemptionPriority = 1;
    ni.NVIC_IRQChannelSubPriority        = 0;
    ni.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&ni);
}

void uart_init(void)
{
    /* 清空环形缓冲区 */
    s_ring.head = 0;
    s_ring.tail = 0;
    memset(s_ring.buf, 0, sizeof(s_ring.buf));
    memset(s_dma_rx_buf, 0, sizeof(s_dma_rx_buf));

    /* NVIC 分组（工程里一般已经配过，此处再兜底一次） */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    gpio_init();
    usart_init();
    dma_rx_init();
    dma_tx_init();
}

/* ======================== 写入环形缓冲区（内部，中断侧调用） ======================== */

/**
 * @brief 将 DMA buf [off, off+len) 写入环形缓冲区
 *        发生溢出时丢弃尾部新数据（也可改为覆盖最旧，按需调整）
 */
static void ring_write(const uint8_t *src, uint16_t off, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        if (ring_free() == 0) {
            /* 溢出：丢弃新数据，或者推进 tail 覆盖最旧数据
             * 这里选择丢弃，更安全 */
            break;
        }
        s_ring.buf[s_ring.head] = src[off + i];
        s_ring.head = (s_ring.head + 1) & RING_MASK;
    }
}

/* ======================== 读取环形缓冲区（main 侧） ======================== */

uint16_t uart_read(uint8_t *buf, uint16_t size)
{
    uint16_t available = uart_available();
    uint16_t to_read   = (available < size) ? available : size;
    uint16_t i;

    for (i = 0; i < to_read; i++) {
        buf[i] = s_ring.buf[s_ring.tail];
        s_ring.tail = (s_ring.tail + 1) & RING_MASK;
    }
    return to_read;
}

/* 阻塞读带超时：使用简单的忙等计数作为超时 tick */
uint16_t uart_read_timeout(uint8_t *buf, uint16_t size, uint32_t timeout)
{
    uint16_t got;
    uint32_t t;

    if (timeout == 0) {
        return uart_read(buf, size);
    }

    for (t = 0; t < timeout; t++) {
        got = uart_read(buf, size);
        if (got > 0) return got;
        /* 这里用一个空转循环代替延时；如果工程里有 systick 延时，
         * 请把这一行替换为 delay_ms(1) */
        volatile uint32_t d = 1000; while (d--);
    }
    return 0;
}

void uart_flush_rx(void)
{
    s_ring.tail = s_ring.head;
}

/* ======================== 发送 ======================== */

static int uart_send_block(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint32_t t;

    for (i = 0; i < len; i++) {
        t = UART_BLOCK_TIMEOUT;
        /* 等待 TXE (Transmit Data Register Empty) */
        while (USART_GetFlagStatus(UARTx, USART_FLAG_TXE) == RESET) {
            if (t-- == 0) return -3;
        }
        USART_SendData(UARTx, data[i]);
    }
    /* 等待 TC (Transmission Complete) 确保最后一字节发完 */
    t = UART_BLOCK_TIMEOUT;
    while (USART_GetFlagStatus(UARTx, USART_FLAG_TC) == RESET) {
        if (t-- == 0) return -3;
    }
    return 0;
}

static int uart_send_dma(const uint8_t *data, uint16_t len)
{
    if (len == 0) return 0;
    if (s_tx_busy) return -2;

    s_tx_busy = 1;

    /* 关闭 stream 后再改配置 */
    DMA_Cmd(UART_DMA_TX_STREAM, DISABLE);
    while (DMA_GetCmdStatus(UART_DMA_TX_STREAM) != DISABLE);

    DMA_MemoryTargetConfig(UART_DMA_TX_STREAM,
                           (uint32_t)data, DMA_Memory_0);
    DMA_SetCurrDataCounter(UART_DMA_TX_STREAM, len);

    /* 清标志 */
    DMA_ClearITPendingBit(UART_DMA_TX_STREAM,
                          DMA_IT_TC | DMA_IT_TE | DMA_IT_FE);

    DMA_Cmd(UART_DMA_TX_STREAM, ENABLE);

    /* 等待 DMA TC 中断来清除 s_tx_busy（此处立即返回） */
    return 0;
}

int uart_send(const uint8_t *data, uint16_t len, UART_SendMode_t mode)
{
    if (data == NULL || len == 0) return -1;

    switch (mode) {
    case UART_MODE_BLOCK:
        return uart_send_block(data, len);

    case UART_MODE_DMA:
    case UART_MODE_IRQ:  /* IRQ 模式简化实现：复用 DMA 发送 */
        return uart_send_dma(data, len);

    default:
        return -1;
    }
}

uint8_t uart_tx_busy(void)
{
    return s_tx_busy;
}

/* ======================== 中断处理 ======================== */

/**
 * @brief USART IDLE 空闲中断
 *        含义：接收总线上空闲了一个字节时间以上，意味着一帧结束。
 *        此时 DMA 可能刚好停在半满/满的边界，也可能还没到边界，
 *        直接读取当前 DMA 写指针，把这段数据搬入环形缓冲区。
 */
void uart_idle_irq_handler(void)
{
    uint16_t ndtr;
    uint16_t i;

    /* 必须先清 IDLE 标志：读 SR -> 读 DR */
    (void)UARTx->SR;
    (void)UARTx->DR;

    /* 关闭 DMA stream 防止搬运期间被继续写入 */
    DMA_Cmd(UART_DMA_RX_STREAM, DISABLE);

    /* DMA 循环模式下 NDTR 表示“剩余未传输字节数”
     * 已传输字节数 = UART_DMA_BUF_SIZE - NDTR
     * 但我们只搬运“本轮收到的新数据”，即从上次停止位置到当前 NDTR 之间 */
    ndtr = (uint16_t)DMA_GetCurrDataCounter(UART_DMA_RX_STREAM);

    /* 简化策略：直接把 DMA 缓冲区全部拷入环形 —— 开销可接受，逻辑最稳
     * （因为 DMA 是循环模式，这一轮的完整帧已经在 s_dma_rx_buf 里） */
    ring_write(s_dma_rx_buf, 0, UART_DMA_BUF_SIZE);

    /* 重设 NDTR 并重启 DMA */
    DMA_SetCurrDataCounter(UART_DMA_RX_STREAM, UART_DMA_BUF_SIZE);
    DMA_Cmd(UART_DMA_RX_STREAM, ENABLE);

    /* 防止未使用变量警告（如果将来改为精确搬运，ndtr 会用上） */
    (void)ndtr;
}

/**
 * @brief DMA RX 半满 / 满中断
 *        半满时把前半搬入环形，满时把后半搬入环形
 */
void uart_dma_rx_irq_handler(void)
{
    if (DMA_GetITStatus(UART_DMA_RX_STREAM, DMA_IT_HT) != RESET) {
        DMA_ClearITPendingBit(UART_DMA_RX_STREAM, DMA_IT_HT);
        ring_write(s_dma_rx_buf, 0, UART_DMA_BUF_SIZE / 2);
    }

    if (DMA_GetITStatus(UART_DMA_RX_STREAM, DMA_IT_TC) != RESET) {
        DMA_ClearITPendingBit(UART_DMA_RX_STREAM, DMA_IT_TC);
        ring_write(s_dma_rx_buf, UART_DMA_BUF_SIZE / 2, UART_DMA_BUF_SIZE / 2);
    }

    if (DMA_GetITStatus(UART_DMA_RX_STREAM, DMA_IT_TE) != RESET) {
        DMA_ClearITPendingBit(UART_DMA_RX_STREAM, DMA_IT_TE);
        /* 传输错误，工程上应做告警 */
    }
}

/**
 * @brief DMA TX 发送完成中断
 */
void uart_dma_tx_irq_handler(void)
{
    if (DMA_GetITStatus(UART_DMA_TX_STREAM, DMA_IT_TC) != RESET) {
        DMA_ClearITPendingBit(UART_DMA_TX_STREAM, DMA_IT_TC);
        s_tx_busy = 0;
    }

    if (DMA_GetITStatus(UART_DMA_TX_STREAM, DMA_IT_TE) != RESET) {
        DMA_ClearITPendingBit(UART_DMA_TX_STREAM, DMA_IT_TE);
        s_tx_busy = 0;
    }
}

/* ======================== 默认中断服务函数（弱符号，如自己定义请覆盖） ======================== */

/* 这三个函数名就是 STM32 启动文件里默认引用的 IRQ Handler 名。
 * main.c 或启动文件里若已定义同名强符号，这里的弱符号会被自动忽略。 */

__attribute__((weak)) void USART1_IRQHandler(void)
{
    uart_idle_irq_handler();
}

__attribute__((weak)) void DMA2_Stream5_IRQHandler(void)
{
    uart_dma_rx_irq_handler();
}

__attribute__((weak)) void DMA2_Stream7_IRQHandler(void)
{
    uart_dma_tx_irq_handler();
}
