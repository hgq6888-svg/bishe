#include "touch_debug.h"
#include "lcd.h"
#include "touch.h"
#include "usart.h"
#include <stdio.h>

#define DOT_SIZE              3
#define PRINT_EVERY_N_CALLS   5
#define MOVE_THRESHOLD        2

static u8  s_div = 0;
static u16 s_last_x = 0xFFFF;
static u16 s_last_y = 0xFFFF;
static u8  s_last_pressed = 0;

/*
 * 你的现象：画竖线会变横线、画横线会变竖线
 * 说明 LCD_DrawLine 的 x/y 轴在底层被互换了。
 * 所以调试页里对 DrawLine / Fill 进行一次 (x,y) -> (y,x) 交换适配，
 * 让屏幕看到的十字方向正常。
 */
static void draw_cross_fix(u16 x, u16 y)
{
    // 目标：屏幕上显示正常的十字
    // 由于底层DrawLine轴互换，因此传参时交换(x,y)->(y,x)

    // 横线：y不变，x变化
    if (x >= 3 && (x + 3) < lcddev.width)
    {
        // (x-3,y) -> (x+3,y)，传参交换成：
        LCD_DrawLine(y, x - 3, y, x + 3);
    }

    // 竖线：x不变，y变化
    if (y >= 3 && (y + 3) < lcddev.height)
    {
        // (x,y-3) -> (x,y+3)，传参交换成：
        LCD_DrawLine(y - 3, x, y + 3, x);
    }

    // 中心点（用 Fill，也做交换）
    if (x < lcddev.width && y < lcddev.height)
    {
        LCD_Fill(y, x, y + 1, x + 1, RED);
    }
}

void TP_DebugPage_Init(void)
{
    LCD_Clear(BLACK);

    POINT_COLOR = WHITE;
    BACK_COLOR  = BLACK;

    LCD_DrawRectangle(0, 0, lcddev.width - 1, lcddev.height - 1);

    // 全英文，避免编码问题
    Show_Str(6, 6, lcddev.width - 12, 16, (u8*)"Touch Debug: draw point + UART print", 16, 0);
    Show_Str(6, 26, lcddev.width - 12, 16, (u8*)"Press screen to draw and show (x,y)", 16, 0);
    Show_Str(6, 46, lcddev.width - 12, 16, (u8*)"If offset: run TP_Adjust() once", 16, 0);

    POINT_COLOR = CYAN;
    LCD_DrawRectangle(6, 70, lcddev.width - 7, 120);

    POINT_COLOR = WHITE;
    BACK_COLOR  = BLACK;
    Show_Str(10, 74, lcddev.width - 20, 16, (u8*)"X:", 16, 0);
    Show_Str(10, 94, lcddev.width - 20, 16, (u8*)"Y:", 16, 0);

    s_last_x = 0xFFFF;
    s_last_y = 0xFFFF;
    s_last_pressed = 0;
    s_div = 0;
}

void TP_DebugPage_Task(void)
{
    u16 x = 0, y = 0;

    if (TP_Scan(0))
    {
        x = tp_dev.x[0];
        y = tp_dev.y[0];

        // 画点：由于 Fill 的轴也可能互换，这里同样做交换
        if (x < lcddev.width && y < lcddev.height)
        {
            LCD_Fill(y, x, y + DOT_SIZE - 1, x + DOT_SIZE - 1, WHITE);
            draw_cross_fix(x, y);
        }

        // 屏幕显示坐标（这个不需要交换，因为显示位置你看得见）
        LCD_Fill(40, 72, lcddev.width - 10, 88, BLACK);
        LCD_Fill(40, 92, lcddev.width - 10, 108, BLACK);

        POINT_COLOR = GREEN;
        BACK_COLOR  = BLACK;

        {
            char buf[24];
            sprintf(buf, "%u", (unsigned)x);
            Show_Str(40, 74, lcddev.width - 50, 16, (u8*)buf, 16, 0);

            sprintf(buf, "%u", (unsigned)y);
            Show_Str(40, 94, lcddev.width - 50, 16, (u8*)buf, 16, 0);
        }

        // 串口打印节流：首次按下/移动明显再打印
        s_div++;
        if (!s_last_pressed ||
            ( (x > s_last_x ? (x - s_last_x) : (s_last_x - x)) > MOVE_THRESHOLD ) ||
            ( (y > s_last_y ? (y - s_last_y) : (s_last_y - y)) > MOVE_THRESHOLD ))
        {
            if (s_div >= PRINT_EVERY_N_CALLS || !s_last_pressed)
            {
                s_div = 0;
                printf("TP: x=%u y=%u\r\n", (unsigned)x, (unsigned)y);
            }
        }

        s_last_x = x;
        s_last_y = y;
        s_last_pressed = 1;
    }
    else
    {
        s_last_pressed = 0;
        s_div = 0;
    }
}

