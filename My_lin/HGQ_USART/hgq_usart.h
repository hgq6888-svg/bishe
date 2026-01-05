/******************************************************************************
 * @file     hgq_usart.h
 * @brief    STM32F407x 串口驱动模块 - 黄光全(HGQ)
 * @author   黄光全 (Huang Guangquan)
 * @date     2025-12-26
 * @version  v1.0.0
 * @note     文件名前缀说明：HGQ_ 表示由黄光全开发维护
 *           本模块提供USART1/USART2的初始化和数据收发功能
 * 
 * 模块功能概述：
 * 1. USART1：调试串口，用于PC通信和调试信息输出
 * 2. USART2：外设串口，用于连接ESP8266等通信模块
 * 3. 支持字符和字符串的发送和接收
 * 4. 支持带超时的接收功能，适合AT命令通信
 * 5. 提供非阻塞的数据可用性检查
 * 
 * 典型使用流程：
 * 1. 调用HGQ_USART1_Init(115200)初始化调试串口
 * 2. 调用HGQ_USART2_Init(115200)初始化ESP8266串口
 * 3. 使用SendString发送调试信息
 * 4. 使用ReceiveString接收AT命令响应
 * 
 * @attention
 * 个人项目文件 - 仅供学习使用
 * 作者：黄光全
 * 创建时间：2025年12月26日
 ******************************************************************************/

#ifndef __HGQ_USART_H
#define __HGQ_USART_H

/*-------------------------------------
 *            头文件包含
 *-------------------------------------*/
#include "stm32f4xx.h"      /**< STM32F4系列标准外设库头文件 */
#include <stdio.h>          /**< 标准输入输出，用于printf等函数 */

/*-------------------------------------
 *          USART1数据收发函数声明
 *-------------------------------------*/

/**
 * @brief   USART1串口初始化函数（调试串口）
 * @param   bound: 波特率（如115200、9600等）
 * @note    引脚配置：PA9=TX（发送），PA10=RX（接收）
 *          应用场景：连接PC进行调试通信
 *          时钟总线：USART1挂载在APB2总线
 * @retval  None
 */
void HGQ_USART1_Init(uint32_t bound);

/**
 * @brief   通过USART1发送单个字符
 * @param   ch: 要发送的字符（8位数据）
 * @note    使用查询方式发送，等待发送完成标志
 *          阻塞函数，发送完成前不会返回
 * @retval  None
 */
void HGQ_USART1_SendChar(uint8_t ch);

/**
 * @brief   通过USART1发送字符串
 * @param   str: 要发送的字符串（以'\0'结尾）
 * @note    循环调用SendChar发送每个字符
 *          自动处理字符串结束符
 * @retval  None
 */
void HGQ_USART1_SendString(char *str);

/**
 * @brief   通过USART1接收单个字符
 * @param   None
 * @note    使用查询方式接收，等待接收完成标志
 *          阻塞函数，接收到数据前不会返回
 * @retval  接收到的字符（8位数据）
 */
uint8_t HGQ_USART1_ReceiveChar(void);

/**
 * @brief   通过USART1接收字符串（带超时功能）
 * @param   buf: 接收缓冲区指针
 * @param   len: 缓冲区最大长度
 * @param   timeout: 超时时间（单位：毫秒）
 * @note    接收到指定长度或遇到换行符或超时后停止接收
 *          自动在字符串末尾添加'\0'
 * @retval  实际接收到的字符数
 */
uint16_t HGQ_USART1_ReceiveString(char *buf, uint16_t len, uint32_t timeout);

/**
 * @brief   检查USART1是否有数据可接收
 * @param   None
 * @note    非阻塞函数，立即返回
 * @retval  1: 有数据可接收，0: 无数据
 */
uint8_t HGQ_USART1_Available(void);

/*-------------------------------------
 *          USART2数据收发函数声明
 *-------------------------------------*/

/**
 * @brief   USART2串口初始化函数（外设串口）
 * @param   bound: 波特率（如115200、9600等）
 * @note    引脚配置：PA2=TX（发送），PA3=RX（接收）
 *          应用场景：连接ESP8266等外部通信模块
 *          时钟总线：USART2挂载在APB1总线
 * @retval  None
 */
void HGQ_USART2_Init(uint32_t bound);

/**
 * @brief   通过USART2发送单个字符
 * @param   ch: 要发送的字符（8位数据）
 * @note    使用查询方式发送，等待发送完成标志
 *          阻塞函数，发送完成前不会返回
 * @retval  None
 */
void HGQ_USART2_SendChar(uint8_t ch);

