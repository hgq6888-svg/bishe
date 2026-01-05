#include "hgq_ui.h"
#include "text.h"
#include <string.h>

/* ========== 主题颜色 ========== */
#define HGQ_UI_C_BG        LGRAY
#define HGQ_UI_C_TOP       DARKBLUE
#define HGQ_UI_C_CARD      WHITE
#define HGQ_UI_C_LINE      LGRAYBLUE
#define HGQ_UI_C_TEXT      BLACK
#define HGQ_UI_C_SUBTEXT   GRAYBLUE
#define HGQ_UI_C_ACCENT    GBLUE
#define HGQ_UI_C_OK        GREEN
#define HGQ_UI_C_WARN      YELLOW

/* ========== 横屏布局（ILI9341 320x240） ========== */
#define HGQ_UI_TOP_H       28
#define HGQ_UI_BOTTOM_H    40
#define HGQ_UI_GAP         6
#define HGQ_UI_PAD         6
#define HGQ_UI_FONT        16
#define HGQ_UI_LINE_H      20

/* ========== 动画状态 ========== */
static int s_bri_now = 0;
static int s_blink   = 0;

/* ========== 记录亮度条矩形（给触摸用） ========== */
static u16 s_bar_x1, s_bar_y1, s_bar_x2, s_bar_y2;

/* ========== UI 缓存（减少重复全量绘制以提升响应） ========== */
static HGQ_UI_Data s_cache;
static u8 s_cache_init = 0;
static char s_cache_time_hm[8];
static char s_cache_weekday[8];
static int s_last_bri_draw = -1;
static int s_last_bri_text = -1;
static u8 s_last_bar_enabled = 0xFF;

/* ===========================================================
   GBK 提示词（不写中文常量，防 Keil/ARMCC 编码问题）
   =========================================================== */
/* 顶部：ESP 状态 */
static const u8 STR_ESP01[]       = {'E','S','P','-','0','1',0};
static const u8 STR_ESP_ONLINE[]  = {0xD4,0xDA,0xCF,0xDF,0x00};           /* 在线 */
static const u8 STR_ESP_OFFLINE[] = {0xC0,0xEB,0xCF,0xDF,0x00};           /* 离线 */
static const u8 STR_ESP_CONN[]    = {0xC1,0xAC,0xBD,0xD3,0xD6,0xD0,0x00}; /* 连接中 */

/* 三卡标题 */
static const u8 STR_ENV[]   = {0xBB,0xB7,0xBE,0xB3,0x00};                 /* 环境 */
static const u8 STR_SEAT[]  = {0xD7,0xF9,0xCE,0xBB,0x00};                 /* 座位 */
static const u8 STR_LIGHT[] = {0xB5,0xC6,0xB9,0xE2,0x00};                 /* 灯光 */

/* 环境短标签 */
static const u8 STR_T[] = {0xCE,0xC2,0xB6,0xC8,0x00};                     /* 温度 */
static const u8 STR_H[] = {0xCA,0xAA,0xB6,0xC8,0x00};                     /* 湿度 */
static const u8 STR_L[] = {0xB9,0xE2,0xD5,0xD5,0x00};                     /* 光照 */

/* 座位短标签 */
static const u8 STR_USED[] = {0xCA,0xB9,0xD3,0xC3,0x00};                  /* 使用 */
static const u8 STR_STAT[] = {0xD7,0xB4,0xCC,0xAC,0x00};                  /* 状态 */
static const u8 STR_NEXT[] = {0xCF,0xC2,0xB4,0xCE,0x00};                  /* 下次 */
static const u8 STR_HOUR[] = {0xD0,0xA1,0xCA,0xB1,0x00};                  /* 小时 */

/* 模式/开关 */
static const u8 STR_AUTO[]   = {0xD7,0xD4,0xB6,0xAF,0x00};                /* 自动 */
static const u8 STR_MANU[]   = {0xCA,0xD6,0xB6,0xAF,0x00};                /* 手动 */
static const u8 STR_ON[]     = {0xBF,0xAA,0xB5,0xC6,0x00};                /* 开灯 */
static const u8 STR_OFF[]    = {0xB9,0xD8,0xB5,0xC6,0x00};                /* 关灯 */

