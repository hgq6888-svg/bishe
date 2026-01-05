#include "hgq_ui_seat.h"      // 包含座位UI的头文件
#include <string.h>            // 包含字符串处理函数
#include <stdio.h>             // 包含标准输入输出函数
#include <stdlib.h>            // 包含标准库函数

// 如果未定义__weak宏，则定义为弱函数属性（允许被覆盖）
#ifndef __weak
#define __weak __attribute__((weak))
#endif

/* ===================== 触摸坐标映射配置（按你实际情况调这3个） ===================== */
#ifndef HGQ_UI_TOUCH_SWAP_XY
#define HGQ_UI_TOUCH_SWAP_XY   0      // 是否交换XY坐标
#endif
#ifndef HGQ_UI_TOUCH_INV_X
#define HGQ_UI_TOUCH_INV_X     0      // 是否反转X坐标
#endif
#ifndef HGQ_UI_TOUCH_INV_Y
#define HGQ_UI_TOUCH_INV_Y     0      // 是否反转Y坐标
#endif

/* ===================== 去抖参数 ===================== */
#define HGQ_UI_TOUCH_MOVE_THR  8      // 触摸移动阈值（小于此值认为是点击）

/* ===================== GBK 字节文本（避免源文件编码影响） ===================== */
// 使用GBK编码的字节数组表示中文文本，避免编码问题
#define TXT_FREE        ((u8*)"\xBF\xD5\xCF\xD0")                 // "空闲"
#define TXT_RES         ((u8*)"\xD2\xD1\xD4\xA4\xD4\xBC")         // "已预约"
#define TXT_INUSE       ((u8*)"\xCA\xB9\xD3\xC3\xD6\xD0")         // "使用中"
#define TXT_AWAY        ((u8*)"\xC0\xEB\xD7\xF9\xBC\xC6\xCA\xB1") // "离座计时"

// UI标签文本
#define TXT_SEAT        ((u8*)"\xD7\xF9\xCE\xBB")                 // "座位"
#define TXT_STATUS      ((u8*)"\xD7\xB4\xCC\xAC\x3A")             // "状态:"
#define TXT_ENV         ((u8*)"\xBB\xB7\xBE\xB3\x3A")             // "环境:"
#define TXT_CLOUD       ((u8*)"\xD4\xC6\xB6\xCB\x3A")             // "云端:"
#define TXT_ONLINE      ((u8*)"\xD4\xDA\xCF\xDF")                 // "在线"
#define TXT_OFFLINE     ((u8*)"\xC0\xEB\xCF\xDF")                 // "离线"

// 按钮文本
#define TXT_RESERVE     ((u8*)"\xD4\xA4\xD4\xBC")                 // "预约"
#define TXT_CHECKIN     ((u8*)"\xC7\xA9\xB5\xBD")                 // "签到"
#define TXT_CHECKOUT    ((u8*)"\xC7\xA9\xCD\xCB")                 // "签退"
#define TXT_CANCEL      ((u8*)"\xC8\xA1\xCF\xFB")                 // "取消"

/* ===================== UI布局参数 ===================== */
static u16 gW, gH;                     // 屏幕宽度和高度
static const u16 TOP_H = 28;           // 顶部状态栏高度

// UI布局结构体
typedef struct
{
    u16 left_w;     // 左侧网格区域宽度
    u16 left_h;     // 左侧网格区域高度
    u8  cols;       // 网格列数
    u8  rows;       // 网格行数
    u16 pad;        // 单元格之间的填充
    u16 grid_top;   // 网格顶部起始位置
    u16 cell_w;     // 单元格宽度
    u16 cell_h;     // 单元格高度
} HGQ_UI_LAYOUT_T;

static HGQ_UI_LAYOUT_T gL;            // 全局布局实例

/* ===================== 数据 ===================== */
// 座位数据结构体
typedef struct
{
    HGQ_SEAT_STATE_E state;           // 座位状态
    u8  uid[8];                       // 用户ID（RFID）
    u8  uid_len;                      // 用户ID长度
    
    u32 reserve_deadline_ms;          // 预约截止时间（毫秒）
    u32 away_deadline_ms;             // 离座计时截止时间
    u32 last_here_ms;                 // 最后检测到人在座位的时间
} HGQ_SEAT_T;

static HGQ_SEAT_T g_seat[HGQ_UI_SEAT_MAX];  // 座位数组
static u8  g_seat_count = 12;               // 座位总数
static s8  g_sel = 0;                       // 当前选中的座位索引
static u8  g_cloud_online = 1;              // 云端在线状态