/**
 * @brief   通过USART2发送字符串
 * @param   str: 要发送的字符串（以'\0'结尾）
 * @note    循环调用SendChar发送每个字符
 *          自动处理字符串结束符
 * @retval  None
 */
void HGQ_USART2_SendString(char *str);

/**
 * @brief   通过USART2接收单个字符
 * @param   None
 * @note    使用查询方式接收，等待接收完成标志
 *          阻塞函数，接收到数据前不会返回
 * @retval  接收到的字符（8位数据）
 */
uint8_t HGQ_USART2_ReceiveChar(void);

/**
 * @brief   通过USART2接收字符串（带超时功能）
 * @param   buf: 接收缓冲区指针
 * @param   len: 缓冲区最大长度
 * @param   timeout: 超时时间（单位：毫秒）
 * @note    接收到指定长度或遇到换行符或超时后停止接收
 *          自动在字符串末尾添加'\0'
 * @retval  实际接收到的字符数
 */
uint16_t HGQ_USART2_ReceiveString(char *buf, uint16_t len, uint32_t timeout);
uint16_t HGQ_USART2_ReceiveString_wifi(char *buf, uint16_t len, uint32_t timeout_ms);

/**
 * @brief   检查USART2是否有数据可接收
 * @param   None
 * @note    非阻塞函数，立即返回
 * @retval  1: 有数据可接收，0: 无数据
 */
uint8_t HGQ_USART2_Available(void);



/*==========================================================================
 *               新增：USART 中断接收（可选）接口
 *==========================================================================
 * 说明：
 * 1) 以下接口是在原有“查询接收”基础上新增的“中断触发接收”方案。
 * 2) 不会改变你现有的函数/逻辑；你可以按需选择：
 *      - 继续使用 HGQ_USARTx_ReceiveChar / ReceiveString（查询方式）
 *      - 或者启用中断接收，使用 HGQ_USARTx_EnableRxIRQ 以及 HGQ_USARTx_IT_* 系列接口
 * 3) 使用中断接收时，RXNE 中断会把收到的字节放进环形缓冲区（RingBuffer）。
 *    主循环可以随时读取，无需一直轮询 RXNE 标志位，提高实时性。
 *
 * 启用方式（两种任选其一）：
 * A. 你工程里没有 USARTx_IRQHandler：
 *      - 在工程宏定义里增加：HGQ_USART_IRQ_HANDLER_IN_DRIVER
 *      - 然后调用：HGQ_USARTx_EnableRxIRQ(ENABLE)
 * B. 你工程里已经有 USARTx_IRQHandler：
 *      - 不要定义 HGQ_USART_IRQ_HANDLER_IN_DRIVER
 *      - 在你自己的 USARTx_IRQHandler() 里调用 HGQ_USARTx_RxIRQHandler()
 */

/* 环形缓冲区大小（可在工程宏里重定义） */
#ifndef HGQ_USART1_RXBUF_SIZE
#define HGQ_USART1_RXBUF_SIZE   256
#endif

#ifndef HGQ_USART2_RXBUF_SIZE
#define HGQ_USART2_RXBUF_SIZE   512
#endif

/* 接收回调：每收到 1 字节就会在中断里调用（可选，用于“边收边处理”） */
typedef void (*HGQ_USART_RxCallback)(uint8_t ch);

void HGQ_USART1_EnableRxIRQ(FunctionalState en);
void HGQ_USART2_EnableRxIRQ(FunctionalState en);

uint16_t HGQ_USART1_IT_AvailableBytes(void);
uint16_t HGQ_USART2_IT_AvailableBytes(void);

int HGQ_USART1_IT_GetChar(uint8_t *ch);
int HGQ_USART2_IT_GetChar(uint8_t *ch);

uint16_t HGQ_USART1_IT_Read(uint8_t *buf, uint16_t len);
uint16_t HGQ_USART2_IT_Read(uint8_t *buf, uint16_t len);

void HGQ_USART1_IT_ClearRxBuffer(void);
void HGQ_USART2_IT_ClearRxBuffer(void);

void HGQ_USART1_SetRxCallback(HGQ_USART_RxCallback cb);
void HGQ_USART2_SetRxCallback(HGQ_USART_RxCallback cb);

void HGQ_USART1_RxIRQHandler(void);
void HGQ_USART2_RxIRQHandler(void);

#ifdef HGQ_USART_IRQ_HANDLER_IN_DRIVER
void USART1_IRQHandler(void);
void USART2_IRQHandler(void);
#endif

#endif /* __HGQ_USART_H */
