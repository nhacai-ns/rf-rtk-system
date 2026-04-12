#include "uart_dma.h"

UART_HandleTypeDef* huart2;
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

// --- BIẾN CHO RECEIVE (RX) ---
static uint8_t rx_buffer[UART_RX_BUFFER_SIZE];
static volatile uint16_t old_pos = 0;
volatile bool uart_idle_flag = false;
volatile uint16_t uart_rx_len = 0;
static uint16_t last_size = 0;

// --- BIẾN CHO TRANSMIT (TX) ---
static uint8_t tx_storage[TX_STORAGE_SIZE];      // Ring buffer lưu trữ
static uint8_t dma_active_buffer[1024]; // Buffer cho DMA
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

void UART_DMA_Init()
{
    Serial.begin(115200);
    huart2 = Serial.getHandle();

    __HAL_UART_DISABLE_IT(huart2, UART_IT_RXNE | UART_IT_TC | UART_IT_TXE | UART_IT_PE | UART_IT_ERR);

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();

    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3; // TX/RX
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    huart2->Instance = USART2;
    huart2->Init.BaudRate = 115200;
    huart2->Init.WordLength = UART_WORDLENGTH_8B;
    huart2->Init.StopBits = UART_STOPBITS_1;
    huart2->Init.Parity = UART_PARITY_NONE;
    huart2->Init.Mode = UART_MODE_TX_RX;
    huart2->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2->Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(huart2);

    hdma_usart2_rx.Instance = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK) {
        // Error Handler
    }   
    __HAL_LINKDMA(huart2, hdmarx, hdma_usart2_rx);

    hdma_usart2_tx.Instance = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW;
    hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
        // Error Handler
    }
    __HAL_LINKDMA(huart2, hdmatx, hdma_usart2_tx);
    __HAL_DMA_DISABLE_IT(&hdma_usart2_tx, DMA_IT_HT); // Tắt ngắt 50% dữ liệu, chỉ để lại ngắt 100%
    __HAL_DMA_CLEAR_FLAG(&hdma_usart2_tx, DMA_FLAG_TCIF2_6 | DMA_FLAG_HTIF2_6 | DMA_FLAG_TEIF2_6);
    
    // Start DMA (IDLE mode)
    HAL_UARTEx_ReceiveToIdle_DMA(huart2, rx_buffer, UART_RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
}

// --- HÀM XỬ LÝ TX (GỬI ĐI) ---
void UART_Write_Queue(uint8_t *pData, uint16_t Size) {
    for (uint16_t i = 0; i < Size; i++) {
        uint16_t next = (head + 1) % TX_STORAGE_SIZE;
        if (next != tail) {
            tx_storage[head] = pData[i];
            head = next;
        }
    }
}

void UART_DMA_Process_TX() {
    // Chỉ bắt đầu gửi nếu DMA TX đang rảnh (không bật cờ EN)
    if (!(DMA1_Stream6->CR & DMA_SxCR_EN)) {
        // Xóa cờ hoàn tất cũ
        DMA1->HIFCR = DMA_HIFCR_CTCIF6 | DMA_HIFCR_CHTIF6;

        if (head != tail) {
            uint16_t count = 0;
            // Copy dữ liệu từ Ring Buffer sang Buffer DMA tạm
            while (head != tail && count < DMA_TX_TEMP_SIZE) {
                dma_active_buffer[count++] = tx_storage[tail];
                tail = (tail + 1) % TX_STORAGE_SIZE;
            }

            if (count > 0) {
                DMA1_Stream6->M0AR = (uint32_t)dma_active_buffer;
                DMA1_Stream6->PAR  = (uint32_t)&USART2->DR;
                DMA1_Stream6->NDTR = count;
                
                USART2->CR3 |= USART_CR3_DMAT;
                DMA1_Stream6->CR |= DMA_SxCR_EN;
                
                huart2->gState = HAL_UART_STATE_READY;
            }
        }
    }
}

// Đọc từ DMA circular buffer
uint16_t UART_Read(uint8_t *dest, uint16_t maxlen) {
    uint16_t pos = UART_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart2->hdmarx);
    uint16_t len = 0;
    while (old_pos != pos && len < maxlen) {
        dest[len++] = rx_buffer[old_pos++];
        if (old_pos >= UART_RX_BUFFER_SIZE) old_pos = 0;
    }
    return len;
}

#ifdef __cplusplus
extern "C" {
#endif

// void DMA1_Stream6_IRQHandler(void) {
//     HAL_DMA_IRQHandler(&hdma_usart2_tx);
// }

// Ngắt cho DMA RX (Nếu bạn dùng đồng thời)
void DMA1_Stream5_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART2) {
        uart_rx_len = Size > last_size ? Size - last_size : (UART_RX_BUFFER_SIZE - last_size) + Size;
        last_size = Size; 
        uart_idle_flag = true;
        
        HAL_UARTEx_ReceiveToIdle_DMA(huart, rx_buffer, UART_RX_BUFFER_SIZE);
        __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
    }
}

#ifdef __cplusplus
}
#endif
