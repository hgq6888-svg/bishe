#ifndef __HGQ_UI_H
#define __HGQ_UI_H

#include "stm32f4xx.h"
#include "lcd.h"
#include "delay.h"

/* UI 数据 */
typedef struct
{
    /* 环境 */
    int temp_x10;          /* 235=23.5℃ */
    int humi;              /* % */
    int lux;               /* lux */

    /* 座位 */
    char area_seat[16];    /* "A区-15号" */
    int  use_min;          /* 分钟 */
    char status[12];       /* "正常" */
    char next_time[8];     /* "16:25" */

    /* 灯光 */
    u8  auto_mode;         /* 0=手动 1=自动 */
    u8  light_on;          /* 0=关灯 1=开灯 */
    int bri_target;        /* 目标亮度 0~100 */

    /* ESP-01 状态：0=离线 1=连接中 2=在线 */
    u8 esp_state;

} HGQ_UI_Data;

/* UI */
void HGQ_UI_Init(void);
void HGQ_UI_DrawFramework(void);
void HGQ_UI_Update(HGQ_UI_Data *d, const char *time_hm, const char *weekday);

/* 触摸按钮命中判定（命中返回1，否则0） */
u8 HGQ_UI_TouchBtn_Manual(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_Auto(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_On(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_Off(u16 x, u16 y);

/* 触摸亮度条（手动模式用）
   - 命中亮度条返回1，并把 out_percent 填成 0~100
   - 未命中返回0
*/
u8 HGQ_UI_TouchLightBar(u16 x, u16 y, u8 *out_percent);

/* 获取当前动画亮度（用于你输出PWM/发ESP） */
int HGQ_UI_GetBrightnessNow(void);

#endif
