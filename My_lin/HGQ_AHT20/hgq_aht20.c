/*
 * hgq_aht20.c
 * AHT20数字温湿度传感器驱动程序
 * 功能：测量环境温度和相对湿度
 * 接口：I2C通信，使用第二路软件I2C
 * 作者：黄光全
 * 日期：2025-12-26
 * 
 * 传感器特点：
 * 1. 高精度：温度±0.3℃，湿度±2%RH
 * 2. 快速响应：温度8秒，湿度5秒
 * 3. 低功耗：测量模式0.8mA，休眠模式0.2μA
 * 4. 数字输出，I2C接口
 * 5. 内部校准，长期稳定性好
 * 
 * I2C地址：
 * 固定地址：0x38（7位地址）
 * 
 * 测量范围：
 * 温度：-40℃ ~ +85℃
 * 湿度：0%RH ~ 100%RH
 * 
 * 数据格式：
 * 温度：20位数据（2^20 = 1048576个码值）
 * 湿度：20位数据（2^20 = 1048576个码值）
 * 计算公式：
 * 温度(℃) = (T_raw * 200 / 1048576) - 50
 * 湿度(%RH) = (H_raw * 100 / 1048576)
 */

#include "hgq_aht20.h"
#include "hgq_myiic2.h"  /* 第二路软件I2C */
#include "delay.h"

/* AHT20 I2C地址定义 */
#define HGQ_AHT20_ADDR   0x38      /* AHT20的7位I2C地址 */
#define HGQ_AHT20_W      (HGQ_AHT20_ADDR<<1)      /* 写地址：bit0=0 */
#define HGQ_AHT20_R      ((HGQ_AHT20_ADDR<<1) | 1) /* 读地址：bit0=1 */

/**
 * @brief 向AHT20发送命令序列
 * @param cmd: 命令数组指针
 * @param len: 命令长度（字节数）
 * @note 支持发送多字节命令序列
 * @retval 0: 成功，1: 地址ACK失败，2: 数据ACK失败
 */
static uint8_t HGQ_AHT20_WriteCmd(const uint8_t *cmd, uint8_t len)
{
    /* I2C起始信号 */
    HGQ_IIC2_Start();
    
    /* 发送设备地址（写模式）*/
    HGQ_IIC2_SendByte(HGQ_AHT20_W);
    if (HGQ_IIC2_WaitAck()) { 
        HGQ_IIC2_Stop(); 
        return 1;  /* 地址ACK失败 */
    }
    
    /* 发送命令序列的每个字节 */
    for (uint8_t i = 0; i < len; i++)
    {
        HGQ_IIC2_SendByte(cmd[i]);
        if (HGQ_IIC2_WaitAck()) { 
            HGQ_IIC2_Stop(); 
            return 2;  /* 数据ACK失败 */
        }
    }
    
    /* I2C停止信号 */
    HGQ_IIC2_Stop();
    return 0;  /* 成功 */
}

/**
 * @brief 从AHT20读取多个字节数据
 * @param buf: 数据接收缓冲区
 * @param len: 要读取的字节数
 * @note 读取完成后发送NACK表示停止接收
 * @retval 0: 成功，1: 地址ACK失败
 */
static uint8_t HGQ_AHT20_ReadBytes(uint8_t *buf, uint8_t len)
{
    /* I2C起始信号 */
    HGQ_IIC2_Start();
    
    /* 发送设备地址（读模式）*/
    HGQ_IIC2_SendByte(HGQ_AHT20_R);
    if (HGQ_IIC2_WaitAck()) { 
        HGQ_IIC2_Stop(); 
        return 1;  /* 地址ACK失败 */
    }
    
    /* 读取指定数量的字节 */
    for (uint8_t i = 0; i < len; i++)
        buf[i] = HGQ_IIC2_ReadByte(i != (len - 1)); /* 最后一个字节发NACK */
    
    /* I2C停止信号 */
    HGQ_IIC2_Stop();
    return 0;  /* 成功 */
}

