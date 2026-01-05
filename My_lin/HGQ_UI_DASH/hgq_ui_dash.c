#include "hgq_ui_dash.h"
#include "lcd.h"
#include "touch.h"
#include "delay.h"
#include <stdio.h>
#include <string.h>

/* ============ 依赖：你已经在 hgq_ui_seat_port.c 里实现的接口 ============ */
u32 HGQ_UISeat_Millis(void);
s16 HGQ_UISeat_TempRead_x10(void);
s16 HGQ_UISeat_HumiRead_x10(void);
u16 HGQ_UISeat_LuxRead(void);
u8  HGQ_UISeat_PresenceRead(void);
u8  HGQ_UISeat_RFIDRead(u8 *uid, u8 *uid_len);
void HGQ_UISeat_RelayPower(u8 on);
void HGQ_UISeat_LightSet(u8 on);
void HGQ_UISeat_CloudPush(void);

/* Show_Str 在 text.c/text.h 里 */
void Show_Str(u16 x,u16 y,u16 width,u16 height,u8 *str,u8 size,u8 mode);

/* ============ 触摸映射（你已验证 swap_xy=0 才准） ============ */
#ifndef DASH_TOUCH_SWAP_XY
#define DASH_TOUCH_SWAP_XY   0
#endif
#ifndef DASH_TOUCH_INV_X
#define DASH_TOUCH_INV_X     0
#endif
#ifndef DASH_TOUCH_INV_Y
#define DASH_TOUCH_INV_Y     0
#endif

/* ============ 业务参数 ============ */
#define RESERVE_HOLD_MIN      15
#define AWAY_HOLD_MIN         10
#define LUX_LIGHT_TH          200

/* ============ UI参数 ============ */
#define TOP_H                 26
#define PAD                   6
#define CARD_GAP              6
#define BTN_H                 34
#define BTN_GAP               6

/* 点击判定 */
#define CLICK_MOVE_THR        10
#define SCROLL_START_THR      8   /* 本页不做滚动，这里只是防误触 */

/* 是否强制刷卡（先看效果可改成0） */
#ifndef DASH_REQUIRE_CARD
#define DASH_REQUIRE_CARD     0
#endif

typedef enum
{
    ST_FREE=0,
    ST_RESERVED,
    ST_IN_USE,
    ST_AWAY
} SEAT_STATE_E;

typedef struct
{
    SEAT_STATE_E st;
    u8 uid[8];
    u8 uid_len;
    u32 reserve_deadline_ms;
    u32 away_deadline_ms;
} SEAT_INFO_T;

static SEAT_INFO_T gSeat;

/* 动态显示缓存（减少闪） */
static char s_last_time[16] = {0};
static char s_last_env[32]  = {0};
static char s_last_lux[20]  = {0};
static char s_last_state[24]= {0};
static char s_msg[28]       = {0};
static u32  s_msg_deadline  = 0;

static u16 gW, gH;

/* 卡片区域 */
static u16 c1x,c1y,c1w,c1h;
static u16 c2x,c2y,c2w,c2h;
static u16 c3x,c3y,c3w,c3h;
static u16 c4x,c4y,c4w,c4h;

/* 按钮区域 */
typedef struct {u16 x,y,w,h; const char *txt; u16 bg; u16 fg;} BTN_T;
static BTN_T bRes, bIn, bOut, bCancel;

/* 触摸状态 */
typedef enum {TP_IDLE=0, TP_DOWN} TP_STATE_E;
static TP_STATE_E tpst = TP_IDLE;
static u16 down_x, down_y, last_x, last_y;

static void map_touch(u16 *x, u16 *y)
{
#if DASH_TOUCH_SWAP_XY
    u16 t=*x; *x=*y; *y=t;
#endif
#if DASH_TOUCH_INV_X
    if(*x < gW) *x = (gW-1) - *x;
#endif
#if DASH_TOUCH_INV_Y
    if(*y < gH) *y = (gH-1) - *y;
#endif
    if(*x >= gW) *x = gW-1;
    if(*y >= gH) *y = gH-1;
}

static u8 hit(u16 px,u16 py,u16 x,u16 y,u16 w,u16 h)
{
    return (px>=x && px<x+w && py>=y && py<y+h);
}

