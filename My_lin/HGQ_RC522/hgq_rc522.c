/*
 * hgq_rc522.c
 * RC522 RFID读写模块驱动程序
 * 功能：Mifare 1K卡寻卡、防冲突、选卡、读取UID
 * 接口：软件SPI通信
 * 作者：黄光全
 * 日期：2025-12-26
 * 
 * 特点：
 * 1. 支持ISO14443A标准，兼容Mifare Classic 1K/4K卡
 * 2. 软件SPI实现，引脚可配置
 * 3. 支持防冲突机制，可读取多张卡片UID
 * 4. 简化流程，专注于UID读取，适合门禁系统
 * 5. 代码结构化，易于移植和维护
 */
#include "hgq_rc522.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "delay.h"
#include <stdio.h>
#include <string.h>


/* ================= RC522 寄存器定义（常用）================= */
#define CommandReg          0x01    /* 命令寄存器：设置PCD操作模式 */
#define ComIEnReg           0x02    /* 中断使能寄存器：设置通信中断 */
#define DivIEnReg           0x03    /* 分频中断使能寄存器 */
#define ComIrqReg           0x04    /* 通信中断请求寄存器：读取中断状态 */
#define DivIrqReg           0x05    /* 分频中断请求寄存器 */
#define ErrorReg            0x06    /* 错误寄存器：读取通信错误 */
#define Status1Reg          0x07    /* 状态寄存器1 */
#define Status2Reg          0x08    /* 状态寄存器2 */
#define FIFODataReg         0x09    /* FIFO数据寄存器：数据缓冲区 */
#define FIFOLevelReg        0x0A    /* FIFO水平寄存器：FIFO中数据字节数 */
#define ControlReg          0x0C    /* 控制寄存器 */
#define BitFramingReg       0x0D    /* 位帧调整寄存器：调整数据位长度 */
#define CollReg             0x0E    /* 冲突位置寄存器：防冲突检测 */
#define ModeReg             0x11    /* 模式寄存器：设置发送/接收模式 */
#define TxModeReg           0x12    /* 发送模式寄存器 */
#define RxModeReg           0x13    /* 接收模式寄存器 */
#define TxControlReg        0x14    /* 发送控制寄存器：控制天线驱动 */
#define TxASKReg            0x15    /* 发送ASK调制寄存器 */
#define CRCResultRegH       0x21    /* CRC结果高位寄存器 */
#define CRCResultRegL       0x22    /* CRC结果低位寄存器 */
#define TModeReg            0x2A    /* 定时器模式寄存器 */
#define TPrescalerReg       0x2B    /* 定时器预分频寄存器 */
#define TReloadRegH         0x2C    /* 定时器重载值高位 */
#define TReloadRegL         0x2D    /* 定时器重载值低位 */

/* ================= PCD命令字（读卡器操作） ================= */
#define PCD_IDLE            0x00    /* 空闲命令：进入待机状态 */
#define PCD_MEM             0x01    /* 存储器命令 */
#define PCD_CALCCRC         0x03    /* CRC计算命令：计算数据CRC校验 */
#define PCD_TRANSCEIVE      0x0C    /* 收发命令：发送并接收数据（最常用）*/
#define PCD_RESETPHASE      0x0F    /* 复位命令：复位PCD内部状态机 */

/* ================= PICC命令字（卡片操作） ================= */
#define PICC_REQA           0x26    /* 请求命令A：唤醒附近卡片 */
#define PICC_WUPA           0x52    /* 唤醒命令：强制唤醒卡片 */
#define PICC_ANTICOLL_CL1   0x93    /* 防冲突命令（层1）：获取卡片UID */
#define PICC_SELECT_CL1     0x93    /* 选择命令（层1）：选择指定UID卡片 */
#define PICC_HALT           0x50    /* 休眠命令：使卡片进入休眠状态 */

/* ================= 状态码定义 ================= */
#define MI_OK               0       /* 操作成功 */
#define MI_ERR              1       /* 操作失败（通信错误等）*/
#define MI_NOTAGERR         2       /* 无卡片错误（没有检测到卡片）*/