// 脏标记（需要重绘的区域）
static u8  g_dirty_top = 1;                 // 顶部状态栏需要重绘
static u8  g_dirty_right_full = 1;          // 右侧区域需要完整重绘
static u8  g_dirty_right_dyn  = 1;          // 右侧动态内容需要重绘
static u8  g_dirty_cell[HGQ_UI_SEAT_MAX];   // 每个座位单元格需要重绘
static u32 g_last_top_ms = 0;               // 上次更新顶部状态栏的时间

// 触摸状态机
static u8  g_tp_down = 0;                   // 触摸是否按下
static u16 g_tp_down_x = 0, g_tp_down_y = 0; // 触摸按下时的坐标
static u16 g_tp_last_x = 0, g_tp_last_y = 0; // 触摸最后的位置

/* 业务参数 */
#define RESERVE_HOLD_MS    (15UL*60UL*1000UL)  // 预约保持时间（15分钟）
#define AWAY_HOLD_MS       (10UL*60UL*1000UL)  // 离座保持时间（10分钟）
#define LUX_LIGHT_TH       200                 // 光照阈值（低于此值开灯）

/* ===================== 工具函数 ===================== */
// 填充矩形区域
static void HGQ_UISeat_Fill(u16 x,u16 y,u16 w,u16 h,u16 color)
{
    LCD_Fill(x, y, x + w - 1, y + h - 1, color);
}

// 检测点是否在矩形区域内
static u8 HGQ_UISeat_Hit(u16 px,u16 py,u16 x,u16 y,u16 w,u16 h)
{
    return (px>=x && px<x+w && py>=y && py<y+h);
}

// 标记单元格需要重绘
static void HGQ_UISeat_MarkCellDirty(u8 idx)
{
    if(idx < g_seat_count) g_dirty_cell[idx] = 1;
}

// 映射触摸坐标（根据配置进行坐标变换）
static void HGQ_UISeat_MapTouch(u16 *x, u16 *y)
{
#if HGQ_UI_TOUCH_SWAP_XY
    u16 t = *x; *x = *y; *y = t;  // 交换XY坐标
#endif
#if HGQ_UI_TOUCH_INV_X
    if (*x < gW) *x = (gW - 1) - *x;  // 反转X坐标
#endif
#if HGQ_UI_TOUCH_INV_Y
    if (*y < gH) *y = (gH - 1) - *y;  // 反转Y坐标
#endif
    // 边界检查
    if (*x >= gW) *x = gW - 1;
    if (*y >= gH) *y = gH - 1;
}

// 计算UI布局
static void HGQ_UISeat_LayoutCalc(void)
{
    gW = lcddev.width;    // 获取LCD宽度
    gH = lcddev.height;   // 获取LCD高度

    gL.left_w = (gW * 62) / 100;  // 左侧占屏幕62%
    gL.left_h = gH - TOP_H;       // 左侧高度为屏幕高度减去状态栏

    gL.cols = 4;  // 默认4列
    if (g_seat_count > 16) gL.cols = 6;  // 座位多时用6列

    gL.rows = (g_seat_count + gL.cols - 1) / gL.cols;  // 计算行数（向上取整）
    if (gL.rows == 0) gL.rows = 1;  // 确保至少一行

    gL.pad = 6;  // 内边距
    gL.grid_top = TOP_H + 26;  // 网格顶部位置

    // 计算单元格大小
    gL.cell_w = (gL.left_w - gL.pad * (gL.cols + 1)) / gL.cols;
    gL.cell_h = (gL.left_h - 26 - gL.pad * (gL.rows + 1)) / gL.rows;
}

// 根据座位状态返回颜色
static u16 HGQ_UISeat_StateColor(HGQ_SEAT_STATE_E st)
{
    switch(st)
    {
        case HGQ_SEAT_FREE:           return GREEN;   // 空闲 - 绿色
        case HGQ_SEAT_RESERVED:       return YELLOW;  // 已预约 - 黄色
        case HGQ_SEAT_IN_USE:         return 0xFD20;  // 使用中 - 橙色
        case HGQ_SEAT_AWAY_COUNTDOWN: return RED;     // 离座计时 - 红色
        default:                      return WHITE;   // 默认 - 白色
    }
}

