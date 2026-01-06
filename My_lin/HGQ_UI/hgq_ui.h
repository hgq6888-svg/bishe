#ifndef __HGQ_UI_H
#define __HGQ_UI_H

#include "stm32f4xx.h"

typedef struct {
    /* 环境数据 */
    int temp_x10;       /* 温度放大10倍 */
    int humi;           /* 湿度 */
    int lux;            /* 光照 */
    
    /* 座位卡片数据 */
    char status[16];    /* 状态: 空闲/使用中/预约 */
    char user_str[20];  /* 用户: 姓名或UID */
    char reserve_t[16]; /* 预约时间: HH:MM */
    char start_t[16];   /* 开始时间: HH:MM */
    char remain_t[16];  /* 剩余时间: XX min */
    
    char area_seat[16]; /* 区域座位标题 */
    
    /* 控制状态 */
    int auto_mode;      /* 0:手动, 1:自动 */
    int light_on;       /* 0:关, 1:开 */
    int bri_target;     /* 目标亮度 0-100 */
    int esp_state;      /* 0:离线, 1:连接, 2:在线 */
} HGQ_UI_Data;

void HGQ_UI_Init(void);
void HGQ_UI_DrawFramework(void);
void HGQ_UI_Update(HGQ_UI_Data *d, const char *time_str);
int HGQ_UI_GetBrightnessNow(void);

/* 新增：重置缓存，强制刷新（用于关闭弹窗后） */
void HGQ_UI_ResetCache(void);
/* 新增：显示弹窗消息 */
void HGQ_UI_ShowPopup(const char *msg);

/* 触摸交互 */
u8 HGQ_UI_TouchBtn_Check(u16 x, u16 y); /* 新增：签到/签退按钮 (原位置0) */
u8 HGQ_UI_TouchBtn_Mode(u16 x, u16 y);  /* 模式切换 (原位置1) */
u8 HGQ_UI_TouchBtn_On(u16 x, u16 y);    /* 开灯 (原位置2) */
u8 HGQ_UI_TouchBtn_Off(u16 x, u16 y);   /* 关灯 (原位置3) */
u8 HGQ_UI_TouchBtn_BriUp(u16 x, u16 y);
u8 HGQ_UI_TouchBtn_BriDown(u16 x, u16 y);

#endif
