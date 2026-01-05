#ifndef __TOUCH_H__
#define __TOUCH_H__
#include "sys.h"
#include "ott2001a.h"  // 电容触摸芯片OTT2001A
#include "gt9147.h"    // 电容触摸芯片GT9147
#include "ft5206.h"    // 电容触摸芯片FT5206

// 触摸屏状态定义
#define TP_PRES_DOWN 0x80  // 触屏被按下（BIT7）
#define TP_CATH_PRES 0x40  // 有按键按下了（BIT6）

#define CT_MAX_TOUCH 5     // 电容屏支持的最大触摸点数

// 触摸屏控制结构体
typedef struct
{
    u8 (*init)(void);      // 初始化函数指针
    u8 (*scan)(u8);        // 扫描函数指针：0-屏幕坐标，1-物理坐标
    void (*adjust)(void);  // 校准函数指针
    
    u16 x[CT_MAX_TOUCH];   // X坐标数组（电容屏支持多点）
    u16 y[CT_MAX_TOUCH];   // Y坐标数组
    
    u8  sta;               // 状态字：
                           // BIT7：按下(1)/松开(0)
                           // BIT6：有按键按下(1)
                           // BIT4~0：电容屏按下点数
    
    // 校准参数（电阻屏专用）
    float xfac;   // X方向缩放因子
    float yfac;   // Y方向缩放因子
    short xoff;   // X偏移量
    short yoff;   // Y偏移量
    
    // 触摸类型
    // BIT0：0-竖屏，1-横屏（坐标方向）
    // BIT7：0-电阻屏，1-电容屏
    u8 touchtype;
} _m_tp_dev;

extern _m_tp_dev tp_dev;  // 全局触摸设备结构体

// 电阻屏引脚定义（模拟SPI）
#define PEN     PBin(1)   // 触摸笔中断/状态引脚
#define DOUT    PBin(2)   // SPI MISO（数据输入）
#define TDIN    PFout(11) // SPI MOSI（数据输出）
#define TCLK    PBout(0)  // SPI SCK（时钟）
#define TCS     PCout(13) // SPI CS（片选）

// 电阻屏函数声明
void TP_Write_Byte(u8 num);                     // 写一个字节
u16 TP_Read_AD(u8 CMD);                         // 读取ADC值
u16 TP_Read_XOY(u8 xy);                         // 读取单坐标（滤波）
u8 TP_Read_XY(u16 *x, u16 *y);                  // 读取双坐标
u8 TP_Read_XY2(u16 *x, u16 *y);                 // 增强版双坐标读取
void TP_Drow_Touch_Point(u16 x, u16 y, u16 color); // 画校准点
void TP_Draw_Big_Point(u16 x, u16 y, u16 color);   // 画大点
void TP_Save_Adjdata(void);                     // 保存校准参数
u8 TP_Get_Adjdata(void);                        // 读取校准参数
void TP_Adjust(void);                           // 校准函数
void TP_Adj_Info_Show(u16 x0, u16 y0, u16 x1, u16 y1, u16 x2, u16 y2, u16 x3, u16 y3, u16 fac); // 显示校准信息

// 通用函数（电阻屏/电容屏共用）
u8 TP_Scan(u8 tp);  // 扫描函数
u8 TP_Init(void);   // 初始化函数

#endif