// 根据座位状态返回文本
static u8* HGQ_UISeat_StateText(HGQ_SEAT_STATE_E st)
{
    switch(st)
    {
        case HGQ_SEAT_FREE:           return TXT_FREE;    // "空闲"
        case HGQ_SEAT_RESERVED:       return TXT_RES;     // "已预约"
        case HGQ_SEAT_IN_USE:         return TXT_INUSE;   // "使用中"
        case HGQ_SEAT_AWAY_COUNTDOWN: return TXT_AWAY;    // "离座计时"
        default:                      return (u8*)"?";    // 未知状态
    }
}

// 比较两个用户ID是否相等
static u8 HGQ_UISeat_UIDEqual(const u8 *a,u8 al,const u8 *b,u8 bl)
{
    if(al!=bl) return 0;  // 长度不同直接返回不相等
    return (memcmp(a,b,al)==0)?1:0;  // 比较内存内容
}

/* ===================== 绘制 ===================== */
// 绘制顶部状态栏
static void HGQ_UISeat_DrawTop(void)
{
    HGQ_UISeat_Fill(0, 0, gW, TOP_H, BLUE);  // 蓝色背景

    POINT_COLOR = WHITE;  // 设置前景色为白色
    BACK_COLOR  = BLUE;   // 设置背景色为蓝色

    // 显示"云端:"标签
    Show_Str(6, 6, 120, TOP_H, TXT_CLOUD, 16, 0);
    // 显示云端在线状态
    Show_Str(60, 6, 80, TOP_H, (g_cloud_online ? TXT_ONLINE : TXT_OFFLINE), 16, 0);

    // 显示时间（秒数）
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "t=%lu", (unsigned long)(HGQ_UISeat_Millis()/1000));
        Show_Str(gW-100, 6, 100, TOP_H, (u8*)buf, 16, 0);
    }
}

// 绘制单个座位单元格
static void HGQ_UISeat_DrawSeatCell(u8 i)
{
    if(i >= g_seat_count) return;  // 索引检查

    u8 r = i / gL.cols;  // 计算行索引
    u8 c = i % gL.cols;  // 计算列索引

    // 计算单元格位置
    u16 x = gL.pad + c * (gL.cell_w + gL.pad);
    u16 y = gL.grid_top + gL.pad + r * (gL.cell_h + gL.pad);

    // 根据状态设置背景色
    u16 bg = HGQ_UISeat_StateColor(g_seat[i].state);
    HGQ_UISeat_Fill(x, y, gL.cell_w, gL.cell_h, bg);

    // 设置边框颜色（选中时为白色，否则为黑色）
    POINT_COLOR = (i == (u8)g_sel) ? WHITE : BLACK;
    BACK_COLOR  = bg;
    LCD_DrawRectangle(x, y, x + gL.cell_w - 1, y + gL.cell_h - 1);  // 绘制边框

    // 显示座位编号
    {
        char num[10];
        snprintf(num, sizeof(num), "#%02d", i + 1);
        POINT_COLOR = BLACK; BACK_COLOR = bg;
        Show_Str(x+4, y+4, gL.cell_w-8, 16, (u8*)num, 16, 0);
    }

    // 显示座位状态文本
    POINT_COLOR = BLACK; BACK_COLOR = bg;
    Show_Str(x+4, y+22, gL.cell_w-8, gL.cell_h-24, HGQ_UISeat_StateText(g_seat[i].state), 16, 0);
}

// 绘制完整的座位网格
static void HGQ_UISeat_DrawSeatGridFull(void)
{
    HGQ_UISeat_Fill(0, TOP_H, gL.left_w, gL.left_h, BLACK);  // 黑色背景

    // 显示"座位"标题
    POINT_COLOR = CYAN; BACK_COLOR = BLACK;
    Show_Str(6, TOP_H+4, gL.left_w-12, 20, TXT_SEAT, 16, 0);

    // 绘制所有座位单元格
    for(u8 i=0;i<g_seat_count;i++)
    {
        g_dirty_cell[i] = 0;  // 清除脏标记
        HGQ_UISeat_DrawSeatCell(i);
    }
}

// 绘制按钮
static void HGQ_UISeat_DrawBtn(u16 x,u16 y,u16 w,u16 h,u8 *txt,u16 bg,u16 fg)
{
    HGQ_UISeat_Fill(x,y,w,h,bg);  // 填充背景色
    POINT_COLOR = fg; BACK_COLOR = bg;
    LCD_DrawRectangle(x,y,x+w-1,y+h-1);  // 绘制边框
    Show_Str(x+4, y+(h-16)/2, w-8, h, txt, 16, 0);  // 居中显示文本
}

