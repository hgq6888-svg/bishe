#ifndef __24CXX_H
#define __24CXX_H
#include "myiic.h"   
/******************************************************************************
 * @file     24cxx.h
 * @brief    AT24CXX系列EEPROM头文件
 * @author   正点原子@ALIENTEK
 * @date     2014/5/6
 * @version  V1.0
 * 
 * 硬件连接：
 * AT24C02 -> STM32F407
 *   VCC   -> 3.3V
 *   GND   -> GND
 *   SCL   -> PB8
 *   SDA   -> PB9
 *   WP    -> GND（写保护接地，允许写入）
 *   A0-A2 -> GND（地址引脚接地，I2C地址为0xA0）
 * 
 * 容量定义：
 * 支持从AT24C01到AT24C256全系列
 * 每个容量的定义值为该型号的最大地址
 ******************************************************************************/

/* ================= AT24CXX系列容量定义（最大地址+1）================= */
#define AT24C01		127		/* AT24C01: 128字节，最大地址127 */
#define AT24C02		255		/* AT24C02: 256字节，最大地址255 */
#define AT24C04		511		/* AT24C04: 512字节，最大地址511 */
#define AT24C08		1023	/* AT24C08: 1KB，最大地址1023 */
#define AT24C16		2047	/* AT24C16: 2KB，最大地址2047 */
#define AT24C32		4095	/* AT24C32: 4KB，最大地址4095 */
#define AT24C64	    8191	/* AT24C64: 8KB，最大地址8191 */
#define AT24C128	16383	/* AT24C128: 16KB，最大地址16383 */
#define AT24C256	32767	/* AT24C256: 32KB，最大地址32767 */

/* 开发板使用的EEPROM型号定义（根据实际硬件修改）*/
#define EE_TYPE AT24C02	/* Mini STM32开发板使用AT24C02 */

/* ================= API函数声明 ================= */

/**
 * @brief 指定地址读取一个字节
 * @param ReadAddr: 读取地址
 * @retval 读取到的字节数据
 */
u8 AT24CXX_ReadOneByte(u16 ReadAddr);

/**
 * @brief 指定地址写入一个字节
 * @param WriteAddr: 写入地址
 * @param DataToWrite: 要写入的数据
 */
void AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite);

/**
 * @brief 写入16位或32位数据
 * @param WriteAddr: 开始写入的地址
 * @param DataToWrite: 要写入的长整型数据
 * @param Len: 数据长度（2=16位，4=32位）
 */
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len);

/**
 * @brief 读取16位或32位数据
 * @param ReadAddr: 开始读取的地址
 * @param Len: 数据长度（2=16位，4=32位）
 * @retval 读取到的长整型数据
 */
u32 AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len);

/**
 * @brief 连续写入多个字节
 * @param WriteAddr: 开始写入的地址
 * @param pBuffer: 数据源缓冲区指针
 * @param NumToWrite: 要写入的字节数
 */
void AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite);

/**
 * @brief 连续读取多个字节
 * @param ReadAddr: 开始读取的地址
 * @param pBuffer: 数据存储缓冲区指针
 * @param NumToRead: 要读取的字节数
 */
void AT24CXX_Read(u16 ReadAddr, u8 *pBuffer, u16 NumToRead);

/**
 * @brief 检查EEPROM是否正常工作
 * @retval 0: 正常，1: 异常
 */
u8 AT24CXX_Check(void);

/**
 * @brief 初始化EEPROM（实际是初始化I2C）
 */
void AT24CXX_Init(void);

#endif /* __24CXX_H */