static void fill(u16 x,u16 y,u16 w,u16 h,u16 color)
{
    LCD_Fill(x,y,x+w-1,y+h-1,color);
}

static void rect(u16 x,u16 y,u16 w,u16 h,u16 color)
{
    POINT_COLOR=color;
    LCD_DrawRectangle(x,y,x+w-1,y+h-1);
}

static void msg_set(const char *m, u32 keep_ms)
{
    strncpy(s_msg, m, sizeof(s_msg)-1);
    s_msg[sizeof(s_msg)-1]=0;
    s_msg_deadline = HGQ_UISeat_Millis() + keep_ms;
}

static const char* st_text(SEAT_STATE_E st)
{
    switch(st)
    {
        case ST_FREE:     return "FREE";
        case ST_RESERVED: return "RESERVED";
        case ST_IN_USE:   return "IN USE";
        case ST_AWAY:     return "AWAY";
        default:          return "?";
    }
}

static void draw_top(void)
{
    fill(0,0,gW,TOP_H,BLUE);
    POINT_COLOR=WHITE; BACK_COLOR=BLUE;
    Show_Str(6,5,120,16,(u8*)"DASHBOARD",16,0);
}

static void draw_card(u16 x,u16 y,u16 w,u16 h, const char *title)
{
    fill(x,y,w,h,WHITE);
    rect(x,y,w,h,BLACK);
    POINT_COLOR=BLACK; BACK_COLOR=WHITE;
    Show_Str(x+6,y+4,w-12,16,(u8*)title,16,0);
}

static void draw_btn(const BTN_T *b)
{
    fill(b->x,b->y,b->w,b->h,b->bg);
    rect(b->x,b->y,b->w,b->h,BLACK);
    POINT_COLOR=b->fg; BACK_COLOR=b->bg;
    Show_Str(b->x+4,b->y+(b->h-16)/2,b->w-8,16,(u8*)b->txt,16,0);
}

static void layout_calc(void)
{
    gW = lcddev.width;
    gH = lcddev.height;

    /* 2x2 cards */
    u16 area_y = TOP_H + PAD;
    u16 area_h = gH - area_y - (BTN_H + PAD + 16 + PAD); /* 留按钮+消息 */
    u16 row_h  = (area_h - CARD_GAP) / 2;
    u16 col_w  = (gW - PAD*2 - CARD_GAP) / 2;

    c1x = PAD;                 c1y = area_y;               c1w = col_w; c1h = row_h;
    c2x = PAD + col_w + CARD_GAP; c2y = area_y;             c2w = col_w; c2h = row_h;
    c3x = PAD;                 c3y = area_y + row_h + CARD_GAP; c3w = col_w; c3h = row_h;
    c4x = PAD + col_w + CARD_GAP; c4y = c3y;                c4w = col_w; c4h = row_h;

    /* buttons row */
    u16 btn_y = gH - PAD - 16 - PAD - BTN_H;
    u16 bw = (gW - PAD*2 - BTN_GAP*3) / 4;

    bRes.x=PAD;               bRes.y=btn_y; bRes.w=bw; bRes.h=BTN_H; bRes.txt="RES";    bRes.bg=CYAN;   bRes.fg=BLACK;
    bIn.x =PAD + (bw+BTN_GAP);bIn.y =btn_y; bIn.w =bw; bIn.h =BTN_H; bIn.txt ="IN";     bIn.bg=GREEN;  bIn.fg=BLACK;
    bOut.x=PAD + 2*(bw+BTN_GAP);bOut.y=btn_y;bOut.w=bw; bOut.h=BTN_H; bOut.txt="OUT";   bOut.bg=RED;    bOut.fg=WHITE;
    bCancel.x=PAD + 3*(bw+BTN_GAP);bCancel.y=btn_y;bCancel.w=bw;bCancel.h=BTN_H;bCancel.txt="CANCEL"; bCancel.bg=YELLOW; bCancel.fg=BLACK;
}

