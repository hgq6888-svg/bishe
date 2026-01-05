#ifndef __HGQ_UI_SEAT_H
#define __HGQ_UI_SEAT_H

#include "sys.h"
#include "lcd.h"
#include "text.h"

/*
 * hgq_ui_seat.h
 * 座位预约/签到系统 - LCD横屏UI
 * 作者：黄光全
 * 
 * 功能概述：
 * 1. 横屏一屏展示座位总览 + 选中座位详情 + 操作按钮 + 环境数据 + 云端状态
 * 2. 支持：预约(刷卡/可扩展账号密码) / 签到(刷卡) / 签退(刷卡或触摸) / 取消预约
 * 3. 预约保留倒计时（默认15分钟），离座倒计时（默认10分钟）
 * 4. 使用中控制：继电器座位电源 + 根据光照自动控制灯光（阈值可改）
 * 5. 云端：提供上传接口（你在ESP8266里实现）
 *
 * 依赖说明：
 * 1. lcd.c/lcd.h：LCD绘图
 * 2. text.c/text.h：Show_Str（GBK双字节）显示汉字
 * 3. 触摸、RFID、传感器、云端由下方“弱函数接口”对接
 */

// ============ 可配置参数 ============
#define HGQ_UI_SEAT_MAX            128     // 最大座位数
#define HGQ_UI_RESERVE_HOLD_MIN    15     // 预约保留分钟
#define HGQ_UI_AWAY_HOLD_MIN       10     // 离座超时分钟
#define HGQ_UI_LUX_LIGHT_TH        200    // 光照阈值：低于此值自动开灯

// ============ 座位状态 ============
typedef enum
{
    HGQ_SEAT_FREE = 0,          // 空闲
    HGQ_SEAT_RESERVED,          // 已预约
    HGQ_SEAT_IN_USE,            // 使用中
    HGQ_SEAT_AWAY_COUNTDOWN     // 离座计时
}HGQ_SEAT_STATE_E;

// ============ UI对外接口 ============
void HGQ_UISeat_Init(u8 seat_count);
void HGQ_UISeat_Task(void);      // 主循环周期调用（建议 20~100ms）

// ============ 业务层可用的接口（可选） ============
void HGQ_UISeat_SetCloudOnline(u8 online);   // 设置云端在线/离线显示
void HGQ_UISeat_SetSeatState(u8 idx, HGQ_SEAT_STATE_E st);  // 你如果云端下发状态，可直接改

// ============ 下层硬件/云端对接接口（弱函数，可重载）===========
// 你可以在自己的模块里实现同名函数来覆盖（比如 hgq_rc522.c / hgq_wifi.c 等）

/**
 * @brief 获取系统毫秒计数（用于倒计时）
 * @retval 当前毫秒
 */
u32 HGQ_UISeat_Millis(void);

/**
 * @brief 触摸读取（横屏坐标系）
 * @param x 输出x
 * @param y 输出y
 * @retval 1=按下 0=无触摸
 */
u8  HGQ_UISeat_TouchRead(u16 *x, u16 *y);

/**
 * @brief RFID读卡UID
 * @param uid 输出UID字节
 * @param uid_len 输出UID长度（常见4或7）
 * @retval 1=读到卡 0=无卡
 */
u8  HGQ_UISeat_RFIDRead(u8 *uid, u8 *uid_len);

/**
 * @brief 人体存在检测（红外/TOF）
 * @retval 1=有人 0=无人
 */
u8  HGQ_UISeat_PresenceRead(void);

/**
 * @brief 读取光照（lux）
 * @retval lux
 */
u16 HGQ_UISeat_LuxRead(void);

/**
 * @brief 读取温度*10（比如253表示25.3℃）
 */
s16 HGQ_UISeat_TempRead_x10(void);

/**
 * @brief 读取湿度*10（比如605表示60.5%）
 */
s16 HGQ_UISeat_HumiRead_x10(void);

/**
 * @brief 控制座位电源继电器
 * @param on 1=开 0=关
 */
void HGQ_UISeat_RelayPower(u8 on);

/**
 * @brief 控制灯光
 * @param on 1=开 0=关
 */
void HGQ_UISeat_LightSet(u8 on);

/**
 * @brief 上传云端（座位状态/环境/记录）
 */
void HGQ_UISeat_CloudPush(void);

#endif /* __HGQ_UI_SEAT_H */

