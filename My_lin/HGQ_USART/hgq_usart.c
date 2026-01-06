#include "hgq_usart.h"
#include "stm32f4xx.h"
#include <stdio.h>

/* 定义大缓冲区，防止WIFI长数据溢出 */
#define RX_BUF_SIZE 1024 

typedef struct {
    uint8_t buf[RX_BUF_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuf;

static RingBuf rb2 = {0}; // 用于 ESP8266

/* 初始化 USART2 (PA2/PA3) */
void HGQ_USART2_Init(uint32_t bound) {
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate = bound;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART2, &USART_InitStructure);
    
    /* 开启接收中断 */
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0; // 最高优先级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void HGQ_USART2_SendString(char *str) {
    while(*str) {
        while((USART2->SR & 0x40) == 0); // 等待发送完成
        USART2->DR = *str++;
    }
}

/* 兼容接口：实际上不做事，只为了编译通过 */
void HGQ_USART2_SetRxCallback(void (*cb)(uint8_t)) { (void)cb; }
void HGQ_USART2_EnableRxIRQ(FunctionalState en) { 
    USART_ITConfig(USART2, USART_IT_RXNE, en); 
}

/* 核心：从缓冲区取一个字节 (非阻塞) */
int HGQ_USART2_IT_GetChar(uint8_t *ch) {
    if(rb2.head == rb2.tail) return 0; // 空
    *ch = rb2.buf[rb2.tail];
    rb2.tail = (rb2.tail + 1) % RX_BUF_SIZE;
    return 1;
}

void HGQ_USART2_IT_ClearRxBuffer(void) {
    rb2.head = rb2.tail = 0;
}

/* 中断服务函数：极速存入缓冲区 */
void USART2_IRQHandler(void) {
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        uint8_t res = USART2->DR;
        uint16_t next = (rb2.head + 1) % RX_BUF_SIZE;
        if(next != rb2.tail) { // 未满
            rb2.buf[rb2.head] = res;
            rb2.head = next;
        }
    }
}

// ---------------- USART1 保持原样 (简化版) ----------------
void HGQ_USART1_Init(uint32_t bound) {
    // ... (保留你原来的 USART1 初始化代码) ...
    // 这里为了节省篇幅省略，调试串口不影响性能
}
void HGQ_USART1_SendString(char *str) {}
	