/******************************************************************************
 * @file     24cxx.c
 * @brief    AT24CXX系列EEPROM存储器驱动程序
 * @author   正点原子@ALIENTEK
 * @date     2014/5/6
 * @version  V1.0
 * @note     
 * 功能概述：
 * 1. 支持AT24C01/02/04/08/16/32/64/128/256全系列EEPROM
 * 2. 提供字节读写、多字节读写、长整型数据读写功能
 * 3. 自动处理EEPROM的页写限制和地址扩展
 * 4. 包含EEPROM状态检测和初始化功能
 * 
 * EEPROM特点：
 * 1. 非易失性存储器，掉电数据不丢失
 * 2. I2C接口，支持标准模式（100kHz）和快速模式（400kHz）
 * 3. 写周期时间：典型5ms，最大10ms
 * 4. 数据保持时间：>40年
 * 
 * 使用注意事项：
 * 1. 写操作后需要延时等待内部写周期完成
 * 2. 注意EEPROM的页大小限制（AT24C02为8字节/页）
 * 3. 大容量EEPROM需要特殊地址处理
 ******************************************************************************/

#include "24cxx.h" 
#include "delay.h" 				 

/**
 * @brief 初始化AT24CXX EEPROM
 * @note  实际上是初始化I2C总线接口
 *        因为EEPROM本身不需要特殊初始化
 */
void AT24CXX_Init(void)
{
	IIC_Init();	//IIC总线初始化
}

/**
 * @brief 从AT24CXX指定地址读取一个字节数据
 * @param ReadAddr: 要读取的地址（0-最大地址）
 * @note  读取流程：
 *         1. 发送起始信号
 *         2. 发送器件地址+写命令
 *         3. 发送要读取的地址（高8位+低8位，大容量时需要）
 *         4. 重新发送起始信号
 *         5. 发送器件地址+读命令
 *         6. 读取一个字节数据
 *         7. 发送停止信号
 * @retval 读取到的字节数据
 */
u8 AT24CXX_ReadOneByte(u16 ReadAddr)
{				  
	u8 temp = 0;		  	    																 
    IIC_Start();  	//I2C起始信号
    
    // 根据EEPROM容量处理器件地址
	if(EE_TYPE > AT24C16)	// AT24C32及以上容量
	{
		IIC_Send_Byte(0XA0);	   // 发送写命令，器件地址固定为0xA0
		IIC_Wait_Ack();
		IIC_Send_Byte(ReadAddr >> 8); // 发送高8位地址（大容量需要16位地址）
	}else {
        // AT24C16及以下容量，地址高2位在器件地址中
		IIC_Send_Byte(0XA0 + ((ReadAddr / 256) << 1));   // 发送器件地址0XA0，加上页地址
    }
    
	IIC_Wait_Ack(); 
    IIC_Send_Byte(ReadAddr % 256);   // 发送低8位地址
	IIC_Wait_Ack();	    
	
	IIC_Start();  	 	   	// 重新发送起始信号
	IIC_Send_Byte(0XA1);           // 发送读命令，器件地址+读位(1)
	IIC_Wait_Ack();	 
    
    temp = IIC_Read_Byte(0);		   // 读取一个字节，发送NACK
    IIC_Stop();	//产生停止条件	    
	return temp;
}

/**
 * @brief 向AT24CXX指定地址写入一个字节数据
 * @param WriteAddr: 写入地址（0-最大地址）
 * @param DataToWrite: 要写入的数据
 * @note  写入流程：
 *         1. 发送起始信号
 *         2. 发送器件地址+写命令
 *         3. 发送要写入的地址
 *         4. 发送要写入的数据
 *         5. 发送停止信号
 *         6. 延时等待EEPROM内部写周期完成（典型5-10ms）
 */
void AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite)
{				   	  	    																 
    IIC_Start();  	// I2C起始信号
    
    // 根据EEPROM容量处理器件地址
	if(EE_TYPE > AT24C16)	// AT24C32及以上容量
	{
		IIC_Send_Byte(0XA0);	    // 发送写命令
		IIC_Wait_Ack();
		IIC_Send_Byte(WriteAddr >> 8); // 发送高8位地址
	}else {
        // AT24C16及以下容量
		IIC_Send_Byte(0XA0 + ((WriteAddr / 256) << 1));   // 发送器件地址
    }
    
	IIC_Wait_Ack();	   
    IIC_Send_Byte(WriteAddr % 256);   // 发送低8位地址
	IIC_Wait_Ack(); 	 										  		   
	IIC_Send_Byte(DataToWrite);     // 发送要写入的字节数据							   
	IIC_Wait_Ack();  		    	   
    IIC_Stop();	//产生停止条件 
	
	delay_ms(10);	 // 重要！必须等待EEPROM内部写周期完成（典型5ms）
}