/* ========== 内部：Show_Str 包装 + GBK 按宽度截断 ========== */
static void HGQ_UI_ShowStr(u16 x,u16 y,u16 w,u16 h,const u8*str,u8 size,u8 mode)
{
    Show_Str(x,y,w,h,(u8*)str,size,mode);
}

static void HGQ_UI_ShowStr_GBK_Limit(u16 x,u16 y,u16 w,u16 h,const u8*str,u8 size,u8 mode)
{
    u16 max_chars, i, src, dst;
    static u8 buf[64];

    if(size == 0) return;
    max_chars = (u16)(w / size);
    if(max_chars > 20) max_chars = 20;

    i = 0; src = 0; dst = 0;
    while(str[src] != 0)
    {
        if(i >= max_chars) break;

        if(str[src] < 0x80)
        {
            buf[dst++] = str[src++];
            i++;
        }
        else
        {
            if(str[src+1] == 0) break;
            buf[dst++] = str[src++];
            buf[dst++] = str[src++];
            i++;
        }
        if(dst >= sizeof(buf)-1) break;
    }
    buf[dst] = 0;
    Show_Str(x,y,w,h,buf,size,mode);
}

/* ========== 内部：画卡片 ========== */
static void HGQ_UI_DrawCard(int x1,int y1,int x2,int y2)
{
    LCD_Fill(x1,y1,x2,y2,HGQ_UI_C_CARD);
    POINT_COLOR = HGQ_UI_C_LINE;
    LCD_DrawRectangle(x1,y1,x2,y2);
}

static void HGQ_UI_ClearLine(int x1, int y, int w, int h)
{
    if(w <= 0 || h <= 0) return;
    LCD_Fill(x1, y, x1 + w - 1, y + h - 1, HGQ_UI_C_CARD);
}

/* ========== 自动亮度映射（根据 lux -> 10~100） ========== */
static int HGQ_UI_AutoBriFromLux(int lux)
{
    int bri;

    if(lux < 0) lux = 0;
    if(lux > 1000) lux = 1000;

    /* 0lux->100%，1000lux->10% 线性映射 */
    bri = 100 - (lux * 90) / 1000;

    if(bri < 10) bri = 10;
    if(bri > 100) bri = 100;
    return bri;
}

/* ========== 顶部栏：ESP-01 状态 ========== */
static void HGQ_UI_DrawTopBar(const HGQ_UI_Data *d, const char *time_hm, const char *weekday)
{
    int w;
    u16 block_w, block_x, seat_x, seat_w;
    const u8 *st_txt;
    u16 dot_color;

    w = lcddev.width;

    LCD_Fill(0, 0, w-1, HGQ_UI_TOP_H-1, HGQ_UI_C_TOP);

    POINT_COLOR = WHITE;
    BACK_COLOR  = HGQ_UI_C_TOP;

    LCD_ShowString(6, 6, 60, 16, 16, (u8*)time_hm);
    Show_Str(70, 6, 50, 16, (u8*)weekday, 16, 0);

    /* 右侧状态块 */
    block_w = 120;
    block_x = (w > block_w) ? (u16)(w - block_w) : 0;

    st_txt = STR_ESP_OFFLINE;
    dot_color = RED;
    if(d->esp_state == 2) { st_txt = STR_ESP_ONLINE; dot_color = HGQ_UI_C_OK; }
    else if(d->esp_state == 1) { st_txt = STR_ESP_CONN; dot_color = HGQ_UI_C_WARN; }

    /* ESP-01 */
    HGQ_UI_ShowStr(block_x + 2, 6, 56, 16, STR_ESP01, 16, 0);
    /* 状态文字 */
    HGQ_UI_ShowStr_GBK_Limit(block_x + 58, 6, (u16)(block_w - 58 - 12), 16, st_txt, 16, 0);

    /* 指示点：连接中闪烁，其它常亮 */
    if(d->esp_state == 1)
    {
        s_blink ^= 1;
        if(s_blink) LCD_Fill(w-8, 9, w-4, 13, dot_color);
        else        LCD_Fill(w-8, 9, w-4, 13, HGQ_UI_C_TOP);
    }
    else
    {
        LCD_Fill(w-8, 9, w-4, 13, dot_color);
    }

    /* 中间座位号：限制宽度到右侧块之前 */
    seat_x = 130;
    seat_w = (block_x > seat_x + 6) ? (u16)(block_x - seat_x - 6) : 0;
    if(seat_w > 20)
    {
        POINT_COLOR = WHITE;
        BACK_COLOR  = HGQ_UI_C_TOP;
        HGQ_UI_ShowStr_GBK_Limit(seat_x, 6, seat_w, 16, (u8*)d->area_seat, 16, 0);
    }
}

