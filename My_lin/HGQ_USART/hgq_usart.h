#ifndef __HGQ_USART_H
#define __HGQ_USART_H

#include "stm32f4xx.h"
#include <stdio.h>

/* 缓冲区大小定义：根据内存情况调整，建议 512 或 1024 */
#define HGQ_USART1_RXBUF_SIZE   256
#define HGQ_USART2_RXBUF_SIZE   1024  /* 接收 WIFI 大包，开大一点 */

/* 初始化 */
void HGQ_USART1_Init(uint32_t bound);
void HGQ_USART2_Init(uint32_t bound);

/* 基础发送 */
void HGQ_USART1_SendChar(uint8_t ch);
void HGQ_USART1_SendString(char *str);
void HGQ_USART2_SendChar(uint8_t ch);
void HGQ_USART2_SendString(char *str);

/* 环形缓冲区接收接口 (推荐) */
void HGQ_USART2_EnableRxIRQ(FunctionalState en);     /* 开启中断接收 */
int  HGQ_USART2_IT_GetChar(uint8_t *ch);             /* 从缓冲区取一个字节 (非阻塞) */
void HGQ_USART2_IT_ClearRxBuffer(void);              /* 清空缓冲区 */

/* 兼容旧代码的回调定义 (仅占位，实际逻辑已移入环形缓冲) */
typedef void (*HGQ_USART_RxCallback)(uint8_t ch);
void HGQ_USART2_SetRxCallback(HGQ_USART_RxCallback cb);

#endif