/* ================= GPIO操作快捷宏 ================= */
static inline void pin_hi(uint16_t pin){ GPIO_SetBits(RC522_GPIO_PORT, pin); }
static inline void pin_lo(uint16_t pin){ GPIO_ResetBits(RC522_GPIO_PORT, pin); }
static inline uint8_t pin_read(uint16_t pin){ 
    return (GPIO_ReadInputDataBit(RC522_GPIO_PORT, pin)==Bit_SET)?1:0; 
}

#define RC522_NSS_H()   pin_hi(RC522_NSS_PIN)   /* 片选高电平：释放SPI */
#define RC522_NSS_L()   pin_lo(RC522_NSS_PIN)   /* 片选低电平：选中RC522 */
#define RC522_RST_H()   pin_hi(RC522_RST_PIN)   /* 复位高电平：正常模式 */
#define RC522_RST_L()   pin_lo(RC522_RST_PIN)   /* 复位低电平：复位RC522 */
#define RC522_SCK_H()   pin_hi(RC522_SCK_PIN)   /* SCK时钟高电平 */
#define RC522_SCK_L()   pin_lo(RC522_SCK_PIN)   /* SCK时钟低电平 */
#define RC522_MOSI_H()  pin_hi(RC522_MOSI_PIN)  /* MOSI主出高电平 */
#define RC522_MOSI_L()  pin_lo(RC522_MOSI_PIN)  /* MOSI主出低电平 */
#define RC522_MISO()    pin_read(RC522_MISO_PIN) /* 读取MISO数据 */

/* 
 * 软件SPI：CPOL=0, CPHA=0（模式0）
 * 时钟空闲低电平，数据在上升沿采样
 */
static uint8_t spi_rw(uint8_t data)
{
    uint8_t i, ret = 0;
    for(i=0;i<8;i++)
    {
        if(data & 0x80) RC522_MOSI_H(); else RC522_MOSI_L(); /* 发送MSB位 */
        data <<= 1; /* 左移准备下一位 */

        RC522_SCK_H();      /* 上升沿，RC522采样数据 */
        delay_us(1);

        ret <<= 1;          /* 接收数据左移 */
        if(RC522_MISO()) ret |= 1; /* 读取MISO数据位 */

        RC522_SCK_L();      /* 下降沿 */
        delay_us(1);
    }
    return ret; /* 返回接收到的字节 */
}

/* 
 * RC522寄存器写操作
 * 地址格式：addr[5:0]，写操作：bit7=0，左移1位
 * 寄存器地址需要左移1位，bit7为读写标志：0=写，1=读
 */
static void rc522_write_reg(uint8_t addr, uint8_t val)
{
    RC522_NSS_L();                          /* 片选使能 */
    spi_rw((addr<<1) & 0x7E);               /* 发送寄存器地址（写操作）*/
    spi_rw(val);                            /* 发送寄存器值 */
    RC522_NSS_H();                          /* 片选禁止 */
}

/* 
 * RC522寄存器读操作
 * 地址格式：addr[5:0]，读操作：bit7=1，左移1位
 */
static uint8_t rc522_read_reg(uint8_t addr)
{
    uint8_t val;
    RC522_NSS_L();                          /* 片选使能 */
    spi_rw(((addr<<1) & 0x7E) | 0x80);      /* 发送寄存器地址（读操作）*/
    val = spi_rw(0x00);                     /* 发送虚拟字节，读取寄存器值 */
    RC522_NSS_H();                          /* 片选禁止 */
    return val;
}

/* 
 * 设置寄存器位掩码（置位操作）
 * 将寄存器中指定的位设置为1
 */
static void rc522_set_bitmask(uint8_t reg, uint8_t mask)
{
    rc522_write_reg(reg, rc522_read_reg(reg) | mask);
}

/* 
 * 清除寄存器位掩码（清零操作）
 * 将寄存器中指定的位设置为0
 */
static void rc522_clear_bitmask(uint8_t reg, uint8_t mask)
{
    rc522_write_reg(reg, rc522_read_reg(reg) & (~mask));
}

/* 
 * 打开天线
 * 检查天线状态，如果未打开则打开天线
 * TxControlReg的bit0-1控制天线驱动
 */
static void rc522_antenna_on(void)
{
    uint8_t v = rc522_read_reg(TxControlReg);
    if((v & 0x03) != 0x03) rc522_set_bitmask(TxControlReg, 0x03);
}