/**
 * @brief AHT20传感器初始化
 * @note 初始化步骤：
 *       1. 初始化I2C总线
 *       2. 发送初始化命令（0xBE 0x08 0x00）
 *       3. 等待传感器稳定
 * @retval 0: 成功，1: 初始化失败
 */
uint8_t HGQ_AHT20_Init(void)
{
    /* 初始化命令：0xBE（初始化），0x08（参数），0x00（保留）*/
    uint8_t cmd[3] = {0xBE, 0x08, 0x00};
    
    /* 初始化I2C总线 */
    HGQ_IIC2_Init();
    
    /* AHT20上电后需要至少40ms稳定时间 */
    delay_ms(40);
    
    /* 发送初始化命令 */
    if (HGQ_AHT20_WriteCmd(cmd, 3)) 
        return 1;  /* 初始化失败 */
    
    delay_ms(10);  /* 等待命令执行完成 */
    return 0;      /* 初始化成功 */
}

/**
 * @brief 读取AHT20的温湿度数据
 * @param temp_c: 温度输出指针（单位：℃）
 * @param humi_rh: 湿度输出指针（单位：%RH）
 * @note 测量流程：
 *       1. 发送测量触发命令（0xAC 0x33 0x00）
 *       2. 等待测量完成（检查状态寄存器busy位）
 *       3. 读取6字节原始数据（状态+湿度+温度）
 *       4. 转换为实际温湿度值
 * @retval 0: 成功
 *         1: 触发测量失败
 *         2: 状态读取失败
 *         3: 测量超时
 *         4: 数据读取失败
 */
uint8_t HGQ_AHT20_Read(float *temp_c, float *humi_rh)
{
    /* 测量触发命令：0xAC（触发测量），0x33（参数），0x00（保留）*/
    uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    uint8_t data[6];           /* 6字节数据缓冲区 */
    uint16_t timeout = 200;    /* 超时计数器（约1秒）*/
    
    /* 1. 发送测量触发命令 */
    if (HGQ_AHT20_WriteCmd(cmd, 3)) 
        return 1;  /* 触发测量失败 */
    
    /* 2. 等待测量完成（busy位为0）*/
    delay_ms(10);  /* 测量需要至少75ms */
    
    do {
        /* 读取状态寄存器（1字节）*/
        if (HGQ_AHT20_ReadBytes(data, 1)) 
            return 2;  /* 状态读取失败 */
        
        /* 检查busy位（bit7）：0表示测量完成 */
        if ((data[0] & 0x80) == 0) 
            break;     /* 测量完成 */
        
        delay_ms(5);   /* 等待5ms再次检查 */
    } while(--timeout);
    
    if (timeout == 0) 
        return 3;  /* 测量超时 */
    
    /* 3. 读取完整的6字节数据 */
    if (HGQ_AHT20_ReadBytes(data, 6)) 
        return 4;  /* 数据读取失败 */
    
    /* 4. 数据解析和转换 */
    {
        /* 湿度数据：data[1:3]的20位数据
         * data[1]: 湿度高8位
         * data[2]: 湿度低8位
         * data[3]高4位：湿度最低4位
         */
        uint32_t rh_raw = ((uint32_t)data[1] << 12) | 
                          ((uint32_t)data[2] << 4) | 
                          (data[3] >> 4);
        
        /* 温度数据：data[3:5]的20位数据
         * data[3]低4位：温度最高4位
         * data[4]: 温度中间8位
         * data[5]: 温度低8位
         */
        uint32_t t_raw  = (((uint32_t)(data[3] & 0x0F)) << 16) | 
                          ((uint32_t)data[4] << 8) | 
                          data[5];
        
        /* 转换为实际温湿度值
         * 湿度：RH(%) = raw * 100 / 2^20
         * 温度：T(℃) = raw * 200 / 2^20 - 50
         */
        *humi_rh = (float)rh_raw * 100.0f / 1048576.0f;  /* 2^20 = 1048576 */
        *temp_c  = (float)t_raw  * 200.0f / 1048576.0f - 50.0f;
    }
    
    return 0;  /* 成功 */
}