/* ========== 亮度条（垂直） ========== */
static void HGQ_UI_DrawBrightnessBar(int x1,int y1,int x2,int y2,int percent, u8 enabled)
{
    int h, fill_h;

    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;

    /* 背景 */
    LCD_Fill(x1,y1,x2,y2,HGQ_UI_C_CARD);
    POINT_COLOR = HGQ_UI_C_LINE;
    LCD_DrawRectangle(x1,y1,x2,y2);

    h = (y2 - y1 - 2);
    if(h <= 0) return;

    fill_h = (h * percent) / 100;

    /* 未填充 */
    LCD_Fill(x1+1, y1+1, x2-1, y2-1-fill_h, WHITE);

    /* 填充：可拖动时用强调色；不可拖动/关灯时用灰一些（用边框色代替） */
    if(enabled) LCD_Fill(x1+1, y2-fill_h, x2-1, y2-1, HGQ_UI_C_ACCENT);
    else        LCD_Fill(x1+1, y2-fill_h, x2-1, y2-1, HGQ_UI_C_LINE);
}

/* ========== 底部按钮（4个：手动/自动/开灯/关灯） ========== */
static void HGQ_UI_DrawBottomButtons(int selected_manual, int selected_auto, int light_on)
{
    int w, h;
    int y1, y2;
    int btn_w, x, i;
    const u8 *names[4];

    w = lcddev.width;
    h = lcddev.height;

    y1 = h - HGQ_UI_BOTTOM_H;
    y2 = h - 1;

    names[0] = STR_MANU;
    names[1] = STR_AUTO;
    names[2] = STR_ON;
    names[3] = STR_OFF;

    LCD_Fill(0, y1, w-1, y2, HGQ_UI_C_BG);

    btn_w = (w - HGQ_UI_GAP*5) / 4;
    x = HGQ_UI_GAP;

    for(i=0;i<4;i++)
    {
        int bx1, bx2, by1, by2;
        int active;

        bx1 = x;
        bx2 = x + btn_w;
        by1 = y1 + 6;
        by2 = y2 - 6;

        active = 0;
        if(i==0 && selected_manual) active = 1;
        if(i==1 && selected_auto)   active = 1;
        if(i==2 && light_on)        active = 1;
        if(i==3 && !light_on)       active = 1;

        if(active) LCD_Fill(bx1,by1,bx2,by2,HGQ_UI_C_ACCENT);
        else       LCD_Fill(bx1,by1,bx2,by2,WHITE);

        POINT_COLOR = HGQ_UI_C_LINE;
        LCD_DrawRectangle(bx1,by1,bx2,by2);

        POINT_COLOR = active ? WHITE : BLACK;
        BACK_COLOR  = active ? HGQ_UI_C_ACCENT : WHITE;

        HGQ_UI_ShowStr_GBK_Limit((u16)(bx1+6), (u16)(by1+9), (u16)(btn_w-12), 16, names[i], 16, 0);

        x += btn_w + HGQ_UI_GAP;
    }
}