// 绘制完整的右侧区域
static void HGQ_UISeat_DrawRightFull(void)
{
    u16 rx = gL.left_w;   // 右侧区域起始X坐标
    u16 rw = gW - rx;     // 右侧区域宽度
    u16 rh = gH - TOP_H;  // 右侧区域高度

    HGQ_UISeat_Fill(rx, TOP_H, rw, rh, 0x7BEF);  // 灰色背景

    /* 详情框 */
    {
        u16 box_x = rx + 6, box_y = TOP_H + 6, box_w = rw - 12, box_h = 86;
        HGQ_UISeat_Fill(box_x, box_y, box_w, box_h, WHITE);  // 白色背景
        POINT_COLOR = BLACK; BACK_COLOR = WHITE;
        LCD_DrawRectangle(box_x, box_y, box_x+box_w-1, box_y+box_h-1);  // 黑色边框
    }

    /* 按钮区 */
    {
        u16 box_x = rx + 6, box_y = TOP_H + 6, box_w = rw - 12;
        u16 btn_w = (box_w - 6)/2;  // 按钮宽度
        u16 btn_h = 42;              // 按钮高度

        // 计算四个按钮的位置
        u16 btn_x1 = box_x;
        u16 btn_x2 = box_x + btn_w + 6;
        u16 btn_y1 = box_y + 86 + 10;
        u16 btn_y2 = btn_y1 + btn_h + 6;

        // 绘制四个按钮
        HGQ_UISeat_DrawBtn(btn_x1, btn_y1, btn_w, btn_h, TXT_RESERVE,  GREEN,  BLACK);   // 预约按钮
        HGQ_UISeat_DrawBtn(btn_x2, btn_y1, btn_w, btn_h, TXT_CHECKIN,  CYAN,   BLACK);   // 签到按钮
        HGQ_UISeat_DrawBtn(btn_x1, btn_y2, btn_w, btn_h, TXT_CHECKOUT, RED,    WHITE);   // 签退按钮
        HGQ_UISeat_DrawBtn(btn_x2, btn_y2, btn_w, btn_h, TXT_CANCEL,   YELLOW, BLACK);   // 取消按钮

        /* 环境框 */
        {
            u16 env_y = btn_y2 + btn_h + 10;
            u16 env_h = gH - env_y - 6;
            HGQ_UISeat_Fill(box_x, env_y, box_w, env_h, WHITE);  // 白色背景
            POINT_COLOR = BLACK; BACK_COLOR = WHITE;
            LCD_DrawRectangle(box_x, env_y, box_x+box_w-1, gH-7);  // 黑色边框

            Show_Str(box_x+6, env_y+6, 120, 16, TXT_ENV, 16, 0);  // "环境:"标签
        }
    }

    g_dirty_right_dyn = 1;  // 标记动态内容需要更新
}

