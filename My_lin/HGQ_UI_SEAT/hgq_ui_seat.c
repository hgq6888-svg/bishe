/*
 * hgq_ui_seat.c
 * 座位预约/签到系统 - LCD横屏UI（ILI9341 240x320 -> 横屏一般 320x240）
 *
 * 重点改进：
 * 1) 座位过多支持“上下滑动 + 惯性滚动 + 快速甩动翻页”
 * 2) 滑动更灵敏：更小的滚动启动阈值 + 速度估计
 * 3) 减少闪屏：不再整块清左侧区域；改为“按格子覆盖重画”，点击只重画两格
 *
 * ⚠重要：Show_Str() 的中文识别是 GBK/GB2312(2字节) 方式。
 *   本文件如果用 UTF-8 保存，(u8*)"中文" 会变成 UTF-8 三字节序列 => 显示乱码/甚至编译报错。
 *   请在 Keil 里把本文件保存为 ANSI/GBK 编码（不要 UTF-8）。
 */

#include "hgq_ui_seat.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef __weak
#define __weak __attribute__((weak))
#endif

/* ========================= 触摸坐标映射配置 =========================
 * 你已验证：HGQ_UI_TOUCH_SWAP_XY=0 时选座精准。
 * 如需反转，再改成 1/开启 INV_x/y。
 */
#ifndef HGQ_UI_TOUCH_SWAP_XY
#define HGQ_UI_TOUCH_SWAP_XY   0
#endif
#ifndef HGQ_UI_TOUCH_INV_X
#define HGQ_UI_TOUCH_INV_X     0
#endif
#ifndef HGQ_UI_TOUCH_INV_Y
#define HGQ_UI_TOUCH_INV_Y     0
#endif

/* ========================= UI参数 ========================= */
#define UI_TOP_H              28
#define UI_LEFT_RATE          62
#define UI_PAD                6
#define UI_TITLE_H            22

/* 触摸灵敏度 */
#define CLICK_MOVE_THR        10     /* 抬起判定“点击”允许的最大位移 */
#define SCROLL_START_THR      4      /* 位移超过此值进入滚动（越小越灵敏） */

/* 滚动映射：像素 -> 行（越小越灵敏） */
#define PIX_PER_ROW           18

/* 惯性相关 */
#define MAX_VELOCITY_PX_S     2200
#define INERTIA_DECAY_PER_S   2500  /* 速度衰减（越大停得越快） */
#define INERTIA_STOP_V        80
#define FLING_PAGE_V_THR      800    /* 甩动翻页阈值(px/s) */

/* 颜色 */
#define GRID_BG_COLOR         BLACK
#define COLOR_PANEL_BG        0x7BEF

/* ========================= 文本（注意：本文件需GBK/ANSI保存） ========================= */
#define TXT_STATUS      ((u8*)"\xD7\xB4\xCC\xAC")                         /* 状态 */
#define TXT_FREE        ((u8*)"\xBF\xD5\xCF\xD0")                         /* 空闲 */
#define TXT_RES         ((u8*)"\xD2\xD1\xD4\xA4\xD4\xBC")                 /* 已预约 */
#define TXT_INUSE       ((u8*)"\xCA\xB9\xD3\xC3\xD6\xD0")                 /* 使用中 */
#define TXT_AWAY        ((u8*)"\xC0\xEB\xD7\xF9\xBC\xC6\xCA\xB1")         /* 离座计时 */

#define TXT_SEAT        ((u8*)"\xD7\xF9\xCE\xBB")                         /* 座位 */
#define TXT_CLOUD       ((u8*)"\xD4\xC6\xB6\xCB")                         /* 云端 */
#define TXT_ONLINE      ((u8*)"\xD4\xDA\xCF\xDF")                         /* 在线 */
#define TXT_OFFLINE     ((u8*)"\xC0\xEB\xCF\xDF")                         /* 离线 */
#define TXT_ENV         ((u8*)"\xBB\xB7\xBE\xB3")                         /* 环境 */
#define TXT_MIN         ((u8*)"\xB7\xD6\xD6\xD3")                         /* 分钟 */

#define TXT_RESERVE     ((u8*)"\xD4\xA4\xD4\xBC")                         /* 预约 */
#define TXT_CHECKIN     ((u8*)"\xC7\xA9\xB5\xBD")                         /* 签到 */
#define TXT_CHECKOUT    ((u8*)"\xC7\xA9\xCD\xCB")                         /* 签退 */
#define TXT_CANCEL      ((u8*)"\xC8\xA1\xCF\xFB")                         /* 取消 */
#define TXT_SYNC        ((u8*)"\xCD\xAC\xB2\xBD")                         /* 同步 */

/* ========================= 布局结构 ========================= */
typedef struct
{
    u16 w, h;
    u16 left_w, right_x, right_w;

    u8  cols;
    u16 grid_top;
    u16 cell_w, cell_h;
    u8  vis_rows;
    u8  total_rows;

    /* 右侧区域 */
    u16 detail_x, detail_y, detail_w, detail_h;
    u16 tip_y;
    u16 btn_w, btn_h;
    u16 btn_rx1, btn_rx2, btn_ry1, btn_ry2;
    u16 env_x, env_y, env_w, env_h;
    u16 sync_x, sync_y, sync_w, sync_h;
} UI_LAYOUT_T;

