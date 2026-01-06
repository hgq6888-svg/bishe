#ifndef __HGQ_UI_H
#define __HGQ_UI_H

#include "stm32f4xx.h"

typedef struct {
    int temp_x10;
    int humi;
    int lux;
    int use_min;
    char status[16];
    char next_time[16];
    char area_seat[16]; /* 区域座位 */
    
    int auto_mode;      /* 0:手动, 1:自动 */
    int light_on;       /* 0:关, 1:开 */
    int bri_target;     /* 目标亮度 0-100 */
    int esp_state;      /* 0:离线, 1:连接, 2:在线 */
} HGQ_UI_Data;

void HGQ_UI_Init(void);
void HGQ_UI_DrawFramework(void);
void HGQ_UI_Update(HGQ_UI_Data *d, const char *time_hm, const char *weekday);
int HGQ_UI_GetBrightnessNow(void);

/* 底部按钮 */
u8 HGQ_UI_TouchBtn_Manual(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_Auto(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_On(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_Off(u16 x, u16 y);

/* 亮度加减按钮 */
u8 HGQ_UI_TouchBtn_BriUp(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_BriDown(u16 x, u16 y);
u8 HGQ_UI_TouchLightBar(u16 x, u16 y, u8 *p); /* 兼容性保留 */

#endif