// 绘制右侧动态内容（座位详情）
static void HGQ_UISeat_DrawRightDynamic(void)
{
    u16 rx = gL.left_w;   // 右侧区域起始X坐标
    u16 rw = gW - rx;     // 右侧区域宽度

    u16 box_x = rx + 6, box_y = TOP_H + 6, box_w = rw - 12, box_h = 86;

    // 清除详情框内容
    HGQ_UISeat_Fill(box_x+1, box_y+1, box_w-2, box_h-2, WHITE);

    POINT_COLOR = BLACK; BACK_COLOR = WHITE;

    // 显示座位编号标题
    {
        char title[24];
        snprintf(title, sizeof(title), "Seat #%02d", (int)g_sel+1);
        Show_Str(box_x+6, box_y+6, box_w-12, 16, (u8*)title, 16, 0);
    }

    // 显示状态
    Show_Str(box_x+6, box_y+26, 80, 16, TXT_STATUS, 16, 0);  // "状态:"
    Show_Str(box_x+60,box_y+26, box_w-66, 16, HGQ_UISeat_StateText(g_seat[g_sel].state), 16, 0);

    /* 倒计时显示 */
    {
        u32 now = HGQ_UISeat_Millis();
        // 预约倒计时
        if(g_seat[g_sel].state == HGQ_SEAT_RESERVED && g_seat[g_sel].reserve_deadline_ms > now)
        {
            u32 m = (g_seat[g_sel].reserve_deadline_ms - now)/60000;  // 计算剩余分钟数
            char b[24];
            snprintf(b,sizeof(b),"Remain:%lu", (unsigned long)m);
            Show_Str(box_x+6, box_y+52, box_w-12, 16, (u8*)b, 16, 0);
        }
        // 离座倒计时
        else if(g_seat[g_sel].state == HGQ_SEAT_AWAY_COUNTDOWN && g_seat[g_sel].away_deadline_ms > now)
        {
            u32 m = (g_seat[g_sel].away_deadline_ms - now)/60000;  // 计算剩余分钟数
            char b[24];
            snprintf(b,sizeof(b),"Away:%lu", (unsigned long)m);
            Show_Str(box_x+6, box_y+52, box_w-12, 16, (u8*)b, 16, 0);
        }
    }

    /* 环境信息显示 */
    {
        u16 box_x2 = rx + 6, box_w2 = rw - 12;
        u16 btn_h = 42;
        u16 env_y = (TOP_H + 6) + 86 + 10 + btn_h + 6 + btn_h + 10;  // 计算环境框位置

        if(env_y < gH-20)  // 确保在屏幕范围内
        {
            // 读取传感器数据
            u16 lux = HGQ_UISeat_LuxRead();           // 光照强度
            s16 t10 = HGQ_UISeat_TempRead_x10();      // 温度（放大10倍）
            s16 h10 = HGQ_UISeat_HumiRead_x10();      // 湿度（放大10倍）

            // 格式化环境信息字符串
            char envbuf[64];
            snprintf(envbuf,sizeof(envbuf),"Lux:%u T:%d.%d H:%d.%d",
                     (unsigned)lux,
                     (int)(t10/10), (int)abs(t10%10),  // 温度整数和小数部分
                     (int)(h10/10), (int)abs(h10%10)); // 湿度整数和小数部分

            // 清除之前的环境信息
            HGQ_UISeat_Fill(box_x2+1, env_y+26, box_w2-2, 18, WHITE);
            POINT_COLOR = BLACK; BACK_COLOR = WHITE;
            // 显示环境信息
            Show_Str(box_x2+6, env_y+26, box_w2-12, 16, (u8*)envbuf, 16, 0);
        }
    }
}

/* ===================== 业务动作 ===================== */
// 释放座位（恢复为空闲状态）
static void HGQ_UISeat_Release(u8 idx)
{
    g_seat[idx].state = HGQ_SEAT_FREE;          // 设置为空闲状态
    g_seat[idx].uid_len = 0;                    // 清空用户ID
    g_seat[idx].reserve_deadline_ms = 0;        // 清除预约截止时间
    g_seat[idx].away_deadline_ms = 0;           // 清除离座截止时间
    g_seat[idx].last_here_ms = 0;               // 清除最后在座时间

    HGQ_UISeat_RelayPower(0);  // 关闭继电器电源
    HGQ_UISeat_LightSet(0);    // 关闭灯光

    HGQ_UISeat_MarkCellDirty(idx);  // 标记单元格需要重绘
    g_dirty_right_dyn = 1;           // 标记右侧动态内容需要更新
}

// 预约座位
static void HGQ_UISeat_Reserve(u8 idx, const u8 *uid, u8 uid_len)
{
    g_seat[idx].state = HGQ_SEAT_RESERVED;                    // 设置为已预约状态
    memcpy(g_seat[idx].uid, uid, uid_len);                   // 复制用户ID
    g_seat[idx].uid_len = uid_len;                           // 设置用户ID长度
    g_seat[idx].reserve_deadline_ms = HGQ_UISeat_Millis() + RESERVE_HOLD_MS;  // 设置预约截止时间

    HGQ_UISeat_MarkCellDirty(idx);  // 标记单元格需要重绘
    g_dirty_right_dyn = 1;           // 标记右侧动态内容需要更新
}

// 签到座位
static void HGQ_UISeat_CheckIn(u8 idx)
{
    g_seat[idx].state = HGQ_SEAT_IN_USE;           // 设置为使用中状态
    g_seat[idx].last_here_ms = HGQ_UISeat_Millis(); // 记录最后在座时间
    g_seat[idx].away_deadline_ms = 0;              // 清除离座截止时间

    HGQ_UISeat_RelayPower(1);  // 打开继电器电源

    // 根据光照强度控制灯光
    if(HGQ_UISeat_LuxRead() < LUX_LIGHT_TH) HGQ_UISeat_LightSet(1);  // 光照不足时开灯
    else HGQ_UISeat_LightSet(0);                                     // 否则关灯

    HGQ_UISeat_MarkCellDirty(idx);  // 标记单元格需要重绘
    g_dirty_right_dyn = 1;           // 标记右侧动态内容需要更新
}

