#include "hgq_ui.h"
#include "lcd.h"
#include "text.h"
#include <string.h>
#include <stdio.h>

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

/* 布局 */
#define TOP_H        30
#define BOTTOM_H     40
#define CARD_Y_START 36
#define CARD_H       158
#define GAP          5

/* 中文 GBK */
const u8 STR_TITLE[] = {0xD6,0xC7,0xC4,0xDC,0xD7,0xD4,0xCF,0xB0,0xCA,0xD2,0x00};
const u8 STR_ENV[]   = {0xBB,0xB7,0xBE,0xB3,0x00};
const u8 STR_SEAT[]  = {0xD7,0xF9,0xCE,0xBB,0x00};
const u8 STR_LIGHT[] = {0xB5,0xC6,0xB9,0xE2,0x00};
const u8 STR_T[] = {0xCE,0xC2,0xB6,0xC8,0x3A,0x00};
const u8 STR_H[] = {0xCA,0xAA,0xB6,0xC8,0x3A,0x00};
const u8 STR_L[] = {0xB9,0xE2,0xD5,0xD5,0x3A,0x00};
const u8 STR_USED[] = {0xCA,0xB9,0xD3,0xC3,0x3A,0x00};
const u8 STR_STAT[] = {0xD7,0xB4,0xCC,0xAC,0x3A,0x00};
const u8 STR_NEXT[] = {0xCF,0xC2,0xB4,0xCE,0x3A,0x00};
const u8 STR_MANU[] = {0xCA,0xD6,0xB6,0xAF,0x00};
const u8 STR_AUTO[] = {0xD7,0xD4,0xB6,0xAF,0x00};
const u8 STR_ON[]   = {0xBF,0xAA,0xB5,0xC6,0x00};
const u8 STR_OFF[]  = {0xB9,0xD8,0xB5,0xC6,0x00};
const u8 STR_ST_FREE[] = {0xBF,0xD5,0xCF,0xD0,0x00};
const u8 STR_ST_BUSY[] = {0xCA,0xB9,0xD3,0xC3,0x00};
const u8 STR_ST_RES[]  = {0xD4,0xA4,0xD4,0xBC,0x00};
const u8 STR_ESP_CON[] = {0xC1,0xAC,0xBD,0xD3,0xD6,0xD0,0x00};
const u8 STR_ESP_OK[]  = {0xD4,0xDA,0xCF,0xDF,0x00};
const u8 STR_ESP_OFF[] = {0xC0,0xEB,0xCF,0xDF,0x00};

/* 缓存 */
static HGQ_UI_Data s_cache = {0};
static u8 s_first_run = 1;
static int s_bri_now = 0;

/* 按钮区域记录 */
static u16 btn_up_x1, btn_up_y1, btn_up_x2, btn_up_y2;
static u16 btn_dn_x1, btn_dn_y1, btn_dn_x2, btn_dn_y2;

void HGQ_UI_Init(void) {
    s_first_run = 1; s_bri_now = 0;
    s_cache.esp_state = -1; s_cache.auto_mode = -1; s_cache.light_on = -1;
}
int HGQ_UI_GetBrightnessNow(void) { return s_bri_now; }

static void Show_Label(u16 x, u16 y, const u8* str) {
    POINT_COLOR = UI_C_LABEL; BACK_COLOR = UI_C_CARD;
    Show_Str(x, y, 200, 16, (u8*)str, 16, 0);
}
static void Show_Value_Num(u16 x, u16 y, int num) {
    LCD_Fill(x, y, x+30, y+16, UI_C_CARD);
    POINT_COLOR = UI_C_TEXT; BACK_COLOR = UI_C_CARD;
    LCD_ShowNum(x, y, num, (num>99?3:(num>9?2:1)), 16);
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
    Show_Str(x + (w-32)/2, y + (h-16)/2, 60, 16, (u8*)str, 16, 0);
}

static void DrawPlusMinusBtn(u16 x1, u16 y1, u16 x2, u16 y2, char* symbol) {
    LCD_Fill(x1, y1, x2, y2, UI_C_BTN_OFF);
    POINT_COLOR = 0xCE79; LCD_DrawRectangle(x1, y1, x2, y2);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_BTN_OFF;
    LCD_ShowString(x1+(x2-x1-8)/2, y1+(y2-y1-16)/2, 16, 16, 16, (u8*)symbol);
}

/* ========== 1. 静态框架 ========== */
void HGQ_UI_DrawFramework(void) {
    u16 x, y;
    LCD_Clear(UI_C_BG);
    LCD_Fill(0, 0, lcddev.width, TOP_H, UI_C_TOP);
    
    /* 环境卡片 */
    x = GAP; LCD_Fill(x, CARD_Y_START, x+100, CARD_Y_START+CARD_H, UI_C_CARD);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_CARD;
    Show_Str(x+5, CARD_Y_START+5, 80, 16, (u8*)STR_ENV, 16, 0);
    y = CARD_Y_START + 30;
    Show_Label(x+5, y, STR_T); Show_Label(x+5, y+30, STR_H); Show_Label(x+5, y+60, STR_L);
    
    /* 座位卡片 */
    x = GAP + 100 + GAP; LCD_Fill(x, CARD_Y_START, x+120, CARD_Y_START+CARD_H, UI_C_CARD);
    POINT_COLOR = UI_C_ACCENT; BACK_COLOR = UI_C_CARD;
    Show_Str(x+5, CARD_Y_START+5, 80, 16, (u8*)STR_SEAT, 16, 0);
    y = CARD_Y_START + 30;
    Show_Label(x+5, y, STR_USED); Show_Label(x+5, y+30, STR_STAT); Show_Label(x+5, y+60, STR_NEXT);

    /* 灯光卡片 */
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
    
    POINT_COLOR = WHITE; BACK_COLOR = UI_C_TOP;
    Show_Str(10, 7, 100, 16, (u8*)STR_TITLE, 16, 0);
    
    DrawBottomBtn(0, STR_MANU, 1); DrawBottomBtn(1, STR_AUTO, 0);
    DrawBottomBtn(2, STR_ON, 1);   DrawBottomBtn(3, STR_OFF, 0);
}

