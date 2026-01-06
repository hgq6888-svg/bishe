#include "hgq_ui.h"
#include "lcd.h"
#include "text.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 颜色配置 */
#define UI_C_BG      0xE71C
#define UI_C_TOP     0x00A5
#define UI_C_CARD    WHITE
#define UI_C_TEXT    BLACK
#define UI_C_LABEL   GRAYBLUE
#define UI_C_ACCENT  0x051D
#define UI_C_OK      0x07E0
#define UI_C_WARN    0xF800
#define UI_C_BTN_OFF WHITE
#define UI_C_BTN_ON  0x051D
#define UI_C_POP_BG  WHITE
#define UI_C_POP_BD  BLACK

/* 布局常量 */
#define TOP_H        30
#define BOTTOM_H     40
#define CARD_Y_START 36
#define CARD_H       158
#define GAP          5

/* GBK 中文字符串常量 */
const u8 STR_TITLE[] = {0xD6,0xC7,0xC4,0xDC,0xD7,0xD4,0xCF,0xB0,0xCA,0xD2,0x00}; 
const u8 STR_ENV[]   = {0xBB,0xB7,0xBE,0xB3,0x00}; 
const u8 STR_SEAT[]  = {0xD7,0xF9,0xCE,0xBB,0x00}; 
const u8 STR_LIGHT[] = {0xB5,0xC6,0xB9,0xE2,0x00}; 

/* 环境标签 */
const u8 STR_T[] = {0xCE,0xC2,0xB6,0xC8,0x3A,0x00}; 
const u8 STR_H[] = {0xCA,0xAA,0xB6,0xC8,0x3A,0x00}; 
const u8 STR_L[] = {0xB9,0xE2,0xD5,0xD5,0x3A,0x00}; 

/* 座位标签 */
const u8 STR_STAT[]    = {0xD7,0xB4,0xCC,0xAC,0x3A,0x00}; 
const u8 STR_USER[]    = {0xD3,0xC3,0xBB,0xA7,0x3A,0x00}; 
const u8 STR_RES_T[]   = {0xD4,0xA4,0xD4,0xBC,0xCA,0xB1,0xBC,0xE4,0x3A,0x00}; 
const u8 STR_START_T[] = {0xBF,0xAA,0xCA,0xBC,0xCA,0xB1,0xBC,0xE4,0x3A,0x00}; 
const u8 STR_REM_T[]   = {0xCA,0xA3,0xD3,0xE0,0xCA,0xB1,0xBC,0xE4,0x3A,0x00}; 

/* 单位 */
const u8 STR_UNIT_C[]   = {0xA1,0xE6,0x00}; 
const u8 STR_UNIT_PCT[] = {0x25,0x00};      
const u8 STR_UNIT_LUX[] = "Lux";

/* 底部按钮 (修改) */
const u8 STR_CHECKIN[]  = {0xC7,0xA9,0xB5,0xBD,0x00}; // 签到
const u8 STR_CHECKOUT[] = {0xC7,0xA9,0xCD,0xCB,0x00}; // 签退
const u8 STR_MODE_M[]   = {0xCA,0xD6,0xB6,0xAF,0x00}; // 手动
const u8 STR_MODE_A[]   = {0xD7,0xD4,0xB6,0xAF,0x00}; // 自动
const u8 STR_ON[]       = {0xBF,0xAA,0xB5,0xC6,0x00};
const u8 STR_OFF[]      = {0xB9,0xD8,0xB5,0xC6,0x00};

const u8 STR_ESP_CON[] = {0xC1,0xAC,0xBD,0xD3,0xD6,0xD0,0x00};
const u8 STR_ESP_OK[]  = {0xD4,0xDA,0xCF,0xDF,0x00};
const u8 STR_ESP_OFF[] = {0xC0,0xEB,0xCF,0xDF,0x00};

/* 缓存 */
static HGQ_UI_Data s_cache = {0};
static char s_time_cache[10] = "";
static u8 s_first_run = 1;
static int s_bri_now = 0;

/* 按钮坐标 */
static u16 btn_up_x1, btn_up_y1, btn_up_x2, btn_up_y2;
static u16 btn_dn_x1, btn_dn_y1, btn_dn_x2, btn_dn_y2;

void HGQ_UI_Init(void) {
    HGQ_UI_ResetCache();
}

void HGQ_UI_ResetCache(void) {
    s_first_run = 1; 
    s_bri_now = 0;
    memset(&s_cache, 0, sizeof(s_cache));
    s_cache.esp_state = -1; 
    s_time_cache[0] = 0;
}

int HGQ_UI_GetBrightnessNow(void) { return s_bri_now; }