// 定时任务处理
static void HGQ_UISeat_Tick(void)
{
    u32 now = HGQ_UISeat_Millis();  // 获取当前时间

    // 遍历所有座位
    for(u8 i=0;i<g_seat_count;i++)
    {
        // 处理预约超时
        if(g_seat[i].state == HGQ_SEAT_RESERVED && g_seat[i].reserve_deadline_ms)
        {
            if(now > g_seat[i].reserve_deadline_ms)  // 超时检查
            {
                HGQ_UISeat_Release(i);    // 释放座位
                HGQ_UISeat_CloudPush();   // 推送状态到云端
            }
        }

        // 处理使用中座位的离座检测
        if(g_seat[i].state == HGQ_SEAT_IN_USE)
        {
            if(HGQ_UISeat_PresenceRead())  // 检测到人在座位
            {
                g_seat[i].last_here_ms = now;           // 更新最后在座时间
                g_seat[i].away_deadline_ms = 0;         // 清除离座计时
            }
            else  // 未检测到人
            {
                if(g_seat[i].away_deadline_ms == 0)  // 首次检测到人离开
                {
                    g_seat[i].away_deadline_ms = now + AWAY_HOLD_MS;  // 开始离座计时
                    g_seat[i].state = HGQ_SEAT_AWAY_COUNTDOWN;        // 切换到离座计时状态
                    HGQ_UISeat_MarkCellDirty(i);                      // 标记需要重绘
                    g_dirty_right_dyn = 1;                            // 标记右侧需要更新
                }
            }
        }
        // 处理离座计时状态
        else if(g_seat[i].state == HGQ_SEAT_AWAY_COUNTDOWN)
        {
            if(HGQ_UISeat_PresenceRead())  // 检测到人回来了
            {
                g_seat[i].state = HGQ_SEAT_IN_USE;     // 恢复使用中状态
                g_seat[i].away_deadline_ms = 0;        // 清除离座计时
                g_seat[i].last_here_ms = now;          // 更新最后在座时间
                HGQ_UISeat_MarkCellDirty(i);           // 标记需要重绘
                g_dirty_right_dyn = 1;                 // 标记右侧需要更新
            }
            else  // 人仍然不在
            {
                if(g_seat[i].away_deadline_ms && now > g_seat[i].away_deadline_ms)  // 离座超时
                {
                    HGQ_UISeat_Release(i);    // 释放座位
                    HGQ_UISeat_CloudPush();   // 推送状态到云端
                }
            }
        }
    }
}