static UI_LAYOUT_T gL;

/* ========================= 座位数据 ========================= */
typedef struct
{
    HGQ_SEAT_STATE_E state;
    u8  uid[8];
    u8  uid_len;
    u32 reserve_deadline_ms;
    u32 away_deadline_ms;
} SEAT_T;

static SEAT_T g_seat[HGQ_UI_SEAT_MAX];
static u8  g_seat_count = 12;
static s8  g_sel = 0;
static u8  g_cloud_online = 1;

/* ========================= 滚动状态 =========================
 * 以“行”为单位滚动：0..(total_rows-vis_rows)
 */
static s16 g_scroll_row = 0;

/* 惯性 */
static s32 g_inertia_v = 0;
static u8  g_inertia_on = 0;
static u32 g_inertia_last_ms = 0;

/* ========================= dirty 刷新 ========================= */
static u8  g_dirty_top = 1;
static u8  g_dirty_grid = 1;         /* 左侧可视格子区需要重绘 */
static u8  g_dirty_right_static = 1;
static u8  g_dirty_right_dyn = 1;
static u8  g_dirty_tip = 1;

static u32 g_last_top_ms = 0;

/* ========================= 提示（用ASCII，避免编码问题） ========================= */
static char g_tip[28] = {0};
static u32  g_tip_deadline_ms = 0;

static void ui_set_tip(const char *s, u32 keep_ms)
{
    if(!s){ g_tip[0]=0; g_tip_deadline_ms=0; g_dirty_tip=1; return; }
    strncpy(g_tip, s, sizeof(g_tip)-1);
    g_tip[sizeof(g_tip)-1]=0;
    g_tip_deadline_ms = HGQ_UISeat_Millis() + keep_ms;
    g_dirty_tip=1;
}

/* ========================= 触摸状态机 ========================= */
typedef enum { TP_IDLE=0, TP_DOWN, TP_SCROLLING } TP_STATE_E;
static TP_STATE_E g_tp_state = TP_IDLE;
static u16 g_tp_down_x=0, g_tp_down_y=0;
static u16 g_tp_last_x=0, g_tp_last_y=0;
static u32 g_tp_last_ms=0;
static s32 g_tp_vy=0; /* px/s，滤波后的速度 */