/* 辅助绘图 */
static void Show_Label(u16 x, u16 y, const u8* str) {
    POINT_COLOR = UI_C_LABEL; BACK_COLOR = UI_C_CARD;
    Show_Str(x, y, 200, 16, (u8*)str, 16, 0);
}
static void Show_Value_Str(u16 x, u16 y, u16 w, char* str, u16 color) {
    LCD_Fill(x, y, x+w, y+16, UI_C_CARD);
    POINT_COLOR = color; BACK_COLOR = UI_C_CARD;
    Show_Str(x, y, w, 16, (u8*)str, 16, 0);
}
static void DrawBottomBtn(int idx, const u8* str, int active) {
    u16 w = (lcddev.width - 5*GAP) / 4;
    u16 x = GAP + idx * (w + GAP);
    u16 y = lcddev.height - BOTTOM_H + GAP;
    u16 h = BOTTOM_H - 2*GAP;
    u16 color = active ? UI_C_BTN_ON : UI_C_BTN_OFF;
    u16 tc = active ? WHITE : BLACK;
    LCD_Fill(x, y, x+w, y+h, color);
    POINT_COLOR = 0xCE79; LCD_DrawRectangle(x, y, x+w, y+h);
    POINT_COLOR = tc; BACK_COLOR = color;
    /* 计算文字居中 */
    u16 str_w = 0;
    while(str[str_w]) str_w++; // 简单估算
    str_w = (str_w > 6) ? 32 : 16; // 粗略
    Show_Str(x + (w-32)/2, y + (h-16)/2, 60, 16, (u8*)str, 16, 0);
}
static void DrawPlusMinusBtn(u16 x1, u16 y1, u16 x2, u16 y2, char* symbol) {
    LCD_Fill(x1, y1, x2, y2, UI_C_BTN_OFF);
    POINT_COLOR = 0xCE79; LCD_DrawRectangle(x1, y1, x2, y2);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_BTN_OFF;
    LCD_ShowString(x1+(x2-x1-8)/2, y1+(y2-y1-16)/2, 16, 16, 16, (u8*)symbol);
}

/* 弹窗功能 */
void HGQ_UI_ShowPopup(const char *msg) {
    u16 w = 240, h = 100;
    u16 x = (lcddev.width - w) / 2;
    u16 y = (lcddev.height - h) / 2;
    
    // 绘制弹窗背景和边框
    LCD_Fill(x, y, x+w, y+h, UI_C_POP_BG);
    POINT_COLOR = UI_C_POP_BD;
    LCD_DrawRectangle(x, y, x+w, y+h);
    LCD_DrawRectangle(x+1, y+1, x+w-1, y+h-1);
    
    // 显示消息
    POINT_COLOR = UI_C_TEXT; BACK_COLOR = UI_C_POP_BG;
    Show_Str(x + 20, y + 42, 200, 16, (u8*)msg, 16, 0);
}

/* ================== 1. 静态框架绘制 ================== */
void HGQ_UI_DrawFramework(void) {
    u16 x, y;
    LCD_Clear(UI_C_BG);
    LCD_Fill(0, 0, lcddev.width, TOP_H, UI_C_TOP);
    
    /* 1. 环境卡片 */
    x = GAP; LCD_Fill(x, CARD_Y_START, x+100, CARD_Y_START+CARD_H, UI_C_CARD);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_CARD;
    Show_Str(x+5, CARD_Y_START+5, 80, 16, (u8*)STR_ENV, 16, 0);
    
    y = CARD_Y_START + 30;
    Show_Label(x+5, y, STR_T); 
    Show_Label(x+5, y+30, STR_H); 
    Show_Label(x+5, y+60, STR_L);
    
    /* 2. 座位卡片 */
    x = GAP + 100 + GAP; 
    LCD_Fill(x, CARD_Y_START, x+120, CARD_Y_START+CARD_H, UI_C_CARD);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_CARD;
    Show_Str(x+5, CARD_Y_START+5, 80, 16, (u8*)STR_SEAT, 16, 0);
    
    y = CARD_Y_START + 30;
    Show_Label(x+5, y, STR_STAT);       
    Show_Label(x+5, y+25, STR_USER);    
    Show_Label(x+5, y+50, STR_RES_T);   
    Show_Label(x+5, y+75, STR_START_T); 
    Show_Label(x+5, y+100, STR_REM_T);  

    /* 3. 灯光卡片 */
    x = GAP + 100 + GAP + 120 + GAP; 
    u16 w_light = lcddev.width - x - GAP;
    LCD_Fill(x, CARD_Y_START, lcddev.width-GAP, CARD_Y_START+CARD_H, UI_C_CARD);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_CARD;
    Show_Str(x+5, CARD_Y_START+5, 80, 16, (u8*)STR_LIGHT, 16, 0);
    
    u16 cx = x + w_light / 2;
    btn_up_x1 = cx - 20; btn_up_x2 = cx + 20;
    btn_up_y1 = CARD_Y_START + 35; btn_up_y2 = btn_up_y1 + 30;
    DrawPlusMinusBtn(btn_up_x1, btn_up_y1, btn_up_x2, btn_up_y2, "+");
    
    btn_dn_x1 = cx - 20; btn_dn_x2 = cx + 20;
    btn_dn_y1 = CARD_Y_START + 105; btn_dn_y2 = btn_dn_y1 + 30;
    DrawPlusMinusBtn(btn_dn_x1, btn_dn_y1, btn_dn_x2, btn_dn_y2, "-");
    
    /* 顶部标题 */
    POINT_COLOR = WHITE; BACK_COLOR = UI_C_TOP;
    Show_Str(10, 7, 100, 16, (u8*)STR_TITLE, 16, 0);
    
    // 按钮初始状态不绘制，在Update中绘制
}

