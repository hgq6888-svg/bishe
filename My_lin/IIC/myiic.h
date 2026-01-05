#ifndef __MYIIC_H
#define __MYIIC_H
#include "sys.h" 
/******************************************************************************
 * @file     myiic.h
 * @brief    软件I2C驱动头文件（正点原子版本）
 * @author   正点原子@ALIENTEK
 * @date     2014/5/6
 * @version  V1.0
 * 
 * 重要说明：
 * 1. 本驱动使用直接寄存器操作，提高执行效率
 * 2. SDA引脚需要在输入和输出模式间切换
 * 3. 支持100kHz标准I2C时序
 * 4. 适用于AT24CXX等I2C设备通信
 ******************************************************************************/

/* ================= 引脚模式设置宏（直接寄存器操作）================= */

/**
 * @brief 设置SDA引脚为输入模式
 * @note 直接操作GPIOB的MODER寄存器
 *       MODER位[19:18]对应PB9的输入输出配置
 *       00: 输入模式，01: 输出模式
 */
#define SDA_IN()  { \
    GPIOB->MODER &= ~(3 << (9 * 2));	/* 清除PB9的模式位 */ \
    GPIOB->MODER |= 0 << (9 * 2);		/* 设置为输入模式（00）*/ \
}

/**
 * @brief 设置SDA引脚为输出模式
 * @note 直接操作GPIOB的MODER寄存器
 *       设置为通用输出模式（01）
 */
#define SDA_OUT() { \
    GPIOB->MODER &= ~(3 << (9 * 2));	/* 清除PB9的模式位 */ \
    GPIOB->MODER |= 1 << (9 * 2);		/* 设置为输出模式（01）*/ \
}

/* ================= 引脚操作宏（直接寄存器操作）================= */

/**
 * @brief SCL时钟线输出
 * @note PB8引脚输出控制
 */
#define IIC_SCL    PBout(8)	// SCL时钟线

/**
 * @brief SDA数据线输出
 * @note PB9引脚输出控制
 */
#define IIC_SDA    PBout(9)	// SDA数据线

/**
 * @brief 读取SDA数据线输入
 * @note PB9引脚输入读取
 */
#define READ_SDA   PBin(9)	// 输入SDA

/* ================= I2C操作函数声明 ================= */

/**
 * @brief 初始化I2C总线
 * @note 配置引脚为推挽输出，设置初始状态
 */
void IIC_Init(void);

/**
 * @brief 发送I2C起始信号
 * @note SCL高电平时，SDA从高变低
 */
void IIC_Start(void);

/**
 * @brief 发送I2C停止信号
 * @note SCL高电平时，SDA从低变高
 */
void IIC_Stop(void);

/**
 * @brief 发送一个字节数据
 * @param txd: 要发送的字节
 */
void IIC_Send_Byte(u8 txd);

/**
 * @brief 读取一个字节数据
 * @param ack: 是否发送应答
 *              1: 发送ACK，0: 发送NACK
 * @retval 接收到的字节数据
 */
u8 IIC_Read_Byte(unsigned char ack);

/**
 * @brief 等待从机应答信号
 * @retval 0: 收到应答，1: 无应答或超时
 */
u8 IIC_Wait_Ack(void);

/**
 * @brief 发送ACK应答信号
 * @note 表示继续接收数据
 */
void IIC_Ack(void);

/**
 * @brief 发送NACK非应答信号
 * @note 表示停止接收数据
 */
void IIC_NAck(void);

/**
 * @brief 向指定I2C设备地址写入一个字节（便捷函数）
 * @param daddr: 器件地址（7位）
 * @param addr: 寄存器地址
 * @param data: 要写入的数据
 */
void IIC_Write_One_Byte(u8 daddr, u8 addr, u8 data);

/**
 * @brief 从指定I2C设备地址读取一个字节（便捷函数）
 * @param daddr: 器件地址（7位）
 * @param addr: 寄存器地址
 * @retval 读取到的数据
 */
u8 IIC_Read_One_Byte(u8 daddr, u8 addr);

#endif /* __MYIIC_H */