/* ========== 对外：触摸判定（按钮矩形与绘制一致） ========== */
static void HGQ_UI_GetBtnRect(u8 idx, u16 *x1, u16 *y1, u16 *x2, u16 *y2)
{
    u16 w, h;
    u16 bar_y1, bar_y2;
    u16 btn_w;

    w = (u16)lcddev.width;
    h = (u16)lcddev.height;

    bar_y1 = (u16)(h - HGQ_UI_BOTTOM_H);
    bar_y2 = (u16)(h - 1);

    btn_w = (u16)((w - HGQ_UI_GAP * 5) / 4);

    *x1 = (u16)(HGQ_UI_GAP + idx * (btn_w + HGQ_UI_GAP));
    *x2 = (u16)(*x1 + btn_w);
    *y1 = (u16)(bar_y1 + 6);
    *y2 = (u16)(bar_y2 - 6);
}

static u8 HGQ_UI_InRect(u16 x,u16 y,u16 x1,u16 y1,u16 x2,u16 y2)
{
    if(x>=x1 && x<=x2 && y>=y1 && y<=y2) return 1;
    return 0;
}

u8 HGQ_UI_TouchBtn_Manual(u16 x, u16 y)
{
    u16 x1,y1,x2,y2;
    HGQ_UI_GetBtnRect(0,&x1,&y1,&x2,&y2);
    return HGQ_UI_InRect(x,y,x1,y1,x2,y2);
}

u8 HGQ_UI_TouchBtn_Auto(u16 x, u16 y)
{
    u16 x1,y1,x2,y2;
    HGQ_UI_GetBtnRect(1,&x1,&y1,&x2,&y2);
    return HGQ_UI_InRect(x,y,x1,y1,x2,y2);
}

u8 HGQ_UI_TouchBtn_On(u16 x, u16 y)
{
    u16 x1,y1,x2,y2;
    HGQ_UI_GetBtnRect(2,&x1,&y1,&x2,&y2);
    return HGQ_UI_InRect(x,y,x1,y1,x2,y2);
}

u8 HGQ_UI_TouchBtn_Off(u16 x, u16 y)
{
    u16 x1,y1,x2,y2;
    HGQ_UI_GetBtnRect(3,&x1,&y1,&x2,&y2);
    return HGQ_UI_InRect(x,y,x1,y1,x2,y2);
}

/* 触摸亮度条：命中返回亮度百分比 */
u8 HGQ_UI_TouchLightBar(u16 x, u16 y, u8 *out_percent)
{
    int percent;
    int bar_h;

    if(out_percent == 0) return 0;
    if(!HGQ_UI_InRect(x,y,s_bar_x1,s_bar_y1,s_bar_x2,s_bar_y2)) return 0;

    bar_h = (int)(s_bar_y2 - s_bar_y1);
    if(bar_h <= 2) { *out_percent = 0; return 1; }

    /* 顶部=100，底部=0（从下往上） */
    percent = (int)(s_bar_y2 - y) * 100 / bar_h;
    if(percent < 0) percent = 0;
    if(percent > 100) percent = 100;

    *out_percent = (u8)percent;
    return 1;
}

/* ========== 对外 API ========== */
void HGQ_UI_Init(void)
{
    s_bri_now = 0;
    s_blink   = 0;
    s_bar_x1 = s_bar_y1 = s_bar_x2 = s_bar_y2 = 0;
}

int HGQ_UI_GetBrightnessNow(void)
{
    return s_bri_now;
}