/* ================== 2. 动态刷新 ================== */
void HGQ_UI_Update(HGQ_UI_Data *d, const char *time_str) {
    u16 x_env = GAP; u16 x_seat = GAP + 100 + GAP; u16 y_base = CARD_Y_START + 30;
    
    POINT_COLOR = UI_C_TEXT; BACK_COLOR = UI_C_CARD;

    /* 顶部区域 */
    if(s_first_run) {
        POINT_COLOR = WHITE; BACK_COLOR = UI_C_TOP;
        Show_Str(lcddev.width-140, 7, 80, 16, (u8*)d->area_seat, 16, 0);
    }
    if(s_first_run || strcmp(time_str, s_time_cache) != 0) {
        POINT_COLOR = WHITE; BACK_COLOR = UI_C_TOP;
        LCD_Fill(115, 7, 115+40, 7+16, UI_C_TOP);
        LCD_ShowString(115, 7, 40, 16, 16, (u8*)time_str);
        strncpy(s_time_cache, time_str, sizeof(s_time_cache));
    }

    /* 1. 环境数据更新 */
    if(s_first_run || d->temp_x10 != s_cache.temp_x10) {
        LCD_Fill(x_env+50, y_base, x_env+95, y_base+16, UI_C_CARD);
        LCD_ShowNum(x_env+50, y_base, d->temp_x10/10, 2, 16);
        LCD_ShowString(x_env+66, y_base, 8, 16, 16, (u8*)".");
        LCD_ShowNum(x_env+74, y_base, d->temp_x10%10, 1, 16);
        Show_Str(x_env+82, y_base, 16, 16, (u8*)STR_UNIT_C, 16, 0);
    }
    if(s_first_run || d->humi != s_cache.humi) {
        LCD_Fill(x_env+50, y_base+30, x_env+95, y_base+46, UI_C_CARD);
        LCD_ShowNum(x_env+50, y_base+30, d->humi, (d->humi>99?3:2), 16);
        Show_Str(x_env+50+(d->humi>99?24:16), y_base+30, 16, 16, (u8*)STR_UNIT_PCT, 16, 0);
    }
    if(s_first_run || d->lux != s_cache.lux) {
        LCD_Fill(x_env+50, y_base+60, x_env+100, y_base+76, UI_C_CARD);
        if(d->lux < 0) {
            POINT_COLOR = RED; LCD_ShowString(x_env+50, y_base+60, 40, 16, 16, (u8*)"Err");
        } else {
            POINT_COLOR = UI_C_TEXT;
            u8 len = (d->lux>9999?5:(d->lux>999?4:(d->lux>99?3:(d->lux>9?2:1))));
            LCD_ShowNum(x_env+50, y_base+60, d->lux, len, 16);
            Show_Str(x_env+50+len*8, y_base+60, 24, 16, (u8*)STR_UNIT_LUX, 16, 0);
        }
    }

    /* 2. 座位数据更新 */
    if(s_first_run || strcmp(d->status, s_cache.status) != 0) {
        u16 c = UI_C_OK; 
        if(strstr(d->status, "In") || strstr(d->status, "Busy")) c = UI_C_ACCENT; 
        else if(strstr(d->status, "Res") || strstr(d->status, "Book")) c = UI_C_WARN; 
        Show_Value_Str(x_seat+50, y_base, 60, d->status, c);
    }
    if(s_first_run || strcmp(d->user_str, s_cache.user_str) != 0) {
        Show_Value_Str(x_seat+50, y_base+25, 60, d->user_str, UI_C_TEXT);
    }
    if(s_first_run || strcmp(d->reserve_t, s_cache.reserve_t) != 0) {
        Show_Value_Str(x_seat+70, y_base+50, 45, d->reserve_t, UI_C_TEXT);
    }
    if(s_first_run || strcmp(d->start_t, s_cache.start_t) != 0) {
        Show_Value_Str(x_seat+70, y_base+75, 45, d->start_t, UI_C_TEXT);
    }
    if(s_first_run || strcmp(d->remain_t, s_cache.remain_t) != 0) {
        Show_Value_Str(x_seat+70, y_base+100, 45, d->remain_t, UI_C_ACCENT);
    }

    /* 3. 灯光与按钮状态 */
    if(s_bri_now < d->bri_target) s_bri_now += 2;
    if(s_bri_now > d->bri_target) s_bri_now -= 2;
    if(abs(s_bri_now - d->bri_target) < 2) s_bri_now = d->bri_target;
    
    if(s_first_run || s_bri_now != s_cache.bri_target || d->light_on != s_cache.light_on) {
        u16 x_light = GAP + 100 + GAP + 120 + GAP;
        u16 w_light = lcddev.width - x_light - GAP;
        u16 cx = x_light + w_light / 2;
        u16 val_y = CARD_Y_START + 75;
        LCD_Fill(cx-15, val_y, cx+15, val_y+16, UI_C_CARD);
        POINT_COLOR = d->light_on ? UI_C_ACCENT : 0xCE79; BACK_COLOR = UI_C_CARD;
        LCD_ShowNum(cx-12, val_y, s_bri_now, 3, 16);
    }
    
    /* 底部按钮刷新 */
    /* 按钮0: 根据状态显示签到/签退 */
    u8 is_in_use = (strstr(d->status, "In") != NULL);
    if(s_first_run || is_in_use != (strstr(s_cache.status, "In")!=NULL)) {
        DrawBottomBtn(0, is_in_use ? STR_CHECKOUT : STR_CHECKIN, 0); 
    }
    
    if(s_first_run || d->auto_mode != s_cache.auto_mode) {
        DrawBottomBtn(1, d->auto_mode ? STR_MODE_A : STR_MODE_M, d->auto_mode);
    }
    if(s_first_run || d->light_on != s_cache.light_on) {
        DrawBottomBtn(2, STR_ON,   d->light_on);
        DrawBottomBtn(3, STR_OFF, !d->light_on);
    }
    
    /* 4. 网络状态图标 */
    if(s_first_run || d->esp_state != s_cache.esp_state) {
        u16 c = UI_C_WARN; const u8* s = STR_ESP_CON;
        if(d->esp_state == 2) { c = UI_C_OK; s = STR_ESP_OK; }
        else if(d->esp_state == 0) { c = RED; s = STR_ESP_OFF; }
        LCD_Fill(lcddev.width-60, 10, lcddev.width-54, 16, c);
        POINT_COLOR = WHITE; BACK_COLOR = UI_C_TOP;
        LCD_Fill(lcddev.width-50, 5, lcddev.width, 25, UI_C_TOP);
        Show_Str(lcddev.width-50, 7, 50, 16, (u8*)s, 16, 0);
    }
    
    s_cache = *d; s_first_run = 0;
}

