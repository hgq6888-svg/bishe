// hgq_ui_seat_port.c
// UI 硬件适配层：触摸 + 毫秒时基 + AHT20温湿度 + (可选)在座检测/光照/云上报
//
// 注意：HGQ_UISeat_Millis / HGQ_UISeat_TouchRead / HGQ_UISeat_TempRead_x10 / HGQ_UISeat_HumiRead_x10
// 这些函数不要在 main.c 里再实现，否则会 multiply defined。

#include "hgq_ui_seat.h"
#include "touch.h"
#include "hgq_aht20.h"

#include <math.h>    // 用到 roundf，可去掉改手动四舍五入

/* ====================== 1) 毫秒时基：用 DWT 周期计数（不依赖 SysTick） ======================
 * 优点：不用改 delay.c 的 SysTick_Handler，也不受 SYSTEM_SUPPORT_OS 影响
 * 前提：SystemCoreClock 正确（一般 system_stm32f4xx.c 会维护）
 */
static u8 s_dwt_inited = 0;

static void dwt_init_once(void)
{
    if (s_dwt_inited) return;

    /* 使能 DWT CYCCNT */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    s_dwt_inited = 1;
}

u32 HGQ_UISeat_Millis(void)
{
    dwt_init_once();

    /* 周期 -> ms */
    /* 防止 SystemCoreClock 没更新，建议你在 main 初始化时调用一次 SystemCoreClockUpdate(); */
    u32 core = SystemCoreClock;
    if (core == 0) core = 168000000; // 兜底（F407 常用 168MHz）

    return (u32)(DWT->CYCCNT / (core / 1000UL));
}

/* ====================== 2) 触摸读取（你现有 touch.c 已做校准换算） ====================== */
u8 HGQ_UISeat_TouchRead(u16 *x, u16 *y)
{
    if (TP_Scan(0))
    {
        *x = tp_dev.x[0];
        *y = tp_dev.y[0];
        return 1;
    }
    return 0;
}

/* ====================== 3) AHT20 温湿度：缓存 + 限频（>=2s一次） ====================== */
static u8  s_aht20_inited = 0;
static u32 s_aht20_last_ms = 0;
static s16 s_last_t10 = 250;  // 默认 25.0℃
static s16 s_last_h10 = 500;  // 默认 50.0%

static void aht20_try_update(void)
{
    float t = 0.0f, h = 0.0f;
    u32 now = HGQ_UISeat_Millis();

    if (!s_aht20_inited)
    {
        /* 初始化一次 */
        if (HGQ_AHT20_Init() == 0)
        {
            s_aht20_inited = 1;
            s_aht20_last_ms = 0; // 让下一次立刻读取
        }
        else
        {
            /* init 失败就先用默认值 */
            return;
        }
    }

    /* AHT20 建议测量间隔 >=2秒 */
    if (s_aht20_last_ms != 0 && (now - s_aht20_last_ms) < 2000) return;

    if (HGQ_AHT20_Read(&t, &h) == 0)
    {
        /* 转为 x10，四舍五入 */
        s_last_t10 = (s16)roundf(t * 10.0f);
        s_last_h10 = (s16)roundf(h * 10.0f);

        /* 合理范围夹紧 */
        if (s_last_h10 < 0) s_last_h10 = 0;
        if (s_last_h10 > 1000) s_last_h10 = 1000;

        s_aht20_last_ms = now;
    }
    /* 失败则保留旧值 */
}

s16 HGQ_UISeat_TempRead_x10(void)
{
    aht20_try_update();
    return s_last_t10;
}

s16 HGQ_UISeat_HumiRead_x10(void)
{
    aht20_try_update();
    return s_last_h10;
}

/* ====================== 4) 其它接口：先给安全默认值（你想接再改） ====================== */

/* 光照（你有 bh1750 的话可以改成真实读数） */
u16 HGQ_UISeat_LuxRead(void)
{
    return 300;
}

/* 在座检测（有 HCSR501/TOF 再接） */
u8 HGQ_UISeat_PresenceRead(void)
{
    return 1;
}

/* 继电器/灯 */
void HGQ_UISeat_RelayPower(u8 on) { (void)on; }
void HGQ_UISeat_LightSet(u8 on)   { (void)on; }

/* RFID（接 RC522 后实现：有卡返回1并填 uid/uid_len；无卡返回0） */
u8 HGQ_UISeat_RFIDRead(u8 *uid, u8 *uid_len)
{
    (void)uid;
    *uid_len = 0;
    return 0;
}

/* 云端上报 */
void HGQ_UISeat_CloudPush(void)
{
}