/* ========== 2. 动态刷新 ========== */
void HGQ_UI_Update(HGQ_UI_Data *d, const char *time_hm, const char *weekday) {
    u16 x_env = GAP; u16 x_seat = GAP + 100 + GAP; u16 y_base = CARD_Y_START + 30;
    
    POINT_COLOR = UI_C_TEXT; BACK_COLOR = UI_C_CARD;

    if(s_first_run) {
        POINT_COLOR = WHITE; BACK_COLOR = UI_C_TOP;
        /* 显示 A区-18号 */
        Show_Str(lcddev.width-140, 7, 80, 16, (u8*)d->area_seat, 16, 0);
    }

    if(s_first_run || d->temp_x10 != s_cache.temp_x10) {
        LCD_Fill(x_env+50, y_base, x_env+95, y_base+16, UI_C_CARD);
        LCD_ShowNum(x_env+50, y_base, d->temp_x10/10, 2, 16);
        LCD_ShowString(x_env+66, y_base, 8, 16, 16, (u8*)".");
        LCD_ShowNum(x_env+74, y_base, d->temp_x10%10, 1, 16);
    }
    if(s_first_run || d->humi != s_cache.humi) Show_Value_Num(x_env+50, y_base+30, d->humi);
    
    /* 光照显示优化 */
    if(s_first_run || d->lux != s_cache.lux) {
        if(d->lux < 0) {
            /* 传感器错误显示 */
            LCD_Fill(x_env+50, y_base+60, x_env+95, y_base+76, UI_C_CARD);
            POINT_COLOR = RED; 
            LCD_ShowString(x_env+50, y_base+60, 40, 16, 16, (u8*)"Err");
        } else {
            Show_Value_Num(x_env+50, y_base+60, d->lux);
        }
    }

    if(s_first_run || d->use_min != s_cache.use_min) Show_Value_Num(x_seat+50, y_base, d->use_min); 
    if(s_first_run || strcmp(d->status, s_cache.status) != 0) {
        const u8* st = STR_ST_FREE; u16 c = UI_C_OK;
        if(strcmp(d->status, "InUse") == 0) { st = STR_ST_BUSY; c = UI_C_ACCENT; }
        else if(strcmp(d->status, "Reserved") == 0) { st = STR_ST_RES; c = UI_C_WARN; }
        Show_Value_Str(x_seat+50, y_base+30, 60, (char*)st, c);
    }
    
    /* 亮度显示 */
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
    
    if(s_first_run || d->auto_mode != s_cache.auto_mode || d->light_on != s_cache.light_on) {
        DrawBottomBtn(0, STR_MANU, !d->auto_mode);
        DrawBottomBtn(1, STR_AUTO, d->auto_mode);
        DrawBottomBtn(2, STR_ON,   d->light_on);
        DrawBottomBtn(3, STR_OFF, !d->light_on);
    }
    
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

static u8 IsIn(u16 x, u16 y, u16 x1, u16 y1, u16 x2, u16 y2) { return (x>=x1 && x<=x2 && y>=y1 && y<=y2); }
static u8 IsBtn(u16 x, u16 y, int idx) {
    u16 w = (lcddev.width - 5*GAP) / 4;
    u16 x1 = GAP + idx * (w + GAP);
    u16 y1 = lcddev.height - BOTTOM_H + GAP;
    return IsIn(x, y, x1, y1, x1+w, y1+(BOTTOM_H - 2*GAP));
}

u8 HGQ_UI_TouchBtn_Manual(u16 x, u16 y) { return IsBtn(x, y, 0); }
u8 HGQ_UI_TouchBtn_Auto(u16 x, u16 y)   { return IsBtn(x, y, 1); }
u8 HGQ_UI_TouchBtn_On(u16 x, u16 y)     { return IsBtn(x, y, 2); }
u8 HGQ_UI_TouchBtn_Off(u16 x, u16 y)    { return IsBtn(x, y, 3); }

u8 HGQ_UI_TouchBtn_BriUp(u16 x, u16 y)   { return IsIn(x, y, btn_up_x1, btn_up_y1, btn_up_x2, btn_up_y2); }
u8 HGQ_UI_TouchBtn_BriDown(u16 x, u16 y) { return IsIn(x, y, btn_dn_x1, btn_dn_y1, btn_dn_x2, btn_dn_y2); }
u8 HGQ_UI_TouchLightBar(u16 x, u16 y, u8 *p) { return 0; }