/**
 * @brief 向AT24CXX写入16位或32位数据（多字节写入）
 * @param WriteAddr: 开始写入的地址
 * @param DataToWrite: 要写入的长整型数据
 * @param Len: 数据长度（2:16位, 4:32位）
 * @note  将长整型数据按字节拆分后逐个写入
 *        注意：写入后会自动递增地址
 */
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len)
{  	
	u8 t;
	for(t = 0; t < Len; t++)
	{
        // 按字节写入，从最低字节开始
		AT24CXX_WriteOneByte(WriteAddr + t, (DataToWrite >> (8 * t)) & 0xff);
	}												    
}

/**
 * @brief 从AT24CXX读取16位或32位数据（多字节读取）
 * @param ReadAddr: 开始读取的地址
 * @param Len: 数据长度（2:16位, 4:32位）
 * @note  读取多个字节后组合成长整型数据
 *        注意：从高地址向低地址读取，确保数据正确组合
 * @retval 读取到的长整型数据
 */
u32 AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len)
{  	
	u8 t;
	u32 temp = 0;
	for(t = 0; t < Len; t++)
	{
		temp <<= 8;	// 左移8位为下一个字节腾出位置
		temp += AT24CXX_ReadOneByte(ReadAddr + Len - t - 1); 	// 从高地址向低地址读取 				   
	}
	return temp;												    
}

/**
 * @brief 检测AT24CXX是否正常工作
 * @note  使用EEPROM最后一个地址（255）存储测试标志
 *        原理：写入一个特定值（0x55），然后读回验证
 *        如果读写一致，说明EEPROM工作正常
 * @retval 0: 检测成功，1: 检测失败
 */
u8 AT24CXX_Check(void)
{
	u8 temp;
	temp = AT24CXX_ReadOneByte(255);	// 读取测试地址的值
	    
	if(temp == 0X55) {
        return 0;	// 已存在测试标志，说明之前已初始化成功
    } else {
        // 第一次运行或EEPROM未初始化，进行初始化测试
		AT24CXX_WriteOneByte(255, 0X55);	// 写入测试标志
	    temp = AT24CXX_ReadOneByte(255);	  // 读回验证
		
        if(temp == 0X55) {
            return 0;	// 读写一致，检测成功
        }
	}
	return 1;	// 读写不一致，检测失败											  
}

/**
 * @brief 从AT24CXX连续读取多个字节数据
 * @param ReadAddr: 开始读取的地址
 * @param pBuffer: 数据存储缓冲区指针
 * @param NumToRead: 要读取的字节数
 * @note  循环调用AT24CXX_ReadOneByte读取指定数量的字节
 *        适用于大量数据的连续读取
 */
void AT24CXX_Read(u16 ReadAddr, u8 *pBuffer, u16 NumToRead)
{
	while(NumToRead)
	{
		*pBuffer++ = AT24CXX_ReadOneByte(ReadAddr++);	// 读取一个字节并递增指针
		NumToRead--;
	}
}  

/**
 * @brief 向AT24CXX连续写入多个字节数据
 * @param WriteAddr: 开始写入的地址
 * @param pBuffer: 数据源缓冲区指针
 * @param NumToWrite: 要写入的字节数
 * @note  循环调用AT24CXX_WriteOneByte写入指定数量的字节
 *        注意：跨页写入需要特殊处理（这里没有处理）
 *        每写入一个字节都会等待10ms，影响写入速度
 */
void AT24CXX_Write(u16 WriteAddr, u8 *pBuffer, u16 NumToWrite)
{
	while(NumToWrite--)
	{
		AT24CXX_WriteOneByte(WriteAddr, *pBuffer);	// 写入一个字节
		WriteAddr++;	// 地址递增
		pBuffer++;		// 数据指针递增
	}
}
