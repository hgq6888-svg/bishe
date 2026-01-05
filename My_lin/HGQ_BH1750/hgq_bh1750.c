/*
 * hgq_bh1750.c
 * BH1750光照强度传感器驱动程序
 * 功能：测量环境光照强度（0-65535 lux）
 * 接口：I2C通信，使用第三路软件I2C
 * 作者：黄光全
 * 日期：2025-12-26
 * 
 * 传感器特点：
 * 1. 数字输出，无需ADC转换
 * 2. 测量范围：0-65535 lux
 * 3. 分辨率：1 lux（高精度模式）
 * 4. 低功耗，内置16位AD转换器
 * 5. 支持多种测量模式
 * 
 * I2C地址：
 * 地址引脚悬空（或接地）：0x23（7位地址）
 * 地址引脚接VCC：0x5C（7位地址）
 * 
 * 工作模式：
 * 0x10：连续高分辨率模式（120ms测量时间）
 * 0x11：连续高分辨率模式2（120ms）
 * 0x13：连续低分辨率模式（16ms）
 * 0x20：单次高分辨率模式
 * 0x21：单次高分辨率模式2
 * 0x23：单次低分辨率模式
 */

#include "hgq_bh1750.h"
#include "hgq_myiic3.h"  /* 第三路软件I2C */
#include "delay.h"

/* 全局变量：I2C设备地址 */
static uint8_t s_addr_w = 0;  /* 写地址：7位地址左移1位，bit0=0 */
static uint8_t s_addr_r = 0;  /* 读地址：7位地址左移1位，bit0=1 */

/**
 * @brief 向BH1750发送命令
 * @param cmd: BH1750命令字
 * @note 使用I2C写操作发送单个命令字节
 * @retval 0: 成功，1: 地址ACK失败，2: 数据ACK失败
 */
static uint8_t BH1750_WriteCmd(uint8_t cmd)
{
    /* I2C起始信号 */
    HGQ_IIC3_Start();
    
    /* 发送设备地址（写模式）*/
    HGQ_IIC3_SendByte(s_addr_w);
    if (HGQ_IIC3_WaitAck()) { 
        HGQ_IIC3_Stop(); 
        return 1;  /* 地址ACK失败 */
    }
    
    /* 发送命令字节 */
    HGQ_IIC3_SendByte(cmd);
    if (HGQ_IIC3_WaitAck()) { 
        HGQ_IIC3_Stop(); 
        return 2;  /* 数据ACK失败 */
    }
    
    /* I2C停止信号 */
    HGQ_IIC3_Stop();
    return 0;  /* 成功 */
}

/**
 * @brief 从BH1750读取2字节数据
 * @param msb: 高字节输出指针
 * @param lsb: 低字节输出指针
 * @note 读取光照强度原始数据（16位）
 * @retval 0: 成功，1: 地址ACK失败
 */
static uint8_t BH1750_Read2(uint8_t *msb, uint8_t *lsb)
{
    /* I2C起始信号 */
    HGQ_IIC3_Start();
    
    /* 发送设备地址（读模式）*/
    HGQ_IIC3_SendByte(s_addr_r);
    if (HGQ_IIC3_WaitAck()) { 
        HGQ_IIC3_Stop(); 
        return 1;  /* 地址ACK失败 */
    }
    
    /* 读取高字节，发送ACK */
    *msb = HGQ_IIC3_ReadByte(1);
    /* 读取低字节，发送NACK（最后一个字节）*/
    *lsb = HGQ_IIC3_ReadByte(0);
    
    /* I2C停止信号 */
    HGQ_IIC3_Stop();
    return 0;  /* 成功 */
}

/**
 * @brief BH1750传感器初始化
 * @param addr_7bit: BH1750的7位I2C地址（0x23或0x5C）
 * @note 初始化步骤：
 *       1. 初始化I2C总线
 *       2. 设置设备地址
 *       3. 发送上电命令
 *       4. 发送复位命令（可选）
 *       5. 设置测量模式（连续高分辨率）
 *       6. 等待测量完成
 * @retval 0: 成功，1: 上电失败，2: 模式设置失败
 */
uint8_t HGQ_BH1750_Init(uint8_t addr_7bit)
{
    /* 初始化I2C总线 */
    HGQ_IIC3_Init();
    
    /* 计算读写地址 */
    s_addr_w = (addr_7bit << 1);      /* 写地址：bit0=0 */
    s_addr_r = (addr_7bit << 1) | 1;  /* 读地址：bit0=1 */
    
    /* 1. 上电命令：唤醒传感器 */
    if (BH1750_WriteCmd(0x01)) 
        return 1;  /* 上电失败 */
    delay_ms(5);   /* 等待上电稳定 */
    
    /* 2. 复位命令（可选）：清除之前的测量数据 */
    BH1750_WriteCmd(0x07);
    delay_ms(5);
    
    /* 3. 设置连续高分辨率模式（0x10）
     *    测量时间：120ms，分辨率：1 lux
     */
    if (BH1750_WriteCmd(0x10)) 
        return 2;  /* 模式设置失败 */
    
    /* 等待第一次测量完成（手册建议至少180ms）*/
    delay_ms(180);
    
    return 0;  /* 初始化成功 */
}

/**
 * @brief 读取光照强度值
 * @param lux: 光照强度输出指针（单位：lux）
 * @note 计算公式：lux = raw_data / 1.2
 *       避免浮点运算：lux = raw_data * 10 / 12
 * @retval 0: 成功，1: 读取失败
 */
uint8_t HGQ_BH1750_ReadLux(uint16_t *lux)
{
    uint8_t msb, lsb;     /* 高字节和低字节 */
    uint16_t raw;         /* 原始数据（16位）*/
    
    /* 读取原始数据 */
    if (BH1750_Read2(&msb, &lsb)) 
        return 1;  /* 读取失败 */
    
    /* 组合为16位数据：高字节在前 */
    raw = ((uint16_t)msb << 8) | lsb;
    
    /* 转换为光照强度（lux）
     * 公式：lux = raw / 1.2 = raw * 10 / 12
     * 避免浮点运算，使用整数运算
     */
    *lux = (uint16_t)((raw * 10U) / 12U);
    
    return 0;  /* 成功 */
}