static void draw_static_ui(void)
{
    LCD_Clear(0x7BEF); /* 背景灰 */
    draw_top();

    draw_card(c1x,c1y,c1w,c1h,"TIME");
    draw_card(c2x,c2y,c2w,c2h,"TEMP/HUMI");
    draw_card(c3x,c3y,c3w,c3h,"LUX");
    draw_card(c4x,c4y,c4w,c4h,"SEAT");

    draw_btn(&bRes);
    draw_btn(&bIn);
    draw_btn(&bOut);
    draw_btn(&bCancel);

    /* message label area */
    fill(PAD, gH-PAD-16, gW-PAD*2, 16, 0x7BEF);
    POINT_COLOR=BLACK; BACK_COLOR=0x7BEF;
    Show_Str(PAD, gH-PAD-16, gW-PAD*2, 16, (u8*)"MSG:",16,0);

    /* 清缓存，强制下一次动态刷新 */
    s_last_time[0]=0; s_last_env[0]=0; s_last_lux[0]=0; s_last_state[0]=0; s_msg[0]=0; s_msg_deadline=0;
}

static void draw_dyn_time(void)
{
    u32 ms = HGQ_UISeat_Millis();
    u32 sec = ms/1000;
    u32 hh = (sec/3600)%100;
    u32 mm = (sec/60)%60;
    u32 ss = sec%60;

    char buf[16];
    snprintf(buf,sizeof(buf),"%02lu:%02lu:%02lu",(unsigned long)hh,(unsigned long)mm,(unsigned long)ss);

    if(strcmp(buf,s_last_time)!=0)
    {
        strcpy(s_last_time,buf);
        fill(c1x+2,c1y+24,c1w-4,22,WHITE);
        POINT_COLOR=BLUE; BACK_COLOR=WHITE;
        Show_Str(c1x+8,c1y+28,c1w-16,16,(u8*)buf,16,0);
    }
}

static void draw_dyn_env(void)
{
    s16 t10 = HGQ_UISeat_TempRead_x10();
    s16 h10 = HGQ_UISeat_HumiRead_x10();

    char buf[32];
    snprintf(buf,sizeof(buf),"T:%d.%dC  H:%d.%d%%",
             (int)(t10/10),(int)abs(t10%10),
             (int)(h10/10),(int)abs(h10%10));

    if(strcmp(buf,s_last_env)!=0)
    {
        strcpy(s_last_env,buf);
        fill(c2x+2,c2y+24,c2w-4,22,WHITE);
        POINT_COLOR=BLACK; BACK_COLOR=WHITE;
        Show_Str(c2x+6,c2y+28,c2w-12,16,(u8*)buf,16,0);
    }
}

static void draw_dyn_lux(void)
{
    u16 lux = HGQ_UISeat_LuxRead();
    char buf[20];
    snprintf(buf,sizeof(buf),"%u lux",(unsigned)lux);

    if(strcmp(buf,s_last_lux)!=0)
    {
        strcpy(s_last_lux,buf);
        fill(c3x+2,c3y+24,c3w-4,22,WHITE);
        POINT_COLOR=BLACK; BACK_COLOR=WHITE;
        Show_Str(c3x+6,c3y+28,c3w-12,16,(u8*)buf,16,0);
    }
}

static void draw_dyn_state(void)
{
    char buf[24];
    strcpy(buf, st_text(gSeat.st));

    if(strcmp(buf,s_last_state)!=0)
    {
        strcpy(s_last_state,buf);
        fill(c4x+2,c4y+24,c4w-4,22,WHITE);
        POINT_COLOR=BLACK; BACK_COLOR=WHITE;
        Show_Str(c4x+6,c4y+28,c4w-12,16,(u8*)buf,16,0);
    }

    /* 倒计时行（只对 RESERVED/AWAY 显示） */
    fill(c4x+2,c4y+46,c4w-4,18,WHITE);
    if(gSeat.st==ST_RESERVED && gSeat.reserve_deadline_ms>HGQ_UISeat_Millis())
    {
        u32 left = gSeat.reserve_deadline_ms - HGQ_UISeat_Millis();
        u32 m = left/60000;
        char t[24]; snprintf(t,sizeof(t),"hold:%lum",(unsigned long)m);
        POINT_COLOR=BLUE; BACK_COLOR=WHITE;
        Show_Str(c4x+6,c4y+46,c4w-12,16,(u8*)t,16,0);
    }
    else if(gSeat.st==ST_AWAY && gSeat.away_deadline_ms>HGQ_UISeat_Millis())
    {
        u32 left = gSeat.away_deadline_ms - HGQ_UISeat_Millis();
        u32 m = left/60000;
        char t[24]; snprintf(t,sizeof(t),"away:%lum",(unsigned long)m);
        POINT_COLOR=RED; BACK_COLOR=WHITE;
        Show_Str(c4x+6,c4y+46,c4w-12,16,(u8*)t,16,0);
    }
}