// 处理点击事件
static void HGQ_UISeat_HandleClick(u16 tx,u16 ty)
{
    /* 点击座位格子 */
    if(tx < gL.left_w && ty > TOP_H)  // 在左侧网格区域内
    {
        // 遍历所有座位查找点击的是哪个
        for(u8 i=0;i<g_seat_count;i++)
        {
            u8 r = i / gL.cols;
            u8 c = i % gL.cols;

            // 计算单元格位置
            u16 x = gL.pad + c * (gL.cell_w + gL.pad);
            u16 y = gL.grid_top + gL.pad + r * (gL.cell_h + gL.pad);

            // 检测点击是否在此单元格内
            if(HGQ_UISeat_Hit(tx, ty, x, y, gL.cell_w, gL.cell_h))
            {
                if(g_sel != (s8)i)  // 选择了不同的座位
                {
                    HGQ_UISeat_MarkCellDirty((u8)g_sel);  // 标记原选中座位需要重绘
                    g_sel = (s8)i;                         // 更新选中座位索引
                    HGQ_UISeat_MarkCellDirty((u8)g_sel);  // 标记新选中座位需要重绘
                    g_dirty_right_dyn = 1;                 // 标记右侧需要更新
                }
                return;  // 处理完成，返回
            }
        }
        return;
    }

    /* 点击右侧按钮区 */
    {
        u16 rx = gL.left_w;
        u16 rw = gW - rx;

        u16 box_x = rx + 6, box_y = TOP_H + 6, box_w = rw - 12;
        u16 btn_w = (box_w - 6)/2;  // 按钮宽度
        u16 btn_h = 42;              // 按钮高度

        // 计算四个按钮的位置
        u16 btn_x1 = box_x;
        u16 btn_x2 = box_x + btn_w + 6;
        u16 btn_y1 = box_y + 86 + 10;
        u16 btn_y2 = btn_y1 + btn_h + 6;

        // 读取RFID卡号
        u8 uid[8], uid_len = 0;
        u8 has_card = HGQ_UISeat_RFIDRead(uid, &uid_len);

        /* 预约按钮 */
        if(HGQ_UISeat_Hit(tx,ty,btn_x1,btn_y1,btn_w,btn_h))
        {
            // 条件：座位空闲且有RFID卡
            if(g_seat[g_sel].state == HGQ_SEAT_FREE && has_card)
            {
                HGQ_UISeat_Reserve((u8)g_sel, uid, uid_len);  // 预约座位
                HGQ_UISeat_CloudPush();                       // 推送状态到云端
            }
            return;
        }

        /* 签到按钮 */
        if(HGQ_UISeat_Hit(tx,ty,btn_x2,btn_y1,btn_w,btn_h))
        {
            // 条件：座位已预约、有RFID卡且卡号匹配
            if(g_seat[g_sel].state == HGQ_SEAT_RESERVED && has_card)
            {
                if(HGQ_UISeat_UIDEqual(g_seat[g_sel].uid, g_seat[g_sel].uid_len, uid, uid_len))
                {
                    HGQ_UISeat_CheckIn((u8)g_sel);  // 签到座位
                    HGQ_UISeat_CloudPush();         // 推送状态到云端
                }
            }
            return;
        }

        /* 签退按钮 */
        if(HGQ_UISeat_Hit(tx,ty,btn_x1,btn_y2,btn_w,btn_h))
        {
            // 条件：座位使用中或离座计时中
            if(g_seat[g_sel].state == HGQ_SEAT_IN_USE || g_seat[g_sel].state == HGQ_SEAT_AWAY_COUNTDOWN)
            {
                HGQ_UISeat_Release((u8)g_sel);  // 释放座位
                HGQ_UISeat_CloudPush();         // 推送状态到云端
            }
            return;
        }

        /* 取消按钮 */
        if(HGQ_UISeat_Hit(tx,ty,btn_x2,btn_y2,btn_w,btn_h))
        {
            // 条件：座位已预约
            if(g_seat[g_sel].state == HGQ_SEAT_RESERVED)
            {
                HGQ_UISeat_Release((u8)g_sel);  // 释放座位（取消预约）
                HGQ_UISeat_CloudPush();         // 推送状态到云端
            }
            return;
        }
    }
}

/* ===================== 对外接口 ===================== */
// 初始化座位UI
void HGQ_UISeat_Init(u8 seat_count)
{
    if(seat_count == 0) seat_count = 1;  // 最少1个座位
    if(seat_count > HGQ_UI_SEAT_MAX) seat_count = HGQ_UI_SEAT_MAX;  // 不超过最大值

    g_seat_count = seat_count;  // 设置座位总数
    g_sel = 0;                  // 默认选中第一个座位

    // 初始化所有座位数据
    for(u8 i=0;i<HGQ_UI_SEAT_MAX;i++)
    {
        g_seat[i].state = HGQ_SEAT_FREE;  // 设置为空闲状态
        g_seat[i].uid_len = 0;            // 用户ID长度为0
        g_seat[i].reserve_deadline_ms = 0;  // 预约截止时间为0
        g_seat[i].away_deadline_ms = 0;     // 离座截止时间为0
        g_seat[i].last_here_ms = 0;         // 最后在座时间为0
        g_dirty_cell[i] = 1;                // 标记所有单元格需要重绘
    }

    HGQ_UISeat_LayoutCalc();  // 计算UI布局

    // 初始化脏标记
    g_dirty_top = 1;
    g_dirty_right_full = 1;
    g_dirty_right_dyn  = 1;
    g_last_top_ms = 0;
    g_tp_down = 0;  // 触摸状态为未按下

    LCD_Clear(BLACK);               // 清屏为黑色
    HGQ_UISeat_DrawSeatGridFull();  // 绘制完整的座位网格
}