/* 画静态框架（横屏三卡非等分） */
void HGQ_UI_DrawFramework(void)
{
    int w, h;
    int y_top, y_bottom;
    int env_w, seat_w;
    int x1,x2,x3,x4,x5,x6;

    w = lcddev.width;   /* 320 */
    h = lcddev.height;  /* 240 */

    y_top    = HGQ_UI_TOP_H + HGQ_UI_GAP;
    y_bottom = h - HGQ_UI_BOTTOM_H - HGQ_UI_GAP;

    env_w  = 108;
    seat_w = 132;

    x1 = HGQ_UI_GAP;
    x2 = x1 + env_w - 1;

    x3 = x2 + HGQ_UI_GAP + 1;
    x4 = x3 + seat_w - 1;

    x5 = x4 + HGQ_UI_GAP + 1;
    x6 = w - HGQ_UI_GAP - 1;

    LCD_Clear(HGQ_UI_C_BG);

    HGQ_UI_DrawCard(x1,y_top,x2,y_bottom);
    HGQ_UI_DrawCard(x3,y_top,x4,y_bottom);
    HGQ_UI_DrawCard(x5,y_top,x6,y_bottom);

    POINT_COLOR = HGQ_UI_C_SUBTEXT;
    BACK_COLOR  = HGQ_UI_C_CARD;

    HGQ_UI_ShowStr_GBK_Limit((u16)(x1+HGQ_UI_PAD),(u16)(y_top+HGQ_UI_PAD),(u16)(env_w-2*HGQ_UI_PAD),16,STR_ENV,16,0);
    HGQ_UI_ShowStr_GBK_Limit((u16)(x3+HGQ_UI_PAD),(u16)(y_top+HGQ_UI_PAD),(u16)(seat_w-2*HGQ_UI_PAD),16,STR_SEAT,16,0);
    HGQ_UI_ShowStr_GBK_Limit((u16)(x5+HGQ_UI_PAD),(u16)(y_top+HGQ_UI_PAD),(u16)((x6-x5+1)-2*HGQ_UI_PAD),16,STR_LIGHT,16,0);
}