/* ========================= 小工具 ========================= */
static s16 clamp_s16(s16 v, s16 lo, s16 hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static s32 clamp_s32(s32 v, s32 lo, s32 hi){ if(v<lo) return lo; if(v>hi) return hi; return v; }
static u8  ui_hit(u16 px,u16 py,u16 x,u16 y,u16 w,u16 h){ return (px>=x && px<x+w && py>=y && py<y+h); }

static void ui_fill(u16 x,u16 y,u16 w,u16 h,u16 color)
{
    LCD_Fill(x,y,x+w-1,y+h-1,color);
}

static void ui_map_touch(u16 *x, u16 *y)
{
#if HGQ_UI_TOUCH_SWAP_XY
    u16 t=*x; *x=*y; *y=t;
#endif
#if HGQ_UI_TOUCH_INV_X
    if(*x < gL.w) *x = (gL.w-1)-*x;
#endif
#if HGQ_UI_TOUCH_INV_Y
    if(*y < gL.h) *y = (gL.h-1)-*y;
#endif
    if(*x >= gL.w) *x = gL.w-1;
    if(*y >= gL.h) *y = gL.h-1;
}

static u16 seat_color(HGQ_SEAT_STATE_E st)
{
    switch(st)
    {
        case HGQ_SEAT_FREE:           return GREEN;
        case HGQ_SEAT_RESERVED:       return YELLOW;
        case HGQ_SEAT_IN_USE:         return 0xFD20;
        case HGQ_SEAT_AWAY_COUNTDOWN: return RED;
        default:                      return WHITE;
    }
}
static u8* seat_text(HGQ_SEAT_STATE_E st)
{
    switch(st)
    {
        case HGQ_SEAT_FREE:           return TXT_FREE;
        case HGQ_SEAT_RESERVED:       return TXT_RES;
        case HGQ_SEAT_IN_USE:         return TXT_INUSE;
        case HGQ_SEAT_AWAY_COUNTDOWN: return TXT_AWAY;
        default:                      return (u8*)"?";
    }
}
static u8 uid_equal(const u8 *a,u8 al,const u8 *b,u8 bl)
{
    if(al!=bl) return 0;
    return (memcmp(a,b,al)==0)?1:0;
}

/* ========================= 布局计算 ========================= */
static void ui_layout_calc(void)
{
    gL.w = lcddev.width;
    gL.h = lcddev.height;

    gL.left_w  = (gL.w * UI_LEFT_RATE) / 100;
    gL.right_x = gL.left_w;
    gL.right_w = gL.w - gL.right_x;

    gL.cols = 4;
    /* 座位多时不要把列数提太高，否则单格太窄导致编号/状态被截断 */
    if(g_seat_count > 20) gL.cols = 5;

    gL.total_rows = (g_seat_count + gL.cols - 1) / gL.cols;
    if(gL.total_rows == 0) gL.total_rows = 1;

    gL.grid_top = UI_TOP_H + UI_TITLE_H;

    gL.cell_w = (gL.left_w - UI_PAD*(gL.cols+1)) / gL.cols;

    /* 可视行数按高度自适应 */
    {
        u16 left_h = gL.h - UI_TOP_H;
        u16 avail  = left_h - UI_TITLE_H - UI_PAD*2;

        gL.vis_rows = 4;
        if(avail >= 5*46) gL.vis_rows = 5;
        if(avail >= 6*46) gL.vis_rows = 6;

        if(gL.vis_rows > gL.total_rows) gL.vis_rows = gL.total_rows;
        if(gL.vis_rows == 0) gL.vis_rows = 1;

        gL.cell_h = (left_h - UI_TITLE_H - UI_PAD*(gL.vis_rows+1)) / gL.vis_rows;
        if(gL.cell_h < 34) gL.cell_h = 34;
    }

    /* 右侧布局 */
    gL.detail_x = gL.right_x + UI_PAD;
    gL.detail_y = UI_TOP_H + UI_PAD;
    gL.detail_w = gL.right_w - UI_PAD*2;
    gL.detail_h = 96;

    gL.tip_y    = gL.detail_y + gL.detail_h - 18; /* 提示在 detail 内部 */

    gL.btn_h = 42;
    gL.btn_w = (gL.detail_w - 6)/2;
    gL.btn_rx1 = gL.detail_x;
    gL.btn_rx2 = gL.detail_x + gL.btn_w + 6;
    gL.btn_ry1 = gL.detail_y + gL.detail_h + 10;
    gL.btn_ry2 = gL.btn_ry1 + gL.btn_h + 8;

    gL.env_x = gL.detail_x;
    gL.env_y = gL.btn_ry2 + gL.btn_h + 10;
    gL.env_w = gL.detail_w;
    gL.env_h = (gL.h - UI_PAD) - gL.env_y;
    if((s16)gL.env_h < 60) gL.env_h = 60;

    gL.sync_w = 80; gL.sync_h = 32;
    gL.sync_x = gL.env_x + gL.env_w - gL.sync_w - 6;
    gL.sync_y = gL.env_y + 44;

    /* 修正 scroll_row */
    if(gL.total_rows <= gL.vis_rows) g_scroll_row = 0;
    else
    {
        s16 max_row = (s16)(gL.total_rows - gL.vis_rows);
        g_scroll_row = clamp_s16(g_scroll_row, 0, max_row);
    }
}

/* ========================= 顶部条 ========================= */
static void ui_draw_top(void)
{
    ui_fill(0,0,gL.w,UI_TOP_H,BLUE);
    POINT_COLOR=WHITE; BACK_COLOR=BLUE;

    Show_Str(6,6,60,16,TXT_SEAT,16,0);
    Show_Str(72,6,60,16,TXT_CLOUD,16,0);
    Show_Str(120,6,60,16,(g_cloud_online?TXT_ONLINE:TXT_OFFLINE),16,0);

    {
        char buf[20];
        snprintf(buf,sizeof(buf),"t=%lus",(unsigned long)(HGQ_UISeat_Millis()/1000));
        Show_Str(gL.w-90,6,90,16,(u8*)buf,16,0);
    }
}

/* ========================= 左侧网格绘制（无整块清屏，按格子覆盖） ========================= */
static void ui_draw_cell_by_index(u8 idx, u8 highlight)
{
    u8 row = idx / gL.cols;
    u8 col = idx % gL.cols;

    /* 只画“当前可视范围”内的格子 */
    if(row < (u8)g_scroll_row) return;
    if(row >= (u8)(g_scroll_row + gL.vis_rows)) return;

    u8 vis_r = (u8)(row - (u8)g_scroll_row);

    u16 x = UI_PAD + col*(gL.cell_w + UI_PAD);
    u16 y = gL.grid_top + UI_PAD + vis_r*(gL.cell_h + UI_PAD);

    u16 bg = seat_color(g_seat[idx].state);
    ui_fill(x,y,gL.cell_w,gL.cell_h,bg);

    /* 边框 */
    POINT_COLOR = highlight ? WHITE : BLACK;
    BACK_COLOR  = bg;
    LCD_DrawRectangle(x,y,x+gL.cell_w-1,y+gL.cell_h-1);

    /* 文本 */
    {
        char num[8];
        /* 只显示数字，避免列数较多时 "#%02d" 变长被截断 */
        snprintf(num, sizeof(num), "%d", (int)idx + 1);

        POINT_COLOR = BLACK;
        BACK_COLOR  = bg;

        /* 第一行：编号 */
        Show_Str(x + 2, y + 2, gL.cell_w - 4, 16, (u8*)num, 16, 0);

        /* 第二行：状态（位置随单元高度自适应，避免被裁切） */
        {
            u16 st_y = y + (gL.cell_h / 2);
            if(st_y < (u16)(y + 18)) st_y = y + 18;
            if(st_y > (u16)(y + gL.cell_h - 18)) st_y = y + gL.cell_h - 18;
            Show_Str(x + 2, st_y, gL.cell_w - 4, 16, seat_text(g_seat[idx].state), 16, 0);
        }
    }
}

static void ui_draw_scrollbar(void)
{
    /* 右侧小滚动条（占左侧最后 3px） */
    u16 bar_x = gL.left_w - 4;
    u16 bar_y = gL.grid_top;
    u16 bar_h = gL.h - bar_y - 2;

    /* 先画底 */
    ui_fill(bar_x, bar_y, 3, bar_h, 0x39E7);

    if(gL.total_rows <= gL.vis_rows) return;

    u16 thumb_h = (u16)((u32)bar_h * gL.vis_rows / gL.total_rows);
    if(thumb_h < 12) thumb_h = 12;

    s16 max_row = (s16)(gL.total_rows - gL.vis_rows);
    u16 thumb_y = bar_y;
    if(max_row > 0)
        thumb_y = (u16)(bar_y + (u32)(bar_h - thumb_h) * (u16)g_scroll_row / (u16)max_row);

    ui_fill(bar_x, thumb_y, 3, thumb_h, WHITE);
}

static void ui_draw_grid_all_visible(void)
{
    /* 左侧标题区（小块） */
    ui_fill(0, UI_TOP_H, gL.left_w, UI_TITLE_H, GRID_BG_COLOR);
    POINT_COLOR=CYAN; BACK_COLOR=GRID_BG_COLOR;
    Show_Str(6, UI_TOP_H+4, gL.left_w-12, 16, TXT_SEAT, 16, 0);

    /* 画可视格子 */
    u8 start = (u8)(g_scroll_row * gL.cols);
    u8 end   = (u8)(start + gL.vis_rows * gL.cols);

    for(u8 idx=start; idx<end; idx++)
    {
        if(idx >= g_seat_count)
        {
            /* 超出 seat_count 的空格子：画背景块，覆盖旧内容 */
            u8 row = idx / gL.cols;
            u8 col = idx % gL.cols;
            u8 vis_r = row - (u8)g_scroll_row;
            u16 x = UI_PAD + col*(gL.cell_w + UI_PAD);
            u16 y = gL.grid_top + UI_PAD + vis_r*(gL.cell_h + UI_PAD);
            ui_fill(x,y,gL.cell_w,gL.cell_h,GRID_BG_COLOR);
            continue;
        }
        ui_draw_cell_by_index(idx, (idx==(u8)g_sel));
    }

    ui_draw_scrollbar();
}

/* ========================= 右侧面板 ========================= */
static void ui_draw_btn(u16 x,u16 y,u16 w,u16 h,u8 *txt,u16 bg,u16 fg)
{
    ui_fill(x,y,w,h,bg);
    POINT_COLOR=fg; BACK_COLOR=bg;
    LCD_DrawRectangle(x,y,x+w-1,y+h-1);
    Show_Str(x+4,y+(h-16)/2,w-8,16,txt,16,0);
}

static void ui_draw_right_static(void)
{
    ui_fill(gL.right_x, UI_TOP_H, gL.right_w, gL.h-UI_TOP_H, COLOR_PANEL_BG);

    ui_fill(gL.detail_x,gL.detail_y,gL.detail_w,gL.detail_h,WHITE);
    POINT_COLOR=BLACK; BACK_COLOR=WHITE;
    LCD_DrawRectangle(gL.detail_x,gL.detail_y,gL.detail_x+gL.detail_w-1,gL.detail_y+gL.detail_h-1);

    ui_draw_btn(gL.btn_rx1,gL.btn_ry1,gL.btn_w,gL.btn_h,TXT_RESERVE,  CYAN,   BLACK);
    ui_draw_btn(gL.btn_rx2,gL.btn_ry1,gL.btn_w,gL.btn_h,TXT_CHECKIN,  GREEN,  BLACK);
    ui_draw_btn(gL.btn_rx1,gL.btn_ry2,gL.btn_w,gL.btn_h,TXT_CHECKOUT, RED,    WHITE);
    ui_draw_btn(gL.btn_rx2,gL.btn_ry2,gL.btn_w,gL.btn_h,TXT_CANCEL,   YELLOW, BLACK);

    ui_fill(gL.env_x,gL.env_y,gL.env_w,gL.env_h,WHITE);
    POINT_COLOR=BLACK; BACK_COLOR=WHITE;
    LCD_DrawRectangle(gL.env_x,gL.env_y,gL.env_x+gL.env_w-1,gL.env_y+gL.env_h-1);

    Show_Str(gL.env_x+6,gL.env_y+6,80,16,TXT_ENV,16,0);

    ui_draw_btn(gL.sync_x,gL.sync_y,gL.sync_w,gL.sync_h,TXT_SYNC,BLUE,WHITE);
}

static void ui_draw_tip_only(void)
{
    ui_fill(gL.detail_x+1, gL.tip_y, gL.detail_w-2, 17, WHITE);
    if(g_tip_deadline_ms && (HGQ_UISeat_Millis() < g_tip_deadline_ms) && g_tip[0])
    {
        POINT_COLOR=BLUE; BACK_COLOR=WHITE;
        Show_Str(gL.detail_x+6, gL.tip_y+1, gL.detail_w-12, 16, (u8*)g_tip, 16, 0);
    }
    else { g_tip_deadline_ms=0; g_tip[0]=0; }
}

static void ui_draw_right_dynamic(void)
{
    /* detail 内容区：只清内部，不动边框 */
    ui_fill(gL.detail_x+1,gL.detail_y+1,gL.detail_w-2,gL.detail_h-2,WHITE);
    POINT_COLOR=BLACK; BACK_COLOR=WHITE;

    {
        char t[20];
        snprintf(t,sizeof(t),"Seat #%02d",(int)g_sel+1);
        Show_Str(gL.detail_x+6,gL.detail_y+6,gL.detail_w-12,16,(u8*)t,16,0);
    }

    Show_Str(gL.detail_x+6,gL.detail_y+28,40,16,TXT_STATUS,16,0);
    Show_Str(gL.detail_x+46,gL.detail_y+28,gL.detail_w-52,16,seat_text(g_seat[g_sel].state),16,0);

    /* 倒计时：用数字 + “分钟” */
    {
        u32 now = HGQ_UISeat_Millis();
        if(g_seat[g_sel].state==HGQ_SEAT_RESERVED && g_seat[g_sel].reserve_deadline_ms>now)
        {
            u32 m=(g_seat[g_sel].reserve_deadline_ms-now)/60000;
            char b[12]; snprintf(b,sizeof(b),"%lu",(unsigned long)m);
            Show_Str(gL.detail_x+6,gL.detail_y+50,40,16,(u8*)b,16,0);
            Show_Str(gL.detail_x+40,gL.detail_y+50,60,16,TXT_MIN,16,0);
        }
        else if(g_seat[g_sel].state==HGQ_SEAT_AWAY_COUNTDOWN && g_seat[g_sel].away_deadline_ms>now)
        {
            u32 m=(g_seat[g_sel].away_deadline_ms-now)/60000;
            char b[12]; snprintf(b,sizeof(b),"%lu",(unsigned long)m);
            Show_Str(gL.detail_x+6,gL.detail_y+50,40,16,(u8*)b,16,0);
            Show_Str(gL.detail_x+40,gL.detail_y+50,60,16,TXT_MIN,16,0);
        }
    }

    ui_draw_tip_only();

    /* 环境行：只清一行再画，减少闪 */
    {
        u16 lux = HGQ_UISeat_LuxRead();
        s16 t10 = HGQ_UISeat_TempRead_x10();
        s16 h10 = HGQ_UISeat_HumiRead_x10();
        char envbuf[64];
        snprintf(envbuf,sizeof(envbuf),"Lux:%u  T:%d.%d  H:%d.%d",
                 (unsigned)lux,(int)(t10/10),(int)abs(t10%10),(int)(h10/10),(int)abs(h10%10));

        ui_fill(gL.env_x+1,gL.env_y+26,gL.env_w-2,18,WHITE);
        POINT_COLOR=BLACK; BACK_COLOR=WHITE;
        Show_Str(gL.env_x+6,gL.env_y+26,gL.env_w-12,16,(u8*)envbuf,16,0);
    }
    {
        ui_fill(gL.env_x+1,gL.env_y+46,gL.env_w-2,18,WHITE);
        POINT_COLOR=BLACK; BACK_COLOR=WHITE;
        Show_Str(gL.env_x+6, gL.env_y+46, 60, 16, TXT_CLOUD, 16, 0);
        Show_Str(gL.env_x+54,gL.env_y+46, 60, 16, (g_cloud_online?TXT_ONLINE:TXT_OFFLINE), 16, 0);
    }
}

/* ========================= 业务逻辑：预约/签到/签退/超时 ========================= */
static void seat_release(u8 idx)
{
    g_seat[idx].state=HGQ_SEAT_FREE;
    g_seat[idx].uid_len=0;
    g_seat[idx].reserve_deadline_ms=0;
    g_seat[idx].away_deadline_ms=0;

    HGQ_UISeat_RelayPower(0);
    HGQ_UISeat_LightSet(0);
    HGQ_UISeat_CloudPush();

    ui_set_tip("REL",1200);

    /* 只重画该格子 + 右侧 */
    ui_draw_cell_by_index(idx, (idx==(u8)g_sel));
    g_dirty_right_dyn=1;
}

static void seat_reserve(u8 idx,const u8*uid,u8 uid_len)
{
    u32 now=HGQ_UISeat_Millis();
    g_seat[idx].state=HGQ_SEAT_RESERVED;
    memcpy(g_seat[idx].uid,uid,uid_len);
    g_seat[idx].uid_len=uid_len;
    g_seat[idx].reserve_deadline_ms=now+(u32)HGQ_UI_RESERVE_HOLD_MIN*60UL*1000UL;

    HGQ_UISeat_CloudPush();
    ui_set_tip("RES OK",1200);

    ui_draw_cell_by_index(idx, (idx==(u8)g_sel));
    g_dirty_right_dyn=1;
}

static void seat_checkin(u8 idx)
{
    g_seat[idx].state=HGQ_SEAT_IN_USE;
    g_seat[idx].reserve_deadline_ms=0;
    g_seat[idx].away_deadline_ms=0;

    HGQ_UISeat_RelayPower(1);
    if(HGQ_UISeat_LuxRead()<HGQ_UI_LUX_LIGHT_TH) HGQ_UISeat_LightSet(1);
    else HGQ_UISeat_LightSet(0);

    HGQ_UISeat_CloudPush();
    ui_set_tip("IN OK",1200);

    ui_draw_cell_by_index(idx, (idx==(u8)g_sel));
    g_dirty_right_dyn=1;
}

static void seat_tick(void)
{
    u32 now=HGQ_UISeat_Millis();
    for(u8 i=0;i<g_seat_count;i++)
    {
        if(g_seat[i].state==HGQ_SEAT_RESERVED && g_seat[i].reserve_deadline_ms && now>g_seat[i].reserve_deadline_ms)
        {
            seat_release(i);
        }

        if(g_seat[i].state==HGQ_SEAT_IN_USE)
        {
            if(HGQ_UISeat_PresenceRead()) g_seat[i].away_deadline_ms=0;
            else if(g_seat[i].away_deadline_ms==0)
            {
                g_seat[i].state=HGQ_SEAT_AWAY_COUNTDOWN;
                g_seat[i].away_deadline_ms=now+(u32)HGQ_UI_AWAY_HOLD_MIN*60UL*1000UL;

                ui_draw_cell_by_index(i, (i==(u8)g_sel));
                if((s8)i==g_sel) g_dirty_right_dyn=1;
            }
        }
        else if(g_seat[i].state==HGQ_SEAT_AWAY_COUNTDOWN)
        {
            if(HGQ_UISeat_PresenceRead())
            {
                g_seat[i].state=HGQ_SEAT_IN_USE;
                g_seat[i].away_deadline_ms=0;

                ui_draw_cell_by_index(i, (i==(u8)g_sel));
                if((s8)i==g_sel) g_dirty_right_dyn=1;
            }
            else if(g_seat[i].away_deadline_ms && now>g_seat[i].away_deadline_ms)
            {
                seat_release(i);
            }
        }
    }
}

/* ========================= 坐标 -> seat index ========================= */
static s8 ui_pos_to_seat(u16 tx,u16 ty)
{
    if(tx>=gL.left_w) return -1;
    if(ty<(gL.grid_top+UI_PAD)) return -1;

    u16 step_x = gL.cell_w + UI_PAD;
    u16 step_y = gL.cell_h + UI_PAD;

    if(tx < UI_PAD) return -1;

    u16 rel_x = tx - UI_PAD;
    u16 rel_y = ty - (gL.grid_top + UI_PAD);

    u16 c  = rel_x / step_x;
    u16 vr = rel_y / step_y;

    if(c>=gL.cols) return -1;
    if(vr>=gL.vis_rows) return -1;

    u16 idx = (u16)(g_scroll_row + (s16)vr) * gL.cols + c;
    if(idx >= g_seat_count) return -1;
    return (s8)idx;
}

/* ========================= 点击业务 ========================= */
static void ui_handle_click(u16 tx,u16 ty)
{
    /* 点座位 */
    {
        s8 idx=ui_pos_to_seat(tx,ty);
        if(idx>=0)
        {
            if(g_sel!=idx)
            {
                /* 点击只重画两格：旧选中 + 新选中 */
                s8 old = g_sel;
                g_sel = idx;

                if(old>=0) ui_draw_cell_by_index((u8)old, 0);
                ui_draw_cell_by_index((u8)g_sel, 1);

                g_dirty_right_dyn=1;
            }
            return;
        }
    }

    /* 右侧按钮 */
    {
        u8 uid[8]={0}, uid_len=0;
        u8 has_card = HGQ_UISeat_RFIDRead(uid,&uid_len);

        if(ui_hit(tx,ty,gL.btn_rx1,gL.btn_ry1,gL.btn_w,gL.btn_h)) /* 预约 */
        {
            if(g_seat[g_sel].state!=HGQ_SEAT_FREE){ui_set_tip("NOT FREE",1200);return;}
            if(!has_card){ui_set_tip("NO CARD",1200);return;}
            seat_reserve((u8)g_sel,uid,uid_len);
            return;
        }
        if(ui_hit(tx,ty,gL.btn_rx2,gL.btn_ry1,gL.btn_w,gL.btn_h)) /* 签到 */
        {
            if(g_seat[g_sel].state!=HGQ_SEAT_RESERVED){ui_set_tip("NOT RES",1200);return;}
            if(!has_card){ui_set_tip("NO CARD",1200);return;}
            if(!uid_equal(uid,uid_len,g_seat[g_sel].uid,g_seat[g_sel].uid_len)){ui_set_tip("UID ERR",1200);return;}
            seat_checkin((u8)g_sel);
            return;
        }
        if(ui_hit(tx,ty,gL.btn_rx1,gL.btn_ry2,gL.btn_w,gL.btn_h)) /* 签退 */
        {
            if(!(g_seat[g_sel].state==HGQ_SEAT_IN_USE||g_seat[g_sel].state==HGQ_SEAT_AWAY_COUNTDOWN)){ui_set_tip("NOT USE",1200);return;}
            if(has_card && g_seat[g_sel].uid_len)
                if(!uid_equal(uid,uid_len,g_seat[g_sel].uid,g_seat[g_sel].uid_len)){ui_set_tip("UID ERR",1200);return;}
            seat_release((u8)g_sel);
            return;
        }
        if(ui_hit(tx,ty,gL.btn_rx2,gL.btn_ry2,gL.btn_w,gL.btn_h)) /* 取消预约 */
        {
            if(g_seat[g_sel].state!=HGQ_SEAT_RESERVED){ui_set_tip("NOT RES",1200);return;}
            if(!has_card){ui_set_tip("NO CARD",1200);return;}
            if(!uid_equal(uid,uid_len,g_seat[g_sel].uid,g_seat[g_sel].uid_len)){ui_set_tip("UID ERR",1200);return;}
            seat_release((u8)g_sel);
            return;
        }
        if(ui_hit(tx,ty,gL.sync_x,gL.sync_y,gL.sync_w,gL.sync_h)) /* 同步 */
        {
            HGQ_UISeat_CloudPush();
            ui_set_tip("SYNC",1200);
            g_dirty_right_dyn=1;
            return;
        }
    }
}

/* ========================= 滚动核心：dy(像素) -> 行滚动 ========================= */
static void ui_scroll_by_dy(s16 dy)
{
    if(gL.total_rows <= gL.vis_rows) return;

    static s16 acc = 0;
    s16 old_row = g_scroll_row;

    /* dy>0：手指向下拖 => 内容下移 => scroll_row 减小 */
    acc += dy;

    while(acc >= (s16)PIX_PER_ROW)
    {
        g_scroll_row--;
        acc -= (s16)PIX_PER_ROW;
    }
    while(acc <= -(s16)PIX_PER_ROW)
    {
        g_scroll_row++;
        acc += (s16)PIX_PER_ROW;
    }

    {
        s16 max_row = (s16)(gL.total_rows - gL.vis_rows);
        g_scroll_row = clamp_s16(g_scroll_row, 0, max_row);
    }

    if(g_scroll_row != old_row)
        g_dirty_grid = 1; /* 只有行变化才整块重绘（大幅减少闪屏） */
}

static void ui_page_scroll(s8 dir)
{
    if(gL.total_rows <= gL.vis_rows) return;
    s16 max_row = (s16)(gL.total_rows - gL.vis_rows);

    if(dir>0) g_scroll_row += (s16)gL.vis_rows; /* 看后面 */
    else      g_scroll_row -= (s16)gL.vis_rows; /* 看前面 */

    g_scroll_row = clamp_s16(g_scroll_row, 0, max_row);
    g_dirty_grid = 1;
}

/* ========================= 惯性 ========================= */
static void inertia_stop(void){ g_inertia_on=0; g_inertia_v=0; }
static void inertia_start(s32 v_px_s)
{
    if(gL.total_rows <= gL.vis_rows) return;
    g_inertia_v = clamp_s32(v_px_s, -MAX_VELOCITY_PX_S, MAX_VELOCITY_PX_S);
    if(abs(g_inertia_v) < INERTIA_STOP_V) { inertia_stop(); return; }
    g_inertia_on = 1;
    g_inertia_last_ms = HGQ_UISeat_Millis();
}

static void inertia_tick(void)
{
    if(!g_inertia_on) return;
    if(gL.total_rows <= gL.vis_rows) { inertia_stop(); return; }

    u32 now = HGQ_UISeat_Millis();
    u32 dt_ms = now - g_inertia_last_ms;
    if(dt_ms < 10) return;
    g_inertia_last_ms = now;

    /* 速度衰减 */
    {
        s32 dv = (s32)((INERTIA_DECAY_PER_S * (s32)dt_ms) / 1000);
        if(g_inertia_v > 0) g_inertia_v = (g_inertia_v > dv) ? (g_inertia_v - dv) : 0;
        else                g_inertia_v = (g_inertia_v < -dv) ? (g_inertia_v + dv) : 0;
    }

    if(abs(g_inertia_v) < INERTIA_STOP_V)
    {
        inertia_stop();
        return;
    }

    /* 位移 dy = v * dt */
    {
        s32 dy = (g_inertia_v * (s32)dt_ms) / 1000;
        if(dy != 0) ui_scroll_by_dy((s16)dy);
    }
}

/* ========================= 外部接口：初始化/任务 ========================= */
void HGQ_UISeat_Init(u8 seat_count)
{
    if(seat_count==0) seat_count=12;
    if(seat_count>HGQ_UI_SEAT_MAX) seat_count=HGQ_UI_SEAT_MAX;

    g_seat_count = seat_count;
    g_sel = 0;
    g_scroll_row = 0;

    for(u8 i=0;i<g_seat_count;i++)
    {
        g_seat[i].state = HGQ_SEAT_FREE;
        g_seat[i].uid_len = 0;
        g_seat[i].reserve_deadline_ms = 0;
        g_seat[i].away_deadline_ms = 0;
    }

    ui_layout_calc();

    g_dirty_top = 1;
    g_dirty_grid = 1;
    g_dirty_right_static = 1;
    g_dirty_right_dyn = 1;
    g_dirty_tip = 1;

    /* 首次全屏清一下，后续尽量避免大面积清屏 */
    LCD_Clear(GRID_BG_COLOR);

    ui_draw_top();
    ui_draw_grid_all_visible();
    ui_draw_right_static();
    ui_draw_right_dynamic();

    g_tp_state = TP_IDLE;
    inertia_stop();
}

void HGQ_UISeat_Task(void)
{
    u16 x=0,y=0;
    u8 pressed;

    seat_tick();
    inertia_tick();

    pressed = HGQ_UISeat_TouchRead(&x,&y);

    if(pressed)
    {
        ui_map_touch(&x,&y);

        /* 按下打断惯性 */
        if(g_inertia_on) inertia_stop();

        if(g_tp_state == TP_IDLE)
        {
            g_tp_state = TP_DOWN;
            g_tp_down_x = x; g_tp_down_y = y;
            g_tp_last_x = x; g_tp_last_y = y;
            g_tp_last_ms = HGQ_UISeat_Millis();
            g_tp_vy = 0;
        }
        else
        {
            u32 now = HGQ_UISeat_Millis();
            u32 dt = now - g_tp_last_ms;
            if(dt == 0) dt = 1;

            s16 dy = (s16)y - (s16)g_tp_last_y;
            s16 dx = (s16)x - (s16)g_tp_last_x;

            /* 速度估计（低通） */
            {
                s32 vy_inst = (s32)dy * 1000 / (s32)dt;
                g_tp_vy = (g_tp_vy * 3 + vy_inst) / 4;
            }

            /* 进入滚动：仅左侧格子区 && 主要是竖向移动 */
            if(g_tp_state == TP_DOWN)
            {
                if(g_tp_down_x < gL.left_w && g_tp_down_y > gL.grid_top)
                {
                    if(abs(dy) >= SCROLL_START_THR && abs(dy) >= abs(dx))
                        g_tp_state = TP_SCROLLING;
                }
            }

            if(g_tp_state == TP_SCROLLING)
                ui_scroll_by_dy(dy);

            g_tp_last_x = x; g_tp_last_y = y;
            g_tp_last_ms = now;
        }
    }
    else
    {
        if(g_tp_state != TP_IDLE)
        {
            u16 up_x = g_tp_last_x;
            u16 up_y = g_tp_last_y;

            u16 mdx = (up_x > g_tp_down_x) ? (up_x - g_tp_down_x) : (g_tp_down_x - up_x);
            u16 mdy = (up_y > g_tp_down_y) ? (up_y - g_tp_down_y) : (g_tp_down_y - up_y);

            if(g_tp_state == TP_SCROLLING)
            {
                /* 甩动翻页优先 */
                if(abs(g_tp_vy) > FLING_PAGE_V_THR)
                {
                    /* 手指向上甩(vy<0) => 看后面 => +1 */
                    if(g_tp_vy < 0) ui_page_scroll(+1);
                    else            ui_page_scroll(-1);
                    inertia_stop();
                }
                else
                {
                    inertia_start(g_tp_vy);
                }
            }
            else
            {
                if(mdx < CLICK_MOVE_THR && mdy < CLICK_MOVE_THR)
                    ui_handle_click(up_x, up_y);
            }

            g_tp_state = TP_IDLE;
            g_tp_vy = 0;
        }
    }

    /* 顶部节流刷新（时间/云状态） */
    if(HGQ_UISeat_Millis() - g_last_top_ms >= 500)
    {
        g_last_top_ms = HGQ_UISeat_Millis();
        g_dirty_top = 1;
        g_dirty_right_dyn = 1; /* 倒计时更新 */
    }

    /* 刷新绘制 */
    if(g_dirty_top){ g_dirty_top=0; ui_draw_top(); }

    if(g_dirty_grid)
    {
        g_dirty_grid=0;
        ui_draw_grid_all_visible();
    }

    if(g_dirty_right_static)
    {
        g_dirty_right_static=0;
        ui_draw_right_static();
        g_dirty_right_dyn=1;
    }

    if(g_dirty_right_dyn)
    {
        g_dirty_right_dyn=0;
        ui_draw_right_dynamic();
    }
    else if(g_dirty_tip)
    {
        g_dirty_tip=0;
        ui_draw_tip_only();
    }
}

/* ========================= 外部接口：状态设置 ========================= */
void HGQ_UISeat_SetCloudOnline(u8 online)
{
    g_cloud_online = online?1:0;
    g_dirty_top=1;
    g_dirty_right_dyn=1;
}

void HGQ_UISeat_SetSeatState(u8 idx, HGQ_SEAT_STATE_E st)
{
    if(idx>=g_seat_count) return;

    if(g_seat[idx].state != st)
    {
        g_seat[idx].state = st;

        /* 只重画该格子即可 */
        ui_draw_cell_by_index(idx, (idx==(u8)g_sel));
        if((s8)idx==g_sel) g_dirty_right_dyn=1;
    }
}

/* ========================= 弱函数（你可以在 hgq_ui_seat_port.c 覆盖） ========================= */
__weak u32 HGQ_UISeat_Millis(void)
{
    static u32 t=0;
    t += 20; /* 如果你没覆盖，默认每次调用+20ms，仅用于演示 */
    return t;
}

__weak u8 HGQ_UISeat_TouchRead(u16 *x, u16 *y)
{
    (void)x; (void)y;
    return 0;
}

__weak u8 HGQ_UISeat_RFIDRead(u8 *uid, u8 *uid_len)
{
    (void)uid;
    *uid_len = 0;
    return 0;
}

__weak u8 HGQ_UISeat_PresenceRead(void){ return 1; }
__weak u16 HGQ_UISeat_LuxRead(void){ return 300; }
__weak s16 HGQ_UISeat_TempRead_x10(void){ return 250; }
__weak s16 HGQ_UISeat_HumiRead_x10(void){ return 500; }
__weak void HGQ_UISeat_RelayPower(u8 on){ (void)on; }
__weak void HGQ_UISeat_LightSet(u8 on){ (void)on; }
__weak void HGQ_UISeat_CloudPush(void){ }

