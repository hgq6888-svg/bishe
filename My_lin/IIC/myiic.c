/******************************************************************************
 * @file     myiic.c
 * @brief    软件I2C驱动程序（正点原子版本）
 * @author   正点原子@ALIENTEK
 * @date     2014/5/6
 * @version  V1.0
 * 
 * 功能特点：
 * 1. 完全软件模拟I2C时序，不依赖硬件I2C外设
 * 2. 支持标准模式（100kHz）和快速模式
 * 3. 使用直接寄存器操作，执行效率高
 * 4. 包含完整的起始、停止、应答机制
 * 5. 超时检测，防止总线死锁
 * 
 * 引脚配置：
 * PB8: SCL（时钟线）
 * PB9: SDA（数据线）
 * 
 * I2C时序参数（100kHz标准模式）：
 * SCL高电平时间：>4.0μs
 * SCL低电平时间：>4.7μs
 * 数据建立时间：>250ns
 * 数据保持时间：>0
 ******************************************************************************/

#include "myiic.h"
#include "delay.h"

/**
 * @brief I2C总线初始化
 * @note 配置PB8(SCL)和PB9(SDA)为推挽输出模式
 *       设置初始状态：SCL=1, SDA=1（总线空闲）
 */
void IIC_Init(void)
{			
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);	// 使能GPIOB时钟

    // 配置PB8(SCL)和PB9(SDA)引脚
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;			// 普通输出模式
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;			// 推挽输出（注意：非开漏）
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;		// 100MHz速度
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;			// 上拉电阻
    GPIO_Init(GPIOB, &GPIO_InitStructure);					// 初始化GPIO
    
    // 设置总线初始状态：空闲时SCL和SDA都为高电平
    IIC_SCL = 1;
    IIC_SDA = 1;
}

/**
 * @brief 产生I2C起始信号
 * @note 起始条件：SCL为高电平时，SDA从高电平跳变到低电平
 *       时序：SCL高 -> SDA高 -> 延时 -> SDA低 -> 延时 -> SCL低
 */
void IIC_Start(void)
{
    SDA_OUT();     		// 设置SDA为输出模式
    IIC_SDA = 1;	  	// SDA先置高
    IIC_SCL = 1;		// SCL置高
    delay_us(4);		// 保持时间
    
    IIC_SDA = 0;		// START条件：当CLK为高时，DATA从高变低
    delay_us(4);
    
    IIC_SCL = 0;		// 拉低时钟线，准备发送数据
}

/**
 * @brief 产生I2C停止信号
 * @note 停止条件：SCL为高电平时，SDA从低电平跳变到高电平
 *       时序：SCL低 -> SDA低 -> 延时 -> SCL高 -> 延时 -> SDA高
 */
void IIC_Stop(void)
{
    SDA_OUT();			// 设置SDA为输出模式
    IIC_SCL = 0;		// SCL先置低
    IIC_SDA = 0;		// SDA置低
    
    delay_us(4);
    IIC_SCL = 1; 		// SCL置高
    IIC_SDA = 1;		// STOP条件：当CLK为高时，DATA从低变高
    delay_us(4);							   	
}

/**
 * @brief 等待从机应答信号
 * @note 主机在第9个时钟周期检测SDA是否为低电平
 *       如果从机正确接收数据，会拉低SDA作为应答
 *       包含超时检测，防止总线死锁
 * @retval 0: 收到应答，1: 无应答或超时
 */
u8 IIC_Wait_Ack(void)
{
    u8 ucErrTime = 0;
    
    SDA_IN();      		// SDA设置为输入模式
    IIC_SDA = 1;		// 释放SDA线
    delay_us(1);	   
    
    IIC_SCL = 1;		// SCL置高，从机在此时钟周期应答
    delay_us(1);	 
    
    // 等待从机拉低SDA（应答信号）
    while(READ_SDA)
    {
        ucErrTime++;	// 超时计数器
        if(ucErrTime > 250)	// 超时（约250μs）
        {
            IIC_Stop();		// 发送停止信号释放总线
            return 1;		// 超时返回错误
        }
    }
    
    IIC_SCL = 0;		// 时钟输出0
    return 0;  			// 收到应答
}

/**
 * @brief 产生ACK应答信号
 * @note 主机在接收数据后发送ACK，表示继续接收
 *       ACK信号：在第9个时钟周期拉低SDA
 */
void IIC_Ack(void)
{
    IIC_SCL = 0;		// SCL先置低
    SDA_OUT();			// SDA设置为输出模式
    IIC_SDA = 0;		// SDA拉低，表示ACK
    delay_us(2);
    
    IIC_SCL = 1;		// SCL置高，产生应答时钟脉冲
    delay_us(2);
    IIC_SCL = 0;		// SCL拉低，准备后续通信
}

/**
 * @brief 产生NACK非应答信号
 * @note 主机在接收最后一个字节后发送NACK，表示停止接收
 *       NACK信号：在第9个时钟周期保持SDA高电平
 */
void IIC_NAck(void)
{
    IIC_SCL = 0;		// SCL先置低
    SDA_OUT();			// SDA设置为输出模式
    IIC_SDA = 1;		// SDA保持高，表示NACK
    delay_us(2);
    
    IIC_SCL = 1;		// SCL置高，产生应答时钟脉冲
    delay_us(2);
    IIC_SCL = 0;		// SCL拉低，准备后续通信
}

/**
 * @brief I2C发送一个字节
 * @param txd: 要发送的字节数据
 * @note 高位先发（MSB first）
 *       每个数据位后跟一个时钟脉冲
 *       时序：设置数据位 -> 延时 -> SCL高 -> 延时 -> SCL低
 */
void IIC_Send_Byte(u8 txd)
{                        
    u8 t;   
    SDA_OUT(); 		// 设置SDA为输出模式
    IIC_SCL = 0;	// 拉低时钟开始数据传输
    
    // 发送8位数据，从最高位（MSB）开始
    for(t = 0; t < 8; t++)
    {              
        // 取出最高位并设置SDA电平
        IIC_SDA = (txd & 0x80) >> 7;
        txd <<= 1; 	// 左移准备下一位
        	  
        delay_us(2);	// 数据建立时间
        IIC_SCL = 1;	// SCL置高，从机在上升沿采样数据
        delay_us(2); 
        IIC_SCL = 0;	// SCL拉低，准备下一位数据
        delay_us(2);
    }	 
}

/**
 * @brief I2C读取一个字节
 * @param ack: 是否发送应答信号
 *              1: 发送ACK（继续接收）
 *              0: 发送NACK（停止接收）
 * @note 高位先收（MSB first）
 *       每个时钟脉冲读取一位数据
 * @retval 接收到的字节数据
 */
u8 IIC_Read_Byte(unsigned char ack)
{
    unsigned char i, receive = 0;
    
    SDA_IN();		// SDA设置为输入模式
    
    // 接收8位数据，从最高位开始
    for(i = 0; i < 8; i++)
    {
        IIC_SCL = 0; 	// SCL先置低
        delay_us(2);
        
        IIC_SCL = 1;	// SCL置高，主机在此时钟周期读取数据
        receive <<= 1;	// 左移为下一位腾出空间
        
        if(READ_SDA) {
            receive++;	// 读取SDA电平，如果为高则设置对应位
        }
        
        delay_us(1); 
    }					 
    
    // 根据参数发送应答或非应答
    if (!ack) {
        IIC_NAck();		// 发送NACK
    } else {
        IIC_Ack(); 		// 发送ACK
    }
    
    return receive;		// 返回接收到的字节
}