/* 
 * 复位RC522
 * 发送复位命令，等待稳定
 */
static void rc522_reset(void)
{
    rc522_write_reg(CommandReg, PCD_RESETPHASE); /* 发送复位命令 */
    delay_ms(50);                               /* 等待复位完成 */
}

/* 
 * 计算CRC校验码
 * 为指定数据计算CRC16校验
 * 输出：out_l=CRC低字节，out_h=CRC高字节
 */
static uint8_t rc522_calculate_crc(uint8_t *data, uint8_t len, uint8_t *out_l, uint8_t *out_h)
{
    rc522_clear_bitmask(DivIrqReg, 0x04);   /* 清除CRC中断标志 */
    rc522_set_bitmask(FIFOLevelReg, 0x80);  /* 清空FIFO缓冲区 */
    
    for(uint8_t i=0;i<len;i++)              /* 将数据写入FIFO */
        rc522_write_reg(FIFODataReg, data[i]);
    
    rc522_write_reg(CommandReg, PCD_CALCCRC); /* 启动CRC计算 */
    
    /* 等待CRC计算完成，超时检测 */
    uint16_t i = 5000;
    while(i--)
    {
        uint8_t n = rc522_read_reg(DivIrqReg);
        if(n & 0x04) break;                /* CRC计算完成标志 */
    }
    if(i==0) return MI_ERR;                /* 超时返回错误 */
    
    *out_l = rc522_read_reg(CRCResultRegL); /* 读取CRC低字节 */
    *out_h = rc522_read_reg(CRCResultRegH); /* 读取CRC高字节 */
    return MI_OK;
}

/* 
 * RC522与卡片通信（核心函数）
 * 发送数据到卡片并接收响应
 * command: PCD命令（如PCD_TRANSCEIVE）
 * sendData: 发送数据缓冲区
 * sendLen: 发送数据长度
 * backData: 接收数据缓冲区
 * backLenBits: 接收到的数据位数（按位计算）
 */
static uint8_t rc522_to_card(uint8_t command, uint8_t *sendData, uint8_t sendLen,
                            uint8_t *backData, uint16_t *backLenBits)
{
    uint8_t status = MI_ERR;
    uint8_t irqEn = 0x00;      /* 中断使能设置 */
    uint8_t waitIRq = 0x00;    /* 等待中断标志 */
    
    if(command == PCD_TRANSCEIVE) { 
        irqEn = 0x77;          /* 使能收发中断 */
        waitIRq = 0x30;        /* 等待接收完成中断 */
    }
    
    /* 配置中断和FIFO */
    rc522_write_reg(ComIEnReg, irqEn | 0x80);
    rc522_clear_bitmask(ComIrqReg, 0x80);
    rc522_set_bitmask(FIFOLevelReg, 0x80); /* 清空FIFO */
    rc522_write_reg(CommandReg, PCD_IDLE); /* 进入空闲模式 */
    
    /* 将数据写入FIFO */
    for(uint8_t i=0;i<sendLen;i++) 
        rc522_write_reg(FIFODataReg, sendData[i]);
    
    /* 执行命令 */
    rc522_write_reg(CommandReg, command);
    if(command == PCD_TRANSCEIVE) 
        rc522_set_bitmask(BitFramingReg, 0x80); /* 启动收发 */
    
    /* 等待命令执行完成或超时 */
    uint16_t i = 2000;
    uint8_t n;
    do {
        n = rc522_read_reg(ComIrqReg);
        i--;
    } while(i && !(n & 0x01) && !(n & waitIRq));
    
    rc522_clear_bitmask(BitFramingReg, 0x80); /* 清除收发标志 */
    
    /* 处理执行结果 */
    if(i)  /* 未超时 */
    {
        uint8_t err = rc522_read_reg(ErrorReg);
        if(!(err & 0x1B)) /* 检查错误标志 */
        {
            status = MI_OK;
            if(n & 0x01) status = MI_NOTAGERR; /* 计时器中断：无卡片 */
            
            /* 如果是收发命令，读取接收数据 */
            if(command == PCD_TRANSCEIVE)
            {
                uint8_t fifoLevel = rc522_read_reg(FIFOLevelReg); /* FIFO数据量 */
                uint8_t lastBits = rc522_read_reg(ControlReg) & 0x07; /* 最后几位 */
                
                /* 计算接收到的总位数 */
                if(lastBits) *backLenBits = (fifoLevel - 1) * 8 + lastBits;
                else *backLenBits = fifoLevel * 8;
                
                /* 限制读取数量，防止溢出 */
                if(fifoLevel == 0) fifoLevel = 1;
                if(fifoLevel > 16) fifoLevel = 16;
                
                /* 从FIFO读取数据 */
                for(uint8_t i2=0;i2<fifoLevel;i2++) 
                    backData[i2] = rc522_read_reg(FIFODataReg);
            }
        }
    }
    return status;
}