static void draw_msg(void)
{
    /* 超时自动清消息 */
    if(s_msg_deadline && HGQ_UISeat_Millis() > s_msg_deadline)
    {
        s_msg_deadline = 0;
        s_msg[0] = 0;
    }

    /* 每次都重画一小条即可 */
    fill(PAD+34, gH-PAD-16, gW-PAD*2-34, 16, 0x7BEF);
    if(s_msg[0])
    {
        POINT_COLOR=BLUE; BACK_COLOR=0x7BEF;
        Show_Str(PAD+34, gH-PAD-16, gW-PAD*2-34, 16, (u8*)s_msg, 16, 0);
    }
}

/* ============ 业务 tick：预约超时/离座超时/自动灯控 ============ */
static void seat_tick(void)
{
    u32 now = HGQ_UISeat_Millis();

    if(gSeat.st==ST_RESERVED && gSeat.reserve_deadline_ms && now > gSeat.reserve_deadline_ms)
    {
        gSeat.st = ST_FREE;
        gSeat.uid_len=0;
        gSeat.reserve_deadline_ms=0;
        msg_set("reserve timeout -> free",1200);
        HGQ_UISeat_RelayPower(0);
        HGQ_UISeat_LightSet(0);
        HGQ_UISeat_CloudPush();
    }

    if(gSeat.st==ST_IN_USE)
    {
        if(HGQ_UISeat_PresenceRead())
        {
            gSeat.away_deadline_ms = 0;
        }
        else if(gSeat.away_deadline_ms==0)
        {
            gSeat.st = ST_AWAY;
            gSeat.away_deadline_ms = now + (u32)AWAY_HOLD_MIN*60UL*1000UL;
            msg_set("away timer start",1200);
            HGQ_UISeat_CloudPush();
        }
    }
    else if(gSeat.st==ST_AWAY)
    {
        if(HGQ_UISeat_PresenceRead())
        {
            gSeat.st = ST_IN_USE;
            gSeat.away_deadline_ms = 0;
            msg_set("back -> in use",1200);
            HGQ_UISeat_CloudPush();
        }
        else if(gSeat.away_deadline_ms && now > gSeat.away_deadline_ms)
        {
            gSeat.st = ST_FREE;
            gSeat.uid_len=0;
            gSeat.away_deadline_ms=0;
            msg_set("away timeout -> free",1200);
            HGQ_UISeat_RelayPower(0);
            HGQ_UISeat_LightSet(0);
            HGQ_UISeat_CloudPush();
        }
    }

    /* 使用中自动灯控 */
    if(gSeat.st==ST_IN_USE)
    {
        u16 lux = HGQ_UISeat_LuxRead();
        if(lux < LUX_LIGHT_TH) HGQ_UISeat_LightSet(1);
        else                   HGQ_UISeat_LightSet(0);
    }
}

static u8 require_card_ok(u8 *uid,u8 *uid_len)
{
#if DASH_REQUIRE_CARD
    if(!HGQ_UISeat_RFIDRead(uid,uid_len) || *uid_len==0)
    {
        msg_set("NO CARD",1200);
        return 0;
    }
#else
    /* 不强制卡：尝试读，有就用，没有也允许 */
    HGQ_UISeat_RFIDRead(uid,uid_len);
#endif
    return 1;
}

static u8 uid_equal(const u8 *a,u8 al,const u8 *b,u8 bl)
{
    if(al!=bl) return 0;
    return (memcmp(a,b,al)==0)?1:0;
}