// 主任务函数（需要周期性调用）
void HGQ_UISeat_Task(void)
{
    u16 x, y;
    u8  pressed;

    HGQ_UISeat_Tick();  // 执行定时业务逻辑

    // 读取触摸状态
    pressed = HGQ_UISeat_TouchRead(&x, &y);
    if(pressed)  // 触摸按下
    {
        HGQ_UISeat_MapTouch(&x, &y);  // 映射触摸坐标
        g_tp_last_x = x;              // 记录最后触摸位置
        g_tp_last_y = y;

        if(!g_tp_down)  // 之前未按下，现在是按下事件
        {
            g_tp_down = 1;          // 设置按下标志
            g_tp_down_x = x;        // 记录按下位置
            g_tp_down_y = y;
        }
    }
    else  // 触摸抬起
    {
        if(g_tp_down)  // 之前按下，现在是抬起事件
        {
            // 计算按下和抬起之间的移动距离
            u16 dx = (g_tp_last_x > g_tp_down_x) ? (g_tp_last_x - g_tp_down_x) : (g_tp_down_x - g_tp_last_x);
            u16 dy = (g_tp_last_y > g_tp_down_y) ? (g_tp_last_y - g_tp_down_y) : (g_tp_down_y - g_tp_last_y);

            // 移动距离小于阈值时认为是点击事件
            if(dx < HGQ_UI_TOUCH_MOVE_THR && dy < HGQ_UI_TOUCH_MOVE_THR)
            {
                HGQ_UISeat_HandleClick(g_tp_last_x, g_tp_last_y);  // 处理点击
            }
            g_tp_down = 0;  // 清除按下标志
        }
    }

    // 每500ms更新一次顶部状态栏
    if(HGQ_UISeat_Millis() - g_last_top_ms >= 500)
    {
        g_last_top_ms = HGQ_UISeat_Millis();  // 更新最后更新时间
        g_dirty_top = 1;                      // 标记顶部需要更新
        g_dirty_right_dyn = 1;                // 标记右侧动态内容需要更新
    }

    // 根据脏标记更新UI
    if(g_dirty_top)  // 顶部需要更新
    {
        g_dirty_top = 0;
        HGQ_UISeat_DrawTop();  // 绘制顶部状态栏
    }

    // 更新需要重绘的座位单元格
    for(u8 i=0;i<g_seat_count;i++)
    {
        if(g_dirty_cell[i])  // 单元格需要重绘
        {
            g_dirty_cell[i] = 0;  // 清除脏标记
            HGQ_UISeat_DrawSeatCell(i);  // 绘制单元格
        }
    }

    // 更新右侧区域
    if(g_dirty_right_full)  // 右侧需要完整重绘
    {
        g_dirty_right_full = 0;
        HGQ_UISeat_DrawRightFull();  // 绘制完整右侧区域
        g_dirty_right_dyn = 0;       // 同时更新了动态内容
    }
    else if(g_dirty_right_dyn)  // 仅动态内容需要更新
    {
        g_dirty_right_dyn = 0;
        HGQ_UISeat_DrawRightDynamic();  // 绘制右侧动态内容
    }
}

// 设置云端在线状态
void HGQ_UISeat_SetCloudOnline(u8 online)
{
    g_cloud_online = online ? 1 : 0;  // 更新云端在线状态
    g_dirty_top = 1;                   // 标记顶部需要更新
    g_dirty_right_dyn = 1;             // 标记右侧动态内容需要更新
}

/* ===================== 弱函数（你可覆盖） ===================== */
// 获取当前时间（毫秒）- 需要用户实现
__weak u32 HGQ_UISeat_Millis(void)
{
    static u32 t = 0;
    t += 20;  // 模拟时间递增
    return t;
}

// 读取触摸坐标 - 需要用户实现
__weak u8 HGQ_UISeat_TouchRead(u16 *x, u16 *y)
{
    (void)x; (void)y;
    return 0;  // 默认返回未触摸
}

// 读取RFID卡号 - 需要用户实现
__weak u8 HGQ_UISeat_RFIDRead(u8 *uid, u8 *uid_len)
{
    (void)uid; (void)uid_len;
    return 0;  // 默认返回无卡
}

// 读取人体存在检测 - 需要用户实现
__weak u8 HGQ_UISeat_PresenceRead(void)
{
    return 1;  // 默认返回有人在
}

// 读取光照强度 - 需要用户实现
__weak u16 HGQ_UISeat_LuxRead(void)
{
    return 300;  // 默认返回300 lux
}

// 读取温度（放大10倍） - 需要用户实现
__weak s16 HGQ_UISeat_TempRead_x10(void)
{
    return 250;  // 默认返回25.0°C
}

// 读取湿度（放大10倍） - 需要用户实现
__weak s16 HGQ_UISeat_HumiRead_x10(void)
{
    return 500;  // 默认返回50.0%
}

// 控制继电器电源 - 需要用户实现
__weak void HGQ_UISeat_RelayPower(u8 on)
{
    (void)on;  // 默认无操作
}

// 控制灯光 - 需要用户实现
__weak void HGQ_UISeat_LightSet(u8 on)
{
    (void)on;  // 默认无操作
}

// 推送状态到云端 - 需要用户实现
__weak void HGQ_UISeat_CloudPush(void)
{
    // 默认空实现
}
