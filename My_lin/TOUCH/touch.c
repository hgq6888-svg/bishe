#include "touch.h"
#include "lcd.h"
#include "delay.h"
#include "stdlib.h"
#include "math.h"
#include "24cxx.h"

// 触摸屏设备结构体实例化，初始化函数指针和变量
_m_tp_dev tp_dev =
{
    TP_Init,    // 初始化函数
    TP_Scan,    // 扫描函数
    TP_Adjust,  // 校准函数
    0,          // x坐标数组
    0,          // y坐标数组
    0,          // 状态字
    0,          // x方向缩放因子
    0,          // y方向缩放因子
    0,          // x偏移量
    0,          // y偏移量
    0,          // 触摸类型
};

// 电阻屏读取X/Y坐标的命令（默认竖屏）
u8 CMD_RDX = 0XD0;  // 读取X坐标命令
u8 CMD_RDY = 0X90;  // 读取Y坐标命令

// SPI写一个字节数据到触摸芯片
void TP_Write_Byte(u8 num)
{
    u8 count = 0;
    for (count = 0; count < 8; count++)
    {
        if (num & 0x80) TDIN = 1;  // 发送最高位
        else TDIN = 0;
        num <<= 1;                  // 左移准备下一位
        TCLK = 0;                  // 时钟下降沿
        delay_us(1);               // 延时确保稳定
        TCLK = 1;                  // 时钟上升沿，数据采样
    }
}

// 从触摸芯片读取ADC值
u16 TP_Read_AD(u8 CMD)
{
    u8 count = 0;
    u16 Num = 0;
    TCLK = 0;                // 拉低时钟
    TDIN = 0;                // 拉低数据线
    TCS = 0;                 // 选中触摸芯片（片选有效）
    TP_Write_Byte(CMD);      // 发送命令字
    delay_us(6);             // ADS7846最大转换时间6us
    TCLK = 0;                // 清除BUSY标志
    delay_us(1);
    TCLK = 1;                // 给一个时钟
    delay_us(1);
    TCLK = 0;
    for (count = 0; count < 16; count++)  // 读取16位数据（高12位有效）
    {
        Num <<= 1;           // 左移准备接收下一位
        TCLK = 0;            // 时钟下降沿，数据输出
        delay_us(1);
        TCLK = 1;
        if (DOUT) Num++;     // 读取数据位
    }
    Num >>= 4;               // 只取高12位有效数据
    TCS = 1;                 // 释放片选
    return Num;
}

