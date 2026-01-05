/*
 * hgq_myiic3.c
 * 第三路软件I2C驱动（用于连接BH1750光照传感器）
 * 功能：软件模拟I2C总线通信
 * 引脚：PC0=SCL，PC1=SDA
 * 作者：黄光全
 * 日期：2025-12-26
 * 
 * 特点：
 * 1. 软件模拟I2C协议，不依赖硬件I2C外设
 * 2. 开漏输出模式，支持多主机和线"与"逻辑
 * 3. 标准100kHz通信速率
 * 4. 完整的起始、停止、应答机制
 * 5. 超时检测，防止总线死锁
 * 
 * I2C时序参数（100kHz）：
 * SCL高电平时间：>4μs
 * SCL低电平时间：>4.7μs
 * 建立时间：>250ns
 * 保持时间：>0
 */

#include "hgq_myiic3.h"

/**
 * @brief 配置SDA引脚为输入模式
 * @note 用于读取从机应答和数据接收
 *       开漏模式下需要切换为输入才能读取总线状态
 */
static void HGQ_IIC3_SDA_IN(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    GPIO_InitStructure.GPIO_Pin  = HGQ_IIC3_SDA_PIN;   /* SDA引脚 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;       /* 输入模式 */
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;       /* 上拉电阻 */
    GPIO_Init(HGQ_IIC3_GPIO_PORT, &GPIO_InitStructure);
}

/**
 * @brief 配置SDA引脚为开漏输出模式
 * @note 用于数据发送和总线控制
 *       开漏输出允许总线"线与"，支持多主机
 */
static void HGQ_IIC3_SDA_OUT_OD(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    GPIO_InitStructure.GPIO_Pin   = HGQ_IIC3_SDA_PIN;   /* SDA引脚 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;      /* 输出模式 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;      /* 开漏输出 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;  /* 高速 */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;       /* 上拉电阻 */
    GPIO_Init(HGQ_IIC3_GPIO_PORT, &GPIO_InitStructure);
}

/**
 * @brief 配置SCL引脚为开漏输出模式
 * @note 用于时钟信号控制
 *       开漏输出允许其他设备拉低时钟线
 */
static void HGQ_IIC3_SCL_OUT_OD(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    GPIO_InitStructure.GPIO_Pin   = HGQ_IIC3_SCL_PIN;   /* SCL引脚 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;      /* 输出模式 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;      /* 开漏输出 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;  /* 高速 */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;       /* 上拉电阻 */
    GPIO_Init(HGQ_IIC3_GPIO_PORT, &GPIO_InitStructure);
}

/**
 * @brief I2C总线初始化
 * @note 配置引脚模式，设置总线空闲状态（SCL和SDA均为高电平）
 */
void HGQ_IIC3_Init(void)
{
    /* 开启GPIOC时钟 */
    RCC_AHB1PeriphClockCmd(HGQ_IIC3_GPIO_RCC, ENABLE);
    
    /* 配置SCL和SDA为开漏输出模式 */
    HGQ_IIC3_SCL_OUT_OD();
    HGQ_IIC3_SDA_OUT_OD();
    
    /* 设置总线空闲状态：SCL=高，SDA=高 */
    HGQ_IIC3_SCL_H();
    HGQ_IIC3_SDA_H();
    delay_us(5);  /* 等待总线稳定 */
}

/**
 * @brief I2C起始信号
 * @note 起始条件：SCL高电平时，SDA从高到低跳变
 *       表示开始一次新的通信
 */
void HGQ_IIC3_Start(void)
{
    HGQ_IIC3_SDA_OUT_OD();  /* 确保SDA为输出模式 */
    HGQ_IIC3_SDA_H();       /* SDA先置高 */
    HGQ_IIC3_SCL_H();       /* SCL置高 */
    delay_us(4);            /* 满足建立时间 */
    
    HGQ_IIC3_SDA_L();       /* SDA从高变低，产生起始条件 */
    delay_us(4);            /* 保持时间 */
    
    HGQ_IIC3_SCL_L();       /* SCL拉低，准备发送数据 */
    delay_us(2);            /* 时钟低电平时间 */
}

/**
 * @brief I2C停止信号
 * @note 停止条件：SCL高电平时，SDA从低到高跳变
 *       表示结束当前通信
 */
void HGQ_IIC3_Stop(void)
{
    HGQ_IIC3_SDA_OUT_OD();  /* 确保SDA为输出模式 */
    HGQ_IIC3_SCL_L();       /* SCL先置低 */
    HGQ_IIC3_SDA_L();       /* SDA置低 */
    delay_us(4);            /* 建立时间 */
    
    HGQ_IIC3_SCL_H();       /* SCL置高 */
    delay_us(4);            /* 保持时间 */
    
    HGQ_IIC3_SDA_H();       /* SDA从低变高，产生停止条件 */
    delay_us(4);            /* 总线空闲时间 */
}