/* ============ 点击处理：按钮业务 ============ */
static void on_click(u16 x,u16 y)
{
    u8 uid[8]={0}, ul=0;

    if(hit(x,y,bRes.x,bRes.y,bRes.w,bRes.h))
    {
        if(gSeat.st!=ST_FREE){ msg_set("NOT FREE",1200); return; }
        if(!require_card_ok(uid,&ul)) return;

        gSeat.st = ST_RESERVED;
        if(ul){ memcpy(gSeat.uid,uid,ul); gSeat.uid_len=ul; }
        gSeat.reserve_deadline_ms = HGQ_UISeat_Millis() + (u32)RESERVE_HOLD_MIN*60UL*1000UL;
        msg_set("reserved",1200);
        HGQ_UISeat_CloudPush();
        return;
    }

    if(hit(x,y,bIn.x,bIn.y,bIn.w,bIn.h))
    {
        if(gSeat.st!=ST_RESERVED){ msg_set("NOT RESERVED",1200); return; }
        if(!require_card_ok(uid,&ul)) return;

        if(gSeat.uid_len && ul && !uid_equal(uid,ul,gSeat.uid,gSeat.uid_len))
        {
            msg_set("UID MISMATCH",1200);
            return;
        }

        gSeat.st = ST_IN_USE;
        gSeat.reserve_deadline_ms = 0;
        gSeat.away_deadline_ms = 0;

        HGQ_UISeat_RelayPower(1);
        msg_set("check-in ok",1200);
        HGQ_UISeat_CloudPush();
        return;
    }

    if(hit(x,y,bOut.x,bOut.y,bOut.w,bOut.h))
    {
        if(!(gSeat.st==ST_IN_USE || gSeat.st==ST_AWAY)){ msg_set("NOT IN USE",1200); return; }
        if(!require_card_ok(uid,&ul)) return;

        if(gSeat.uid_len && ul && !uid_equal(uid,ul,gSeat.uid,gSeat.uid_len))
        {
            msg_set("UID MISMATCH",1200);
            return;
        }

        gSeat.st = ST_FREE;
        gSeat.uid_len=0;
        gSeat.away_deadline_ms=0;

        HGQ_UISeat_RelayPower(0);
        HGQ_UISeat_LightSet(0);
        msg_set("check-out ok",1200);
        HGQ_UISeat_CloudPush();
        return;
    }

    if(hit(x,y,bCancel.x,bCancel.y,bCancel.w,bCancel.h))
    {
        if(gSeat.st!=ST_RESERVED){ msg_set("NOT RESERVED",1200); return; }
        if(!require_card_ok(uid,&ul)) return;

        if(gSeat.uid_len && ul && !uid_equal(uid,ul,gSeat.uid,gSeat.uid_len))
        {
            msg_set("UID MISMATCH",1200);
            return;
        }

        gSeat.st = ST_FREE;
        gSeat.uid_len=0;
        gSeat.reserve_deadline_ms=0;

        msg_set("cancel ok",1200);
        HGQ_UISeat_CloudPush();
        return;
    }
}

/* ============ 对外接口 ============ */
void HGQ_UIDash_Init(void)
{
    layout_calc();

    memset(&gSeat,0,sizeof(gSeat));
    gSeat.st = ST_FREE;

    draw_static_ui();
}

void HGQ_UIDash_Task(void)
{
    /* 业务 tick */
    seat_tick();

    /* 触摸读取：直接用 TP_Scan，避免你工程里 HGQ_UISeat_TouchRead 重复定义 */
    {
        u16 x=0,y=0;
        u8 pressed = 0;

        if(TP_Scan(0))
        {
            x = tp_dev.x[0];
            y = tp_dev.y[0];
            pressed = 1;
        }

        if(pressed)
        {
            map_touch(&x,&y);

            if(tpst==TP_IDLE)
            {
                tpst = TP_DOWN;
                down_x=x; down_y=y;
                last_x=x; last_y=y;
            }
            else
            {
                last_x=x; last_y=y;
            }
        }
        else
        {
            if(tpst==TP_DOWN)
            {
                u16 upx = last_x, upy = last_y;
                u16 mdx = (upx>down_x)?(upx-down_x):(down_x-upx);
                u16 mdy = (upy>down_y)?(upy-down_y):(down_y-upy);

                if(mdx < CLICK_MOVE_THR && mdy < CLICK_MOVE_THR)
                {
                    on_click(upx,upy);
                }
                tpst = TP_IDLE;
            }
        }
    }

    /* 动态刷新：节流（避免闪屏） */
    static u32 last_refresh = 0;
    u32 now = HGQ_UISeat_Millis();
    if(now - last_refresh >= 250)
    {
        last_refresh = now;

        draw_dyn_time();
        draw_dyn_env();
        draw_dyn_lux();
        draw_dyn_state();
        draw_msg();
    }
}