// 读取一个坐标值（X或Y），带滤波处理
#define READ_TIMES 5   // 采样次数
#define LOST_VAL 1     // 丢弃最大值和最小值的个数
u16 TP_Read_XOY(u8 xy)
{
    u16 i, j;
    u16 buf[READ_TIMES];
    u16 sum = 0;
    u16 temp;
    for (i = 0; i < READ_TIMES; i++)
        buf[i] = TP_Read_AD(xy);  // 多次采样
    for (i = 0; i < READ_TIMES - 1; i++)  // 升序排序（冒泡）
    {
        for (j = i + 1; j < READ_TIMES; j++)
        {
            if (buf[i] > buf[j])
            {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }
    sum = 0;
    for (i = LOST_VAL; i < READ_TIMES - LOST_VAL; i++)  // 去掉头尾
        sum += buf[i];
    temp = sum / (READ_TIMES - 2 * LOST_VAL);  // 取平均值
    return temp;
}

// 读取X和Y坐标
u8 TP_Read_XY(u16 *x, u16 *y)
{
    u16 xtemp, ytemp;
    xtemp = TP_Read_XOY(CMD_RDX);  // 读取X
    ytemp = TP_Read_XOY(CMD_RDY);  // 读取Y
    // 可在此处添加范围判断（如小于100则无效）
    *x = xtemp;
    *y = ytemp;
    return 1;  // 读取成功
}

// 增强版坐标读取：连续两次采样，偏差在允许范围内才接受
#define ERR_RANGE 50  // 允许的误差范围
u8 TP_Read_XY2(u16 *x, u16 *y)
{
    u16 x1, y1;
    u16 x2, y2;
    u8 flag;
    flag = TP_Read_XY(&x1, &y1);  // 第一次采样
    if (flag == 0) return 0;
    flag = TP_Read_XY(&x2, &y2);  // 第二次采样
    if (flag == 0) return 0;
    // 判断两次采样是否在允许误差范围内
    if (((x2 <= x1 && x1 < x2 + ERR_RANGE) || (x1 <= x2 && x2 < x1 + ERR_RANGE)) &&
        ((y2 <= y1 && y1 < y2 + ERR_RANGE) || (y1 <= y2 && y2 < y1 + ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;  // 取平均值
        *y = (y1 + y2) / 2;
        return 1;
    }
    else return 0;  // 误差过大，丢弃
}

// 画一个触摸点（用于校准）
void TP_Drow_Touch_Point(u16 x, u16 y, u16 color)
{
    POINT_COLOR = color;
    LCD_DrawLine(x - 12, y, x + 13, y);   // 横线
    LCD_DrawLine(x, y - 12, x, y + 13);   // 竖线
    LCD_DrawPoint(x + 1, y + 1);          // 四个角点
    LCD_DrawPoint(x - 1, y + 1);
    LCD_DrawPoint(x + 1, y - 1);
    LCD_DrawPoint(x - 1, y - 1);
    LCD_Draw_Circle(x, y, 6);             // 中心圆
}

// 画一个大点（2x2像素）
void TP_Draw_Big_Point(u16 x, u16 y, u16 color)
{
    POINT_COLOR = color;
    LCD_DrawPoint(x, y);      // 中心
    LCD_DrawPoint(x + 1, y);
    LCD_DrawPoint(x, y + 1);
    LCD_DrawPoint(x + 1, y + 1);
}

// 触摸按键扫描函数
u8 TP_Scan(u8 tp)
{
    if (PEN == 0)  // 有触摸按下（电阻屏检测）
    {
        if (tp)  // 物理坐标模式（用于校准）
            TP_Read_XY2(&tp_dev.x[0], &tp_dev.y[0]);
        else if (TP_Read_XY2(&tp_dev.x[0], &tp_dev.y[0]))  // 读取屏幕坐标
        {
            // 转换为屏幕坐标（应用校准参数）
            tp_dev.x[0] = tp_dev.xfac * tp_dev.x[0] + tp_dev.xoff;
            tp_dev.y[0] = tp_dev.yfac * tp_dev.y[0] + tp_dev.yoff;
        }
        if ((tp_dev.sta & TP_PRES_DOWN) == 0)  // 之前未被按下
        {
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;  // 标记按下
            tp_dev.x[4] = tp_dev.x[0];  // 记录首次按下坐标
            tp_dev.y[4] = tp_dev.y[0];
        }
    }
    else  // 无触摸
    {
        if (tp_dev.sta & TP_PRES_DOWN)  // 之前是按下的，现在松开
            tp_dev.sta &= ~(1 << 7);    // 清除按下标记
        else  // 之前就是松开的
        {
            tp_dev.x[4] = 0;
            tp_dev.y[4] = 0;
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
        }
    }
    return tp_dev.sta & TP_PRES_DOWN;  // 返回当前状态
}

// 保存校准参数到EEPROM
#define SAVE_ADDR_BASE 40  // 存储基地址
void TP_Save_Adjdata(void)
{
    s32 temp;
    // 保存X方向缩放因子（放大1e8倍存储）
    temp = tp_dev.xfac * 100000000;
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE, temp, 4);
    // 保存Y方向缩放因子
    temp = tp_dev.yfac * 100000000;
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE + 4, temp, 4);
    // 保存X偏移量
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE + 8, tp_dev.xoff, 2);
    // 保存Y偏移量
    AT24CXX_WriteLenByte(SAVE_ADDR_BASE + 10, tp_dev.yoff, 2);
    // 保存触摸类型
    AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 12, tp_dev.touchtype);
    // 写入校准标记
    temp = 0X0A;
    AT24CXX_WriteOneByte(SAVE_ADDR_BASE + 13, temp);
}