/**
 * @brief 等待从机应答信号
 * @note 主机发送完地址或数据后，等待从机的ACK信号
 *       ACK信号：从机在第9个时钟周期拉低SDA
 * @retval 0: 收到应答，1: 无应答或超时
 */
uint8_t HGQ_IIC3_WaitAck(void)
{
    uint16_t timeout = 300;  /* 超时计数器 */
    
    HGQ_IIC3_SDA_IN();  /* SDA切换为输入模式，准备读取 */
    HGQ_IIC3_SDA_H();   /* 释放SDA线，由上拉电阻拉高 */
    delay_us(1);
    
    HGQ_IIC3_SCL_H();   /* SCL置高，从机在此时钟周期应答 */
    delay_us(1);
    
    /* 等待从机拉低SDA（应答信号）*/
    while (HGQ_IIC3_READ_SDA)  /* 检测SDA是否为低电平 */
    {
        if (--timeout == 0)    /* 超时检测 */
        {
            HGQ_IIC3_Stop();   /* 发送停止信号，释放总线 */
            return 1;          /* 超时返回错误 */
        }
        delay_us(1);
    }
    
    HGQ_IIC3_SCL_L();  /* SCL拉低，继续后续通信 */
    return 0;          /* 成功收到应答 */
}

/**
 * @brief 主机发送应答信号
 * @note 主机在接收数据后发送ACK，表示继续接收
 *       ACK信号：在第9个时钟周期拉低SDA
 */
void HGQ_IIC3_Ack(void)
{
    HGQ_IIC3_SCL_L();          /* SCL先置低 */
    HGQ_IIC3_SDA_OUT_OD();     /* SDA切换为输出模式 */
    HGQ_IIC3_SDA_L();          /* SDA拉低，表示ACK */
    delay_us(2);
    
    HGQ_IIC3_SCL_H();          /* SCL置高，产生应答时钟脉冲 */
    delay_us(2);
    HGQ_IIC3_SCL_L();          /* SCL拉低，准备后续通信 */
}

/**
 * @brief 主机发送非应答信号
 * @note 主机在接收最后一个字节后发送NACK，表示停止接收
 *       NACK信号：在第9个时钟周期保持SDA高电平
 */
void HGQ_IIC3_NAck(void)
{
    HGQ_IIC3_SCL_L();          /* SCL先置低 */
    HGQ_IIC3_SDA_OUT_OD();     /* SDA切换为输出模式 */
    HGQ_IIC3_SDA_H();          /* SDA保持高，表示NACK */
    delay_us(2);
    
    HGQ_IIC3_SCL_H();          /* SCL置高，产生应答时钟脉冲 */
    delay_us(2);
    HGQ_IIC3_SCL_L();          /* SCL拉低，准备后续通信 */
}

/**
 * @brief I2C发送一个字节
 * @param data: 要发送的字节数据
 * @note 高位先发，每个数据位后跟一个时钟脉冲
 */
void HGQ_IIC3_SendByte(uint8_t data)
{
    HGQ_IIC3_SDA_OUT_OD();  /* SDA为输出模式 */
    HGQ_IIC3_SCL_L();       /* SCL先置低 */
    
    /* 发送8位数据，从最高位（MSB）开始 */
    for (uint8_t i = 0; i < 8; i++)
    {
        /* 根据数据位设置SDA电平 */
        (data & 0x80) ? HGQ_IIC3_SDA_H() : HGQ_IIC3_SDA_L();
        data <<= 1;  /* 左移准备下一位 */
        
        delay_us(2);         /* 数据建立时间 */
        HGQ_IIC3_SCL_H();    /* SCL置高，从机在上升沿采样数据 */
        delay_us(2);
        HGQ_IIC3_SCL_L();    /* SCL拉低，准备下一位 */
        delay_us(2);
    }
}

/**
 * @brief I2C接收一个字节
 * @param ack: 是否发送应答信号
 *              1: 发送ACK（继续接收）
 *              0: 发送NACK（最后一个字节）
 * @note 高位先收，每个时钟脉冲读取一位数据
 * @retval 接收到的字节数据
 */
uint8_t HGQ_IIC3_ReadByte(uint8_t ack)
{
    uint8_t data = 0;
    HGQ_IIC3_SDA_IN();  /* SDA切换为输入模式，准备读取 */
    
    /* 接收8位数据，从最高位开始 */
    for (uint8_t i = 0; i < 8; i++)
    {
        HGQ_IIC3_SCL_L();   /* SCL先置低 */
        delay_us(2);
        
        HGQ_IIC3_SCL_H();   /* SCL置高，主机在此时钟周期读取数据 */
        delay_us(2);
        
        data <<= 1;         /* 左移为下一位腾出空间 */
        if (HGQ_IIC3_READ_SDA) 
            data |= 1;      /* 读取SDA电平，设置对应位 */
    }
    
    /* 根据参数发送应答或非应答 */
    ack ? HGQ_IIC3_Ack() : HGQ_IIC3_NAck();
    
    return data;  /* 返回接收到的字节 */
}
