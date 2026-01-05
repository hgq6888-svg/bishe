#ifndef __HGQ_MYIIC2_H
#define __HGQ_MYIIC2_H

#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "delay.h"

/*
 * 第二路软件I2C驱动头文件（用于AHT20温湿度传感器）
 * 
 * 引脚配置：
 * SCL: PB6（时钟线）
 * SDA: PB7（数据线）
 * 
 * 与第三路I2C驱动功能相同，但使用不同引脚
 * 允许系统中同时使用多个I2C设备
 */

/* ================= 第二路软件I2C引脚定义 ================= */
#define HGQ_IIC2_GPIO_RCC    RCC_AHB1Periph_GPIOB    /* GPIOB时钟 */
#define HGQ_IIC2_GPIO_PORT   GPIOB                   /* GPIOB端口 */
#define HGQ_IIC2_SCL_PIN     GPIO_Pin_6              /* SCL时钟线：PB6 */
#define HGQ_IIC2_SDA_PIN     GPIO_Pin_7              /* SDA数据线：PB7 */

/* 读取SDA引脚状态的宏 */
#define HGQ_IIC2_READ_SDA    ((HGQ_IIC2_GPIO_PORT->IDR & HGQ_IIC2_SDA_PIN) ? 1 : 0)

/* I2C引脚电平控制宏（使用标准库函数，兼容性好）*/
#define HGQ_IIC2_SCL_H()     GPIO_SetBits(HGQ_IIC2_GPIO_PORT, HGQ_IIC2_SCL_PIN)
#define HGQ_IIC2_SCL_L()     GPIO_ResetBits(HGQ_IIC2_GPIO_PORT, HGQ_IIC2_SCL_PIN)
#define HGQ_IIC2_SDA_H()     GPIO_SetBits(HGQ_IIC2_GPIO_PORT, HGQ_IIC2_SDA_PIN)
#define HGQ_IIC2_SDA_L()     GPIO_ResetBits(HGQ_IIC2_GPIO_PORT, HGQ_IIC2_SDA_PIN)

/* ================= API函数声明 ================= */

/**
 * @brief I2C总线初始化
 * @note 配置引脚为开漏输出，设置空闲状态
 */
void    HGQ_IIC2_Init(void);

/**
 * @brief 发送I2C起始信号
 */
void    HGQ_IIC2_Start(void);

/**
 * @brief 发送I2C停止信号
 */
void    HGQ_IIC2_Stop(void);

/**
 * @brief 等待从机应答信号
 * @retval 0: 收到应答，1: 无应答或超时
 */
uint8_t HGQ_IIC2_WaitAck(void);

/**
 * @brief 主机发送应答信号（ACK）
 */
void    HGQ_IIC2_Ack(void);

/**
 * @brief 主机发送非应答信号（NACK）
 */
void    HGQ_IIC2_NAck(void);

/**
 * @brief 发送一个字节数据
 * @param data: 要发送的字节
 */
void    HGQ_IIC2_SendByte(uint8_t data);

/**
 * @brief 接收一个字节数据
 * @param ack: 是否发送应答
 * @retval 接收到的字节数据
 */
uint8_t HGQ_IIC2_ReadByte(uint8_t ack);

#endif /* __HGQ_MYIIC2_H */