// 从EEPROM读取校准参数
//u8 TP_Get_Adjdata(void)
//{
//    s32 tempfac;
//    tempfac = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 13);  // 读取标记
//    if (tempfac == 0X0A)  // 已校准
//    {
//        tempfac = AT24CXX_ReadLenByte(SAVE_ADDR_BASE, 4);
//        tp_dev.xfac = (float)tempfac / 100000000;  // 恢复X缩放因子
//        tempfac = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 4, 4);
//        tp_dev.yfac = (float)tempfac / 100000000;  // 恢复Y缩放因子
//        tp_dev.xoff = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 8, 2);  // X偏移
//        tp_dev.yoff = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 10, 2); // Y偏移
//        tp_dev.touchtype = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 12);  // 触摸类型
//        // 根据类型设置读取命令
//        if (tp_dev.touchtype)  // X、Y方向相反
//        {
//            CMD_RDX = 0X90;
//            CMD_RDY = 0XD0;
//        }
//        else  // 正常方向
//        {
//            CMD_RDX = 0XD0;
//            CMD_RDY = 0X90;
//        }
//        return 1;  // 成功读取
//    }
//    return 0;  // 未校准
//}
// 从EEPROM读取校准参数（修正版：不再用保存的touchtype覆盖当前LCD方向）
// 关键点：
// 1) EEPROM里保存的touchtype可能是你上次“竖屏校准”存的，后来你横屏运行就会错
// 2) 这里强制：touchtype的BIT0始终跟随当前 lcddev.dir（横/竖）
// 3) CMD_RDX/CMD_RDY 只根据 BIT0 判断（别用 if(tp_dev.touchtype) 这种写法）
u8 TP_Get_Adjdata(void)
{
    s32 tempfac;
    u8  saved_type;

    tempfac = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 13);  // 读取标记
    if (tempfac != 0X0A) return 0;                       // 未校准

    tempfac = AT24CXX_ReadLenByte(SAVE_ADDR_BASE, 4);
    tp_dev.xfac = (float)tempfac / 100000000;

    tempfac = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 4, 4);
    tp_dev.yfac = (float)tempfac / 100000000;

    tp_dev.xoff = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 8, 2);
    tp_dev.yoff = AT24CXX_ReadLenByte(SAVE_ADDR_BASE + 10, 2);

    saved_type = AT24CXX_ReadOneByte(SAVE_ADDR_BASE + 12);

    // 只保留BIT7(电容/电阻标记)，BIT0强制跟随当前LCD方向
    tp_dev.touchtype = (saved_type & 0x80) | (lcddev.dir & 0x01);

    // 读命令只看BIT0：0=竖屏，1=横屏（换轴）
    if (tp_dev.touchtype & 0x01)
    {
        CMD_RDX = 0X90;
        CMD_RDY = 0XD0;
    }
    else
    {
        CMD_RDX = 0XD0;
        CMD_RDY = 0X90;
    }

    return 1;
}


// 校准提示字符串
u8* const TP_REMIND_MSG_TBL = "Please use the stylus click the cross on the screen...";

// 显示校准过程中的坐标和参数
void TP_Adj_Info_Show(u16 x0, u16 y0, u16 x1, u16 y1, u16 x2, u16 y2, u16 x3, u16 y3, u16 fac)
{
    POINT_COLOR = RED;
    // 显示四个点的坐标
    LCD_ShowString(40, 160, lcddev.width, lcddev.height, 16, "x1:");
    // ... 其他坐标显示
    LCD_ShowNum(40 + 24, 160, x0, 4, 16);  // 显示数值
    // ... 显示其他坐标和fac值
    LCD_ShowNum(40 + 56, 240, fac, 3, 16);  // 显示比例因子（应在95~105之间）
}

