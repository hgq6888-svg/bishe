#ifndef __HGQ_MYIIC3_H
#define __HGQ_MYIIC3_H

#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "delay.h"

/*
 * 第三路软件I2C驱动头文件（用于BH1750光照传感器）
 * 
 * 引脚配置：
 * SCL: PC0（时钟线）
 * SDA: PC1（数据线）
 * 
 * 通信参数：
 * 标准模式：100kHz
 * 快速模式：400kHz（需要调整延时）
 * 
 * 支持功能：
 * 1. 起始/停止信号
 * 2. 字节发送/接收
 * 3. 应答/非应答
 * 4. 超时检测
 */

/* ================= 第三路软件I2C引脚定义 ================= */
#define HGQ_IIC3_GPIO_RCC    RCC_AHB1Periph_GPIOC    /* GPIOC时钟 */
#define HGQ_IIC3_GPIO_PORT   GPIOC                   /* GPIOC端口 */
#define HGQ_IIC3_SCL_PIN     GPIO_Pin_0              /* SCL时钟线：PC0 */
#define HGQ_IIC3_SDA_PIN     GPIO_Pin_1              /* SDA数据线：PC1 */

/* 读取SDA引脚状态的宏 */
#define HGQ_IIC3_READ_SDA    ((HGQ_IIC3_GPIO_PORT->IDR & HGQ_IIC3_SDA_PIN) ? 1 : 0)

/* I2C引脚电平控制宏（使用标准库函数，兼容性好）*/
#define HGQ_IIC3_SCL_H()     GPIO_SetBits(HGQ_IIC3_GPIO_PORT, HGQ_IIC3_SCL_PIN)
#define HGQ_IIC3_SCL_L()     GPIO_ResetBits(HGQ_IIC3_GPIO_PORT, HGQ_IIC3_SCL_PIN)
#define HGQ_IIC3_SDA_H()     GPIO_SetBits(HGQ_IIC3_GPIO_PORT, HGQ_IIC3_SDA_PIN)
#define HGQ_IIC3_SDA_L()     GPIO_ResetBits(HGQ_IIC3_GPIO_PORT, HGQ_IIC3_SDA_PIN)

/* ================= API函数声明 ================= */

/**
 * @brief I2C总线初始化
 * @note 配置引脚为开漏输出，设置空闲状态
 */
void    HGQ_IIC3_Init(void);

/**
 * @brief 发送I2C起始信号
 * @note SCL高电平时，SDA从高到低跳变
 */
void    HGQ_IIC3_Start(void);

/**
 * @brief 发送I2C停止信号
 * @note SCL高电平时，SDA从低到高跳变
 */
void    HGQ_IIC3_Stop(void);

/**
 * @brief 等待从机应答信号
 * @retval 0: 收到应答，1: 无应答或超时
 */
uint8_t HGQ_IIC3_WaitAck(void);

/**
 * @brief 主机发送应答信号（ACK）
 * @note 表示继续接收数据
 */
void    HGQ_IIC3_Ack(void);

/**
 * @brief 主机发送非应答信号（NACK）
 * @note 表示停止接收数据
 */
void    HGQ_IIC3_NAck(void);

/**
 * @brief 发送一个字节数据
 * @param data: 要发送的字节
 */
void    HGQ_IIC3_SendByte(uint8_t data);

/**
 * @brief 接收一个字节数据
 * @param ack: 是否发送应答
 *              1: 发送ACK（继续接收）
 *              0: 发送NACK（停止接收）
 * @retval 接收到的字节数据
 */
uint8_t HGQ_IIC3_ReadByte(uint8_t ack);

#endif /* __HGQ_MYIIC3_H */