/* 
 * 请求寻卡（REQA）
 * reqMode: 请求模式（PICC_REQA或PICC_WUPA）
 * tagType: 返回卡片类型
 */
static uint8_t rc522_request(uint8_t reqMode, uint8_t *tagType)
{
    uint8_t status;
    uint16_t backBits;
    rc522_write_reg(BitFramingReg, 0x07); /* 设置7位数据 */
    
    tagType[0] = reqMode; /* 发送请求命令 */
    status = rc522_to_card(PCD_TRANSCEIVE, tagType, 1, tagType, &backBits);
    
    if((status != MI_OK) || (backBits != 0x10)) 
        status = MI_ERR; /* 检查响应长度应为16位 */
    
    return status;
}

/* 
 * 防冲突检测
 * 获取卡片UID，支持多卡检测
 * serNum: 返回5字节序列号（4字节UID + 1字节校验）
 */
static uint8_t rc522_anticoll(uint8_t *serNum)
{
    uint8_t status;
    uint16_t backBits;
    uint8_t serNumCheck = 0;
    
    rc522_write_reg(BitFramingReg, 0x00); /* 清除位调整 */
    serNum[0] = PICC_ANTICOLL_CL1;        /* 防冲突命令 */
    serNum[1] = 0x20;                     /* NVB（有效位）=32 */
    status = rc522_to_card(PCD_TRANSCEIVE, serNum, 2, serNum, &backBits);
    
    if(status == MI_OK)
    {
        /* 计算校验和：前4字节异或等于第5字节 */
        for(uint8_t i=0;i<4;i++) 
            serNumCheck ^= serNum[i];
        if(serNumCheck != serNum[4]) 
            status = MI_ERR; /* 校验失败 */
    }
    return status;
}

/* 
 * 选择卡片
 * 根据UID选择特定卡片
 * serNum: 5字节序列号（4字节UID + 1字节校验）
 */
static uint8_t rc522_select(uint8_t *serNum)
{
    uint8_t buf[9];
    uint8_t crcL, crcH;
    uint16_t backLen;
    
    /* 构建选择命令帧 */
    buf[0] = PICC_SELECT_CL1;  /* 选择命令 */
    buf[1] = 0x70;             /* SEL + NVB */
    memcpy(&buf[2], serNum, 5); /* UID + BCC */
    
    /* 计算CRC */
    if(rc522_calculate_crc(buf, 7, &crcL, &crcH) != MI_OK) 
        return MI_ERR;
    buf[7] = crcL;
    buf[8] = crcH;
    
    uint8_t backData[3];
    uint8_t status = rc522_to_card(PCD_TRANSCEIVE, buf, 9, backData, &backLen);
    
    /* 成功选择后返回SAK（选择应答）应为24位 */
    if((status == MI_OK) && (backLen == 0x18)) 
        return MI_OK;
    
    return MI_ERR;
}

/* 
 * RC522模块初始化
 * 配置GPIO、复位、设置工作参数
 */