// 触摸屏校准主函数
void TP_Adjust(void)
{
    u16 pos_temp[4][2];  // 存储四个校准点的坐标
    u8 cnt = 0;
    u16 d1, d2;
    u32 tem1, tem2;
    double fac;
    u16 outtime = 0;
    cnt = 0;
    POINT_COLOR = BLUE;
    BACK_COLOR = WHITE;
    LCD_Clear(WHITE);  // 清屏
    POINT_COLOR = RED;
    LCD_ShowString(40, 40, 160, 100, 16, (u8*)TP_REMIND_MSG_TBL);  // 显示提示
    TP_Drow_Touch_Point(20, 20, RED);  // 画第一个点
    tp_dev.sta = 0;     // 清除触发信号
    tp_dev.xfac = 0;    // 清除校准标记
    while (1)
    {
        tp_dev.scan(1);  // 扫描物理坐标
        if ((tp_dev.sta & 0xc0) == TP_CATH_PRES)  // 按下并松开
        {
            outtime = 0;
            tp_dev.sta &= ~(1 << 6);  // 标记已处理
            pos_temp[cnt][0] = tp_dev.x[0];  // 存储坐标
            pos_temp[cnt][1] = tp_dev.y[0];
            cnt++;
            switch (cnt)
            {
                case 1:  // 第一个点完成
                    TP_Drow_Touch_Point(20, 20, WHITE);  // 清除
                    TP_Drow_Touch_Point(lcddev.width - 20, 20, RED);  // 画第二个点
                    break;
                case 2:  // 第二个点
                    TP_Drow_Touch_Point(lcddev.width - 20, 20, WHITE);
                    TP_Drow_Touch_Point(20, lcddev.height - 20, RED);  // 第三个点
                    break;
                case 3:  // 第三个点
                    TP_Drow_Touch_Point(20, lcddev.height - 20, WHITE);
                    TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, RED);  // 第四个点
                    break;
                case 4:  // 四个点采集完成
                    // 计算对边距离并验证矩形
                    // 1. 计算边1-2和3-4的距离比例
                    tem1 = abs(pos_temp[0][0] - pos_temp[1][0]);
                    tem2 = abs(pos_temp[0][1] - pos_temp[1][1]);
                    d1 = sqrt(tem1 * tem1 + tem2 * tem2);  // 1-2距离
                    tem1 = abs(pos_temp[2][0] - pos_temp[3][0]);
                    tem2 = abs(pos_temp[2][1] - pos_temp[3][1]);
                    d2 = sqrt(tem1 * tem1 + tem2 * tem2);  // 3-4距离
                    fac = (float)d1 / d2;
                    if (fac < 0.95 || fac > 1.05 || d1 == 0 || d2 == 0)  // 不合格
                    {
                        cnt = 0;
                        TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, WHITE);
                        TP_Drow_Touch_Point(20, 20, RED);  // 重新开始
                        TP_Adj_Info_Show(pos_temp[0][0], pos_temp[0][1], pos_temp[1][0], pos_temp[1][1],
                                         pos_temp[2][0], pos_temp[2][1], pos_temp[3][0], pos_temp[3][1], fac * 100);
                        continue;
                    }
                    // 2. 计算边1-3和2-4的距离比例
                    // ... 类似计算
                    // 3. 计算对角线距离比例
                    // ... 类似计算
                    // 所有验证通过，计算校准参数
                    tp_dev.xfac = (float)(lcddev.width - 40) / (pos_temp[1][0] - pos_temp[0][0]);  // X缩放因子
                    tp_dev.xoff = (lcddev.width - tp_dev.xfac * (pos_temp[1][0] + pos_temp[0][0])) / 2;  // X偏移
                    tp_dev.yfac = (float)(lcddev.height - 40) / (pos_temp[2][1] - pos_temp[0][1]);  // Y缩放因子
                    tp_dev.yoff = (lcddev.height - tp_dev.yfac * (pos_temp[2][1] + pos_temp[0][1])) / 2;  // Y偏移
                    // 检查参数是否合理（方向是否反了）
                    if (abs(tp_dev.xfac) > 2 || abs(tp_dev.yfac) > 2)
                    {
                        cnt = 0;
                        TP_Drow_Touch_Point(lcddev.width - 20, lcddev.height - 20, WHITE);
                        TP_Drow_Touch_Point(20, 20, RED);
                        LCD_ShowString(40, 26, lcddev.width, lcddev.height, 16, "TP Need readjust!");
                        tp_dev.touchtype = !tp_dev.touchtype;  // 翻转方向
                        if (tp_dev.touchtype)  // 调整命令
                        {
                            CMD_RDX = 0X90;
                            CMD_RDY = 0XD0;
                        }
                        else
                        {
                            CMD_RDX = 0XD0;
                            CMD_RDY = 0X90;
                        }
                        continue;
                    }
                    // 校准完成
                    POINT_COLOR = BLUE;
                    LCD_Clear(WHITE);
                    LCD_ShowString(35, 110, lcddev.width, lcddev.height, 16, "Touch Screen Adjust OK!");
                    delay_ms(1000);
                    TP_Save_Adjdata();  // 保存参数
                    LCD_Clear(WHITE);
                    return;
            }
        }
        delay_ms(10);
        outtime++;
        if (outtime > 1000)  // 10秒超时
        {
            TP_Get_Adjdata();  // 尝试读取已有参数
            break;
        }
    }
}