/* 触控判定辅助函数 */
static u8 IsIn(u16 x, u16 y, u16 x1, u16 y1, u16 x2, u16 y2) { return (x>=x1 && x<=x2 && y>=y1 && y<=y2); }
static u8 IsBtn(u16 x, u16 y, int idx) {
    u16 w = (lcddev.width - 5*GAP) / 4;
    u16 x1 = GAP + idx * (w + GAP);
    u16 y1 = lcddev.height - BOTTOM_H + GAP;
    return IsIn(x, y, x1, y1, x1+w, y1+(BOTTOM_H - 2*GAP));
}

u8 HGQ_UI_TouchBtn_Check(u16 x, u16 y) { return IsBtn(x, y, 0); }
u8 HGQ_UI_TouchBtn_Mode(u16 x, u16 y)  { return IsBtn(x, y, 1); }
u8 HGQ_UI_TouchBtn_On(u16 x, u16 y)    { return IsBtn(x, y, 2); }
u8 HGQ_UI_TouchBtn_Off(u16 x, u16 y)   { return IsBtn(x, y, 3); }
u8 HGQ_UI_TouchBtn_BriUp(u16 x, u16 y)   { return IsIn(x, y, btn_up_x1, btn_up_y1, btn_up_x2, btn_up_y2); }
u8 HGQ_UI_TouchBtn_BriDown(u16 x, u16 y) { return IsIn(x, y, btn_dn_x1, btn_dn_y1, btn_dn_x2, btn_dn_y2); }