void HGQ_RC522_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 开启GPIO时钟 */
    RCC_AHB1PeriphClockCmd(RC522_GPIO_RCC, ENABLE);
    
    /* 配置输出引脚：SCK、MOSI、NSS、RST */
    GPIO_InitStructure.GPIO_Pin   = RC522_SCK_PIN | RC522_MOSI_PIN | 
                                    RC522_NSS_PIN | RC522_RST_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;    /* 输出模式 */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;    /* 推挽输出 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 高速 */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;     /* 上拉 */
    GPIO_Init(RC522_GPIO_PORT, &GPIO_InitStructure);
    
    /* 配置输入引脚：MISO */
    GPIO_InitStructure.GPIO_Pin  = RC522_MISO_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;      /* 输入模式 */
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;      /* 上拉 */
    GPIO_Init(RC522_GPIO_PORT, &GPIO_InitStructure);
    
    /* 初始化引脚状态 */
    RC522_NSS_H();  /* 片选禁止 */
    RC522_SCK_L();  /* 时钟低电平 */
    RC522_RST_H();  /* 复位高电平 */
    delay_ms(10);
    
    /* 硬件复位 */
    RC522_RST_L();  /* 复位低电平 */
    delay_ms(10);
    RC522_RST_H();  /* 释放复位 */
    delay_ms(10);
    
    rc522_reset();  /* 软件复位 */
    
    /* 配置RC522工作参数（推荐配置） */
    rc522_write_reg(TModeReg, 0x8D);        /* 定时器自动重载 */
    rc522_write_reg(TPrescalerReg, 0x3E);   /* 定时器预分频 */
    rc522_write_reg(TReloadRegL, 30);       /* 定时器重载值低字节 */
    rc522_write_reg(TReloadRegH, 0);        /* 定时器重载值高字节 */
    rc522_write_reg(TxASKReg, 0x40);        /* 100% ASK调制 */
    rc522_write_reg(ModeReg, 0x3D);         /* CRC初始值0x6363 */
    
    rc522_antenna_on();  /* 打开天线 */
}

/* 
 * 轮询并读取卡片UID
 * 寻卡->防冲突->选卡->读取UID
 * uid_buf: UID输出缓冲区（至少4字节）
 * uid_len: UID长度输出（通常为4）
 * 返回：MI_OK成功，MI_NOTAGERR无卡，MI_ERR错误
 */
uint8_t HGQ_RC522_PollUID(uint8_t *uid_buf, uint8_t *uid_len)
{
    uint8_t buf[16];
    uint8_t status;
    
    /* 步骤1: 寻卡（发送REQA命令）*/
    buf[0] = 0;
    status = rc522_request(PICC_REQA, buf); /* 寻卡请求 */
    if(status != MI_OK) 
        return MI_NOTAGERR; /* 无卡 */
    
    /* 步骤2: 防冲突（获取UID）*/
    memset(buf, 0, sizeof(buf));
    status = rc522_anticoll(buf); /* 防冲突获取UID */
    if(status != MI_OK) 
        return MI_ERR; /* 防冲突失败 */
    
    /* buf[0..4] = UID(4B) + BCC(校验) */
    
    /* 步骤3: 选卡（可选但建议，确认卡片可用）*/
    if(rc522_select(buf) != MI_OK) 
        return MI_ERR; /* 选卡失败 */
    
    /* 输出UID（前4字节）*/
    uid_buf[0] = buf[0];
    uid_buf[1] = buf[1];
    uid_buf[2] = buf[2];
    uid_buf[3] = buf[3];
    *uid_len = 4; /* Mifare 1K卡片UID为4字节 */
    
    return MI_OK;
}

/* 
 * 将UID转换为字符串格式
 * uid: UID字节数组
 * uid_len: UID长度
 * out: 输出字符串缓冲区
 * out_size: 缓冲区大小（建议>=40字节）
 * 输出格式："RC522 UID: xx xx xx xx" 或 "RC522: NO CARD"
 */
void HGQ_RC522_UIDToString(const uint8_t *uid, uint8_t uid_len, char *out, uint16_t out_size)
{
    if(out_size == 0) return;
    out[0] = '\0';
    
    if(uid_len == 0) {
        snprintf(out, out_size, "RC522: NO CARD");
        return;
    }
    
    /* 格式化输出："RC522 UID: xx xx xx xx" */
    char tmp[8];
    snprintf(out, out_size, "RC522 UID:");
    for(uint8_t i=0;i<uid_len;i++)
    {
        snprintf(tmp, sizeof(tmp), " %02X", uid[i]); /* 十六进制格式 */
        strncat(out, tmp, out_size - strlen(out) - 1);
    }
}
