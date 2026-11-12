# STM32F407 USART1 CubeMX 配置步骤（HAL + DMA + IDLE + 环形缓冲）

适配 `F407/uart/` 目录下的 uart.c / uart.h 驱动。

- CubeMX 版本：6.x
- HAL 版本要求：STM32F4xx HAL **v1.24.0 及以上**（`HAL_UARTEx_ReceiveToIdle_DMA` 依赖）
- 硬件连接：`PA9 = USART1_TX`、`PA10 = USART1_RX`、必须共地
- 串口参数：115200 / 8N1 / 无流控

---

## 步骤 1：新建工程，选择芯片

1. `File → New Project`
2. `MCU/MPU Selector` 搜索 `STM32F407`，选你板子上的具体型号（如 `STM32F407ZGTx`）
3. 点 `Start Project`

---

## 步骤 2：配置时钟（RCC + Clock Configuration）

**RCC**

1. 左侧 `System Core → RCC`
2. `High Speed Clock (HSE)` 选 **Crystal/Ceramic Resonator**

**Clock Configuration**

1. `Input frequency` 填板载晶振频率（常见 **8 MHz**）
2. `PLL Source Mux` 选 **HSE**
3. `PLLM` = 8、`PLLN` = 336、`PLLP` = 2、`PLLQ` = 7
4. `System Clock Mux` 选 **PLLCLK**
5. `HCLK (MHz)` 填 **168** 后回车，让工具自动算分频
6. 确认 `APB1 = 42MHz`、`APB2 = 84MHz`

> 时钟不对会导致波特率偏差、串口乱码。若板子是 25MHz 晶振，把 PLLM 改成 25、PLLN 改成 336 左右让 VCO 落回 336MHz。

---

## 步骤 3：配置 USART1

1. 左侧 `Connectivity → USART1`
2. `Mode` 选 **Asynchronous**
3. `Hardware Flow Control` 保持 **Disable**
4. 下方 `Parameter Settings` 按表填写：

| 参数 | 值 |
|------|-----|
| Baud Rate | **115200** Bits/s |
| Word Length | **8 Bits (including Parity)** |
| Parity | **None** |
| Stop Bits | **1** |
| Data Direction | Receive and Transmit |
| Over Sampling | **16 Samples** |
| Advanced Features → Auto Baudrate | Disable |
| Advanced Features → TX/RX Pin Swap | Disable |

5. 回到 `Pinout view`，确认 **PA9 = USART1_TX**、**PA10 = USART1_RX** 已自动标绿

---

## 步骤 4：配置 DMA（关键）

仍然在 `USART1` 配置页，切到 **DMA Settings** 标签，点 `Add` 加两条：

### 4.1 接收通道

| 参数 | 值 |
|------|-----|
| DMA Request | **USART1_RX** |
| Stream | **DMA2 Stream 5** |
| Channel | **Channel 4** |
| Direction | **Peripheral To Memory** |
| Priority | **High** |
| Mode | **Circular** ← 必须循环 |
| Increment Address | Peripheral **不勾**、Memory **勾选** |
| Data Width | Peripheral **Byte**、Memory **Byte** |
| Use Fifo | 不勾 |

### 4.2 发送通道

| 参数 | 值 |
|------|-----|
| DMA Request | **USART1_TX** |
| Stream | **DMA2 Stream 7** |
| Channel | **Channel 4** |
| Direction | **Memory To Peripheral** |
| Priority | **Medium** |
| Mode | **Normal** |
| Increment Address | Peripheral **不勾**、Memory **勾选** |
| Data Width | Peripheral **Byte**、Memory **Byte** |

> F407 上 USART1 固定映射为 `RX = DMA2_Stream5_CH4`、`TX = DMA2_Stream7_CH4`，若被改成别的 Stream，驱动收不到数据。

---

## 步骤 5：配置 NVIC 中断

1. 左侧 `System Core → NVIC`
2. `NVIC` 标签页顶部 `Priority Group` 选 **2 bits for pre-emption priority / 2 bits for subpriority**
3. 按下表勾选并使能优先级：

| 中断 | Enable | Preemption Priority | Sub Priority |
|------|--------|--------------------|--------------|
| USART1 global interrupt | ☑ | 2 | 0 |
| DMA2 stream5 global interrupt | ☑ | 2 | 1 |
| DMA2 stream7 global interrupt | ☑ | 1 | 0 |

4. 切到 `Code generation` 标签，确认这三个中断都保持：

   - ☑ **Generate IRQ handler**
   - ☑ **Call HAL handler**

   > `Call HAL handler` 一定不能取消，否则 `HAL_UART_IRQHandler()` / DMA 回调不会被调用，IDLE 空闲中断形同虚设。

---

## 步骤 6：工程与代码生成设置

1. 左侧 `Project Manager → Project`
   - `Toolchain/IDE`：按实际选 `MDK-ARM` / `STM32CubeIDE` / `Makefile`
2. `Project Manager → Code Generator`
   - ☑ `Generate peripheral initialization as a pair of .c/.h files per peripheral`
   - ☑ `Keep User Code when re-generating`
   - ☑ `Delete previously generated files when not re-generated`
3. `Project Manager → Advanced Settings`
   - 确认 `USART1` 的 `Driver` 列是 **HAL**（不要选 LL）
4. 点右上角 `GENERATE CODE`