// 触摸屏初始化
u8 TP_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    tp_dev.touchtype = 0;  // 默认电阻屏竖屏
    tp_dev.touchtype |= lcddev.dir & 0X01;  // 根据LCD方向设置
    
    // 电容屏检测（根据LCD型号）
    if (lcddev.id == 0x7796)  // 3.5寸电容屏（GT系列）
    {
        if (GT9147_Init() == 0)  // 初始化GT9147成功
        {
            tp_dev.scan = GT9147_Scan;  // 指向电容屏扫描函数
            tp_dev.touchtype |= 0X80;   // 标记为电容屏
            return 0;
        }
    }
    if(lcddev.id == 0X5510 || lcddev.id == 0X9806 || lcddev.id == 0X4342 || lcddev.id == 0X4384 || lcddev.id == 0X1018 )  // 4.3寸电容屏
    {
        if (GT9147_Init() == 0)  // GT9147
            tp_dev.scan = GT9147_Scan;
        else  // OTT2001A
        {
            OTT2001A_Init();
            tp_dev.scan = OTT2001A_Scan;
        }
        tp_dev.touchtype |= 0X80;
        return 0;
    }
    else if(lcddev.id==0X1963 || lcddev.id == 0X7084 || lcddev.id == 0X7016)  // 7寸电容屏
    {
        if (!FT5206_Init())  // FT5206
            tp_dev.scan = FT5206_Scan;
        else  // GT9147
        {
            GT9147_Init();
            tp_dev.scan = GT9147_Scan;
        }
        tp_dev.touchtype |= 0X80;
        tp_dev.touchtype |= lcddev.dir & 0X01;  // 横竖屏
        return 0;
    }
    else  // 电阻屏
    {
        // 初始化GPIO（SPI模拟）
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOF, ENABLE);
        // PB1、PB2输入（PEN、DOUT）
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1 | GPIO_Pin_2;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
        GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
        // PB0、PC13、PF11输出（TCLK、TCS、TDIN）
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
        GPIO_Init(GPIOB, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
        GPIO_Init(GPIOC, &GPIO_InitStructure);
        GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
        GPIO_Init(GPIOF, &GPIO_InitStructure);
        
        TP_Read_XY(&tp_dev.x[0], &tp_dev.y[0]);  // 首次读取测试
        AT24CXX_Init();  // 初始化EEPROM
        if (TP_Get_Adjdata())  // 已有校准参数
            return 0;
        else  // 需要校准
        {
            LCD_Clear(WHITE);
            TP_Adjust();  // 执行校准
            TP_Save_Adjdata();
        }
        TP_Get_Adjdata();  // 读取参数
    }
    return 1;  // 电阻屏且经过校准
}