/* 动态刷新 + 自动亮度逻辑 */
void HGQ_UI_Update(HGQ_UI_Data *d, const char *time_hm, const char *weekday)
{
    int w, h;
    int y_top, y_bottom;
    int env_w, seat_w;
    int x1,x2,x3,x4,x5,x6, light_w;
    int y0, y1, y2l;
    int line_w_env, line_w_seat, line_w_light;

    u8 bar_enabled;
    u8 top_dirty = 0;
    u8 env_dirty = 0;
    u8 seat_dirty = 0;
    u8 light_dirty = 0;

    w = lcddev.width;
    h = lcddev.height;

    y_top    = HGQ_UI_TOP_H + HGQ_UI_GAP;
    y_bottom = h - HGQ_UI_BOTTOM_H - HGQ_UI_GAP;

    env_w  = 108;
    seat_w = 132;

    x1 = HGQ_UI_GAP;
    x2 = x1 + env_w - 1;

    x3 = x2 + HGQ_UI_GAP + 1;
    x4 = x3 + seat_w - 1;

    x5 = x4 + HGQ_UI_GAP + 1;
    x6 = w - HGQ_UI_GAP - 1;
    light_w = (x6 - x5 + 1);
    line_w_env = env_w - 2 * HGQ_UI_PAD;
    line_w_seat = seat_w - 2 * HGQ_UI_PAD;
    line_w_light = light_w - 2 * HGQ_UI_PAD;

    if(time_hm == NULL) time_hm = "";
    if(weekday == NULL) weekday = "";

    if(!s_cache_init)
    {
        memset(&s_cache, 0, sizeof(s_cache));
        s_cache.temp_x10 = -100000;
        s_cache.humi = -1;
        s_cache.lux = -1;
        s_cache.use_min = -1;
        s_cache.auto_mode = 2;
        s_cache.light_on = 2;
        s_cache.bri_target = -1;
        s_cache.esp_state = 0xFF;
        s_cache.area_seat[0] = '\0';
        s_cache.status[0] = '\0';
        s_cache.next_time[0] = '\0';
        s_cache_time_hm[0] = '\0';
        s_cache_weekday[0] = '\0';
        s_cache_init = 1;
        top_dirty = env_dirty = seat_dirty = light_dirty = 1;
    }

    /* 顶部栏 */
    if(d->esp_state != s_cache.esp_state ||
       strcmp(d->area_seat, s_cache.area_seat) != 0 ||
       strcmp(time_hm, s_cache_time_hm) != 0 ||
       strcmp(weekday, s_cache_weekday) != 0)
    {
        top_dirty = 1;
    }
    if(top_dirty) HGQ_UI_DrawTopBar(d, time_hm, weekday);

    /* 自动模式：根据光照更新目标亮度（仅在开灯时生效） */
    if(d->auto_mode && d->light_on)
    {
        d->bri_target = HGQ_UI_AutoBriFromLux(d->lux);
    }

    /* 若关灯：目标亮度视为0 */
    if(!d->light_on) d->bri_target = 0;

    /* 亮度缓动动画（让条变化更顺滑） */
    if(s_bri_now < d->bri_target) s_bri_now++;
    else if(s_bri_now > d->bri_target) s_bri_now--;

    /* 内容行 */
    y0  = y_top + HGQ_UI_PAD + 18;
    y1  = y0 + HGQ_UI_LINE_H;
    y2l = y1 + HGQ_UI_LINE_H;

    /* ========== 左卡：环境 ========== */
    if(d->temp_x10 != s_cache.temp_x10 || d->humi != s_cache.humi || d->lux != s_cache.lux) env_dirty = 1;
    if(env_dirty)
    {
        HGQ_UI_ClearLine(x1 + HGQ_UI_PAD, y0, line_w_env, 16);
        HGQ_UI_ClearLine(x1 + HGQ_UI_PAD, y1, line_w_env, 16);
        HGQ_UI_ClearLine(x1 + HGQ_UI_PAD, y2l, line_w_env, 16);

        POINT_COLOR = HGQ_UI_C_TEXT;
        BACK_COLOR  = HGQ_UI_C_CARD;

        HGQ_UI_ShowStr_GBK_Limit((u16)(x1+HGQ_UI_PAD),(u16)y0,48,16,STR_T,16,0);
        LCD_ShowNum((u16)(x1+52),(u16)y0,(u32)(d->temp_x10/10),2,16);
        LCD_ShowString((u16)(x1+68),(u16)y0,8,16,16,(u8*)".");
        LCD_ShowNum((u16)(x1+76),(u16)y0,(u32)(d->temp_x10%10),1,16);
        LCD_ShowString((u16)(x1+84),(u16)y0,16,16,16,(u8*)"C");

        HGQ_UI_ShowStr_GBK_Limit((u16)(x1+HGQ_UI_PAD),(u16)y1,48,16,STR_H,16,0);
        LCD_ShowNum((u16)(x1+52),(u16)y1,(u32)d->humi,3,16);
        LCD_ShowString((u16)(x1+80),(u16)y1,16,16,16,(u8*)"%");

        HGQ_UI_ShowStr_GBK_Limit((u16)(x1+HGQ_UI_PAD),(u16)y2l,48,16,STR_L,16,0);
        LCD_ShowNum((u16)(x1+52),(u16)y2l,(u32)d->lux,4,16);
        LCD_ShowString((u16)(x1+92),(u16)y2l,24,16,16,(u8*)"lx");
    }

    /* ========== 中卡：座位 ========== */
    if(d->use_min != s_cache.use_min ||
       strcmp(d->status, s_cache.status) != 0 ||
       strcmp(d->next_time, s_cache.next_time) != 0)
    {
        seat_dirty = 1;
    }
    if(seat_dirty)
    {
        HGQ_UI_ClearLine(x3 + HGQ_UI_PAD, y0, line_w_seat, 16);
        HGQ_UI_ClearLine(x3 + HGQ_UI_PAD, y1, line_w_seat, 16);
        HGQ_UI_ClearLine(x3 + HGQ_UI_PAD, y2l, line_w_seat, 16);

        HGQ_UI_ShowStr_GBK_Limit((u16)(x3+HGQ_UI_PAD),(u16)y0,48,16,STR_USED,16,0);
        LCD_ShowNum((u16)(x3+52),(u16)y0,(u32)(d->use_min/60),2,16);
        HGQ_UI_ShowStr_GBK_Limit((u16)(x3+72),(u16)y0,40,16,STR_HOUR,16,0);

        HGQ_UI_ShowStr_GBK_Limit((u16)(x3+HGQ_UI_PAD),(u16)y1,48,16,STR_STAT,16,0);
        HGQ_UI_ShowStr_GBK_Limit((u16)(x3+52),(u16)y1,(u16)(seat_w-52-HGQ_UI_PAD),16,(u8*)d->status,16,0);

        HGQ_UI_ShowStr_GBK_Limit((u16)(x3+HGQ_UI_PAD),(u16)y2l,48,16,STR_NEXT,16,0);
        LCD_ShowString((u16)(x3+52),(u16)y2l,(u16)(seat_w-52-HGQ_UI_PAD),16,16,(u8*)d->next_time);
    }

    /* ========== 右卡：灯光（模式 + 条 + %） ========== */
    if(d->auto_mode != s_cache.auto_mode || d->light_on != s_cache.light_on) light_dirty = 1;
    if(light_dirty)
    {
        HGQ_UI_ClearLine(x5 + HGQ_UI_PAD, y0, line_w_light, 16);
        if(d->auto_mode)
        {
            POINT_COLOR = HGQ_UI_C_OK;  BACK_COLOR = HGQ_UI_C_CARD;
            HGQ_UI_ShowStr_GBK_Limit((u16)(x5+HGQ_UI_PAD),(u16)y0,(u16)(light_w-2*HGQ_UI_PAD),16,STR_AUTO,16,0);
        }
        else
        {
            POINT_COLOR = HGQ_UI_C_WARN; BACK_COLOR = HGQ_UI_C_CARD;
            HGQ_UI_ShowStr_GBK_Limit((u16)(x5+HGQ_UI_PAD),(u16)y0,(u16)(light_w-2*HGQ_UI_PAD),16,STR_MANU,16,0);
        }
    }

    /* 亮度条区域（记录矩形供触摸用） */
    {
        int bar_w;
        int bx1,bx2,by1,by2;

        bar_w = 18;
        bx1 = x5 + (light_w - bar_w)/2;
        bx2 = bx1 + bar_w;

        by1 = y0 + HGQ_UI_LINE_H;
        by2 = y_bottom - 34;

        if(by2 <= by1 + 20) by2 = by1 + 20;

        s_bar_x1 = (u16)bx1;
        s_bar_x2 = (u16)bx2;
        s_bar_y1 = (u16)by1;
        s_bar_y2 = (u16)by2;

        /* 条是否允许手动：手动模式 + 开灯 */
        bar_enabled = (u8)((d->auto_mode==0) && (d->light_on==1));

        /* 关灯时禁用显示 */
        if(light_dirty || s_bri_now != s_last_bri_draw || bar_enabled != s_last_bar_enabled)
        {
            HGQ_UI_DrawBrightnessBar(bx1,by1,bx2,by2,s_bri_now,bar_enabled);
            s_last_bri_draw = s_bri_now;
            s_last_bar_enabled = bar_enabled;
        }
    }

    if(light_dirty || s_bri_now != s_last_bri_text)
    {
        HGQ_UI_ClearLine(x5 + (light_w-30)/2, y_bottom - 26, 40, 16);
        POINT_COLOR = HGQ_UI_C_TEXT;
        BACK_COLOR  = HGQ_UI_C_CARD;
        LCD_ShowNum((u16)(x5 + (light_w-30)/2),(u16)(y_bottom-26),(u32)s_bri_now,3,16);
        LCD_ShowString((u16)(x5 + (light_w-30)/2 + 26),(u16)(y_bottom-26),12,16,16,(u8*)"%");
        s_last_bri_text = s_bri_now;
    }

    /* 底部按钮：高亮当前模式/开关 */
    if(light_dirty) HGQ_UI_DrawBottomButtons(d->auto_mode==0, d->auto_mode==1, d->light_on);

    s_cache = *d;
    strncpy(s_cache_time_hm, time_hm, sizeof(s_cache_time_hm) - 1);
    s_cache_time_hm[sizeof(s_cache_time_hm) - 1] = '\0';
    strncpy(s_cache_weekday, weekday, sizeof(s_cache_weekday) - 1);
    s_cache_weekday[sizeof(s_cache_weekday) - 1] = '\0';
}