生成后的目录结构（关键文件）：

```
Core/Inc/main.h          Core/Src/main.c
Core/Inc/usart.h         Core/Src/usart.c        ← 定义 huart1
Core/Inc/dma.h           Core/Src/dma.c          ← MX_DMA_Init()
Core/Src/stm32f4xx_hal_msp.c                     ← huart1.hdmarx / hdmatx 绑定
Core/Src/stm32f4xx_it.c                          ← USART1_IRQHandler / DMA2_StreamX_IRQHandler
```

---

## 步骤 7：把驱动加入工程

1. 复制 `F407/uart/uart.c` → `Core/Src/`
2. 复制 `F407/uart/uart.h` → `Core/Inc/`
3. 若工程用 Keil/IAR，记得把 `uart.c` 手动 `Add Existing Files` 到工程分组里
4. 确认 `usart.c` 中的句柄名是 **`huart1`**（uart.c 里 `extern UART_HandleTypeDef huart1;`）
   - 若用 USART2/3，需同步改 uart.h 的 `UART_HANDLE` / `UART_INSTANCE` 宏和 uart.c 的 extern 句柄名

---

## 步骤 8：修改 main.c

CubeMX 生成代码里只需在 `USER CODE` 区段插入，**不要动生成代码本身**。

```c
/* USER CODE BEGIN Includes */
#include "uart.h"
#include <string.h>
/* USER CODE END Includes */
```

```c
int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();              /* CubeMX 会自动排在 USART 之前 */
  MX_USART1_UART_Init();

  /* USER CODE BEGIN 2 */
  uint8_t  rx[128];           /* 变量声明放在函数开头，兼容 C89 工具链 */
  uint16_t n;

  if (uart_init() != 0) {     /* 启动 DMA + IDLE 接收 */
    Error_Handler();
  }
  uart_send((const uint8_t *)"UART Ready\r\n", 12, UART_MODE_BLOCK);
  /* USER CODE END 2 */

  while (1)
  {
    /* USER CODE BEGIN 3 */
    n = uart_read_timeout(rx, sizeof(rx), 50);
    if (n > 0) {
      uart_send(rx, n, UART_MODE_BLOCK);   /* 回显 */
    }
    /* USER CODE END 3 */
  }
}
```

> 注意：`uart_init()` 必须在 `MX_USART1_UART_Init()` 之后调用，因为它要使用已配置好的 `huart1` 和 `huart1.hdmarx`。

---

## 步骤 9：验证

| 检查项 | 方法 | 期望 |
|--------|------|------|
| 接收链路就绪 | 调 `uart_rx_ready()` | 返回 1 |
| 能收数据 | 串口助手发任意字符 | `uart_available()` 递增 |
| 能回显 | 串口助手 115200 8N1 发送 | 原样返回 |
| 中断是否进 | 在 `HAL_UARTEx_RxEventCallback` 打断点 | 发数据时命中 |
| 发送是否完成 | `uart_tx_busy()` | DMA 发完变 0 |

串口助手设置：**115200 / 8 / None / 1 / 无流控**，接收建议开 **HEX 显示**排查乱码。

---

## 常见问题排查

| 现象 | 原因 | 处理 |
|------|------|------|
| `uart_init()` 返回 -1 | DMA 未配置 / `MX_DMA_Init()` 顺序错 / `huart1.hdmarx` 为空 | 检查 DMA Settings，确认 DMA 初始化在 USART 之前 |
| 完全收不到数据 | USART1 全局中断没开，或 `Call HAL handler` 没勾 | 步骤 5 重新确认 |
| 只收到第一帧 | RX DMA 模式配成了 Normal | 改成 **Circular** |
| 收到的数据有重复片段 | 用了 HAL 回调里的 `Size` 参数 | 本驱动已改用 DMA 计数器自行计算，无需处理 |
| 收到数据但内容错位 | DMA 的 Memory Increment 没勾 / Data Width 不是 Byte | 重配 DMA |
| 发送卡在该处 | 阻塞模式超时（`-3`），多为时钟/波特率不对或 TX DMA 冲突 | 检查 HSE 是否起振、波特率是否匹配 |
| 全是乱码 | 时钟树配错（HSE 频率填错）或波特率不一致 | 重算 PLL，确认串口助手同参数 |
| `HAL_UARTEx_ReceiveToIdle_DMA` 未定义 | HAL 版本过低（< 1.24.0） | 升级 STM32F4 HAL 包，或在 CubeMX 里更新固件包版本 |
| 编译报 `huart1` 未定义 | 没包含 `usart.h` | 在 uart.h 前 `#include "usart.h"` 或把 uart.c 的 extern 改为引入 usart.h |

---

## 参数速查表（一次性对照）

```
USART1      Asynchronous / 115200 / 8N1 / 无流控 / 16 倍采样
TX 引脚     PA9      RX 引脚 PA10
DMA RX      DMA2 Stream5 Channel4 Circular  Byte→Byte  Memory++
DMA TX      DMA2 Stream7 Channel4 Normal    Byte→Byte  Memory++
NVIC 分组   2 位抢占 / 2 位子优先级
NVIC 优先级 TX=1:0   USART1=2:0   RX=2:1
缓冲区      DMA 搬运用 64 字节, 环形缓冲区 256 字节（都需 2 的幂）
```
