// 头文件包含
#include "stm32f4xx.h"        // STM32F4标准库
#include "delay.h"            // 延时函数
#include "usart.h"            // 串口通信
#include <stdio.h>            // 标准输入输出
#include <string.h>           // 字符串处理
#include <stdlib.h>           // 标准库函数

#include "lcd.h"              // LCD显示屏驱动
#include "touch.h"            // 触摸屏驱动
#include "w25qxx.h"           // SPI Flash存储
#include "fontupd.h"          // 字体更新
#include "text.h"             // 文本显示

#include "led.h"              // LED控制
#include "hgq_ui.h"           // 用户界面

#include "hgq_vl53l0x.h"      // 激光测距传感器
#include "hgq_aht20.h"        // 温湿度传感器
#include "hgq_bh1750.h"       // 光照传感器
#include "hgq_rc522.h"        // RFID读卡器

#include "hgq_esp8266.h"      // ESP8266 WiFi模块

/* ================== 需要你改的参数 ================== */
#define WIFI_SSID       "9636"                  // WiFi名称
#define WIFI_PASS       "123456789abcc"         // WiFi密码

#define MQTT_BROKER     "1.14.163.35"           // MQTT服务器地址
#define MQTT_PORT       1883                    // MQTT端口
#define MQTT_USER       "test01"                // MQTT用户名
#define MQTT_PASS       ""                      // MQTT密码（空）

/* 设备ID：必须和 server.py 的 topic 解析一致：studyroom/<dev_id>/xxx */
#define DEV_ID          "A15"                   // 设备ID

/* seat：网页显示用（可写 A区-15号）。若不需要可留空 "" */
#define SEAT_NAME       "A-15"  /* "A区-15号" GBK 字节（16进制编码）*/

/* TOF 判定"有物品"阈值（mm）：距离小于此值 -> object=1 */
#define TOF_OCCUPIED_MM 380                     // TOF占位检测阈值（毫米）

/* 发布周期 */
#define PUB_TELE_MS     1000                    // 遥测数据发布周期（1秒）
#define PUB_STATE_MS    1500                    // 状态发布周期（1.5秒）
#define PUB_HEART_MS    30000                   // 心跳包周期（30秒）
#define PUB_TOF_EVT_MS  1200                    // TOF事件发布周期（1.2秒）

/* 任务周期(ms) */
#define PERIOD_RFID_MS   100                    // RFID轮询周期
#define PERIOD_TOF_MS    200                    // TOF传感器周期
#define PERIOD_AHT_MS    500                    // 温湿度传感器周期
#define PERIOD_LUX_MS    500                    // 光照传感器周期
// #define PERIOD_TOUCH_MS  10                  // 【优化】移除触摸周期限制，全速运行
#define UI_REFRESH_MS    50                     // UI刷新周期

/* ================== MQTT Topic 组装 ================== */
static void Topic_Make(char *out, u16 out_sz, const char *suffix)
{
    /* out = "studyroom/<DEV_ID>/<suffix>" */
    if(!out || out_sz == 0) return;             // 参数检查
    snprintf(out, out_sz, "server/cmd"); 
    //snprintf(out, out_sz, "server/%s/%s", suffix, DEV_ID);
}

/* ================== key=value&... 取值 ================== */
static u8 KV_Get(const char *kv, const char *key, char *out, u16 out_sz)
{
    const char *p;    // 查找指针
    u16 klen;         // key长度

    if(!kv || !key || !out || out_sz == 0) return 0;  // 参数检查

    klen = (u16)strlen(key);                   // 获取key长度
    p = kv;                                     // 从头开始查找
    while((p = strstr(p, key)) != 0)            // 循环查找key
    {
        if(p == kv || *(p-1) == '&')            // 检查是否是独立的key（开头或前面有&）
        {
            if(p[klen] == '=')                  // 检查后面是否是等号
            {
                const char *v = p + klen + 1;   // 值开始位置
                const char *e = strchr(v, '&'); // 查找下一个&或结束
                u16 n = (u16)((e ? e : (kv + strlen(kv))) - v); // 计算值长度
                if(n >= out_sz) n = out_sz - 1; // 防止溢出
                memcpy(out, v, n);              // 复制值
                out[n] = '\0';                  // 添加字符串结束符
                return 1;                       // 成功返回1
            }
        }
        p += klen;                              // 继续查找
    }
    return 0;                                   // 未找到返回0
}

/* ================== 设备状态机（匹配 server.py）==================
   FREE -> RESERVED(收到 reserve) -> IN_USE(刷卡checkin) -> FREE(刷卡checkout / release)
*/
static char g_state[12] = "FREE";               // 设备状态："FREE"/"RESERVED"/"IN_USE"
static char g_expect_uid[24] = "";              // 预约时服务器下发的 uid（卡号HEX）

/* ================== UI + 传感器 ================== */
static HGQ_UI_Data ui = {0};                    // UI数据结构初始化
static HGQ_VL53L0X_Handle g_tof;                // TOF传感器句柄
static float    g_temp_c   = 0.0f;              // 温度值（摄氏度）
static float    g_humi_rh  = 0.0f;              // 湿度值（百分比）
static uint16_t g_lux      = 0;                 // 光照强度（lux）
static uint16_t g_tof_mm   = 0;                 // TOF距离值（毫米）
static uint8_t  g_bh1750_ok = 0;                // BH1750初始化状态（0失败，1成功）

/* RC522 */
static uint8_t  g_rfid_uid[10];                 // RFID卡UID缓冲区
static uint8_t  g_rfid_uid_len = 0;             // UID实际长度
static uint8_t  g_rfid_has_card = 0;            // 是否有卡标志（0无，1有）
static char     g_card_hex[24];                 // 卡号十六进制字符串

/* MQTT 在线状态 */
static u8 g_mqtt_ok = 0;                        // MQTT连接状态（0未连接，1已连接）

/* ================== UID -> HEX（无空格） ================== */
static void UID_ToHexNoSpace(const uint8_t *uid, uint8_t uid_len, char *out, uint16_t out_sz)
{
    uint16_t pos = 0;                           // 输出位置
    if(!out || out_sz == 0) return;             // 参数检查
    out[0] = '\0';                              // 初始化输出为空
    if(!uid || uid_len == 0) return;            // 参数检查

    for(uint8_t i=0; i<uid_len; i++)            // 遍历每个字节
    {
        if(pos + 3 >= out_sz) break;            // 防止缓冲区溢出（2字符+结束符）
        sprintf(&out[pos], "%02X", uid[i]);     // 转换为十六进制
        pos += 2;                               // 移动位置指针
    }
    out[pos] = '\0';                            // 添加字符串结束符
}

/* ================== MQTT 发布：telemetry物理状态 ================== */
static void MQTT_PubTelemetry(void)
{
    char topic[64];                             // 主题缓冲区
    char msg[196];                              // 消息缓冲区
    int temp_int = ui.temp_x10 / 10;            // 温度整数部分
    int temp_frac = ui.temp_x10 % 10;           // 温度小数部分

    Topic_Make(topic, sizeof(topic), "telemetry"); // 构建主题

    snprintf(msg, sizeof(msg), "type=telemetry&temp=%d.%d&humi=%d&lux=%d", // 格式化消息
             temp_int, temp_frac, ui.humi, ui.lux);        // 温度、湿度、光照

    HGQ_ESP8266_MQTTPUB(topic, msg, 0);         // 发布消息
}

/* ================== MQTT 发布：heartbeat 心跳================== */
static void MQTT_PubHeartbeat(void)
{
    char topic[64];                             // 主题缓冲区
    Topic_Make(topic, sizeof(topic), "heartbeat"); // 构建主题
    HGQ_ESP8266_MQTTPUB(topic, "ping=1", 0);    // 发布心跳消息
}

/* ================== MQTT 发布：state ================== */
static void MQTT_PubState(void)
{
    char topic[64];                             // 主题缓冲区
    char msg[196];                              // 消息缓冲区

    Topic_Make(topic, sizeof(topic), "state");  // 构建主题

     snprintf(msg, sizeof(msg),
              "type=telemetry&seat=%s&state=%s&uid=%s&power=%d&light=%d&light_mode=%s",
              SEAT_NAME,
              g_state,
              g_expect_uid,
              1,
              (ui.light_on ? 1 : 0),
              (ui.auto_mode ? "AUTO" : "MANUAL"));

    HGQ_ESP8266_MQTTPUB(topic, msg, 0);         // 发布消息
}

/* ================== MQTT 发布：event ================== */
static void MQTT_PubEvent(const char *msg_kv)
{
    char topic[64];                             // 主题缓冲区
    Topic_Make(topic, sizeof(topic), "event");  // 构建主题
    HGQ_ESP8266_MQTTPUB(topic, (char*)msg_kv, 0); // 发布事件消息
}

/* ================== 服务器 cmd 处理 ================== */
static void CMD_HandleKV(const char *kv)
{
    char typ[20];                               // 命令类型缓冲区
    char uid[24];                               // UID缓冲区
    // char mode[12];                              // 模式缓冲区
    // char on[8];                                 // 开关状态缓冲区

    if(!KV_Get(kv, "type", typ, sizeof(typ)))   // 提取type字段
        return;                                 // 失败则返回

    /* 预约：进入 RESERVED，记录期望 uid（建议为卡号HEX） */
    if(strcmp(typ, "reserve") == 0)             // 预约命令
    {
        if(KV_Get(kv, "uid", uid, sizeof(uid))) // 提取uid
            strncpy(g_expect_uid, uid, sizeof(g_expect_uid)-1); // 复制到期望uid
        else
            g_expect_uid[0] = '\0';             // 清空期望uid

        strncpy(g_state, "RESERVED", sizeof(g_state)-1); // 设置状态为RESERVED

        /* UI提示 */
        strncpy(ui.status, "Reserved", sizeof(ui.status)-1); // 更新UI状态

        //MQTT_PubState();                        // 上报状态
        return;
    }

    /* 释放：回到 FREE */
    if(strcmp(typ, "release") == 0)             // 释放命令
    {
        g_expect_uid[0] = '\0';                 // 清空期望uid
        strncpy(g_state, "FREE", sizeof(g_state)-1); // 设置状态为FREE
        strncpy(ui.status, "Free", sizeof(ui.status)-1); // 更新UI状态
       // MQTT_PubState();                        // 上报状态
        return;
    }
}

/* ================== ESP UART2 接收解析 +MQTTSUBRECV ================== */
static void ESP_UART2_PollCmd(void)
{
    static char line[256];                      // 接收缓冲区
    static u16  idx = 0;                        // 缓冲区索引

    while(HGQ_USART2_Available())               // 检查串口是否有数据
    {
        uint8_t  ch ;
        HGQ_USART2_IT_GetChar(&ch);

        if(idx < sizeof(line)-1)                // 检查缓冲区是否未满
            line[idx++] = ch;                   // 存储字符并递增索引

        if(ch == '\n')                          // 检测到换行符（一行结束）
        {
            line[idx] = '\0';                   // 添加字符串结束符
            idx = 0;                            // 重置索引

            if(strstr(line, "+MQTTSUBRECV") != 0) // 检查是否包含MQTT订阅消息
            {
                char *last = strrchr(line, '\"'); // 查找最后一个双引号
                if(last)
                {
                    *last = '\0';               // 截断字符串
                    char *prev = strrchr(line, '\"'); // 查找前一个双引号
                    if(prev)
                    {
                        char *payload = prev + 1; // 提取负载（双引号之间的内容）
                        CMD_HandleKV(payload);  // 处理命令
                    }
                }
            }
        }
    }
}

/* ================== 触摸处理（UI里的按钮/亮度条） ================== */
static void APP_TouchProcess(void)
{
    static u8 last_down = 0;                    // 上次触摸状态（0未按下，1按下）

    // 【优化】TP_Scan 内部如果没按下会直接返回，非常快，不会阻塞
    if(tp_dev.scan(0))                          // 扫描触摸屏
    {
        u16 x = tp_dev.x[0];                    // 触摸点X坐标
        u16 y = tp_dev.y[0];                    // 触摸点Y坐标
        u8  p = 0;                              // 亮度百分比

        if(last_down == 0)                      // 如果上次未按下（新按下）
        {
            if(HGQ_UI_TouchBtn_Manual(x, y)) ui.auto_mode = 0; // 手动模式按钮
            else if(HGQ_UI_TouchBtn_Auto(x, y)) ui.auto_mode = 1; // 自动模式按钮
            else if(HGQ_UI_TouchBtn_On(x, y)) ui.light_on = 1; // 开灯按钮
            else if(HGQ_UI_TouchBtn_Off(x, y)) ui.light_on = 0; // 关灯按钮

            /* 手动改动也上报状态 */
           // if(g_mqtt_ok) MQTT_PubState();      // 如果MQTT连接正常，上报状态
        }

        if(ui.auto_mode == 0 && ui.light_on == 1) // 手动模式且灯亮
        {
            if(HGQ_UI_TouchLightBar(x, y, &p))  // 触摸亮度条
            {
                ui.bri_target = p;              // 设置目标亮度
                //if(g_mqtt_ok) MQTT_PubState();  // 上报状态
            }
        }

        last_down = 1;                          // 标记为按下状态
    }
    else
    {
        last_down = 0;                          // 标记为未按下状态
    }
}

static void esp_rx_cb(uint8_t ch)
{
    // 注意：这里在中断里执行，别写延时、别写LCD刷新、别发MQTT这种耗时操作
     ESP_UART2_PollCmd();
}


/* ================== 初始化 ================== */
static void System_Init(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); // 设置中断优先级分组

    delay_init(168);                            // 延时初始化
    uart_init(115200);                          // 串口1初始化
    HGQ_USART2_Init(115200);                    // 串口2初始化
//    HGQ_USART2_SetRxCallback(esp_rx_cb);
//    HGQ_USART2_EnableRxIRQ(ENABLE);   
    
    LED_Init();                                 // LED初始化

    LCD_Init();                                 // LCD初始化
    LCD_Display_Dir(1);                         // 设置显示方向
    LCD_Clear(BLACK);                           // 清屏为黑色

    tp_dev.init();                              // 触摸屏初始化

    W25QXX_Init();                              // SPI Flash初始化
    font_init();                                // 字体初始化
}

static void UI_Init(void)
{
    HGQ_UI_Init();                              // UI初始化
    HGQ_UI_DrawFramework();                     // 绘制UI框架

    strncpy(ui.area_seat, DEV_ID, sizeof(ui.area_seat)-1); // 复制区域座位号
    strncpy(ui.status, "Free", sizeof(ui.status)-1); // 初始化状态为Free
    strncpy(ui.next_time, "--:--", sizeof(ui.next_time)-1); // 初始化下一时间为--

    ui.auto_mode  = 0;                          // 默认为手动模式
    ui.light_on   = 1;                          // 默认为开灯
    ui.bri_target = 70;                         // 默认亮度70%
    ui.esp_state  = 1;                          // ESP状态：1连接中
}

static void Sensors_Init(void)
{
    HGQ_AHT20_Init();                           // AHT20温湿度传感器初始化
    g_bh1750_ok = (HGQ_BH1750_Init(0x23) == 0) ? 1 : 0; // BH1750光照传感器初始化
    HGQ_RC522_Init();                           // RC522 RFID初始化

    HGQ_VL53L0X_I2C_Init();                     // VL53L0X I2C初始化
    HGQ_VL53L0X_Begin(&g_tof, 0x29);            // VL53L0X传感器开始
    HGQ_VL53L0X_SetCalib(&g_tof, 1.0172855f, -61.9165f); // 设置校准参数
}

/* 不用 HGQ_ESP8266_Init()（它内部会重复连接/订阅），这里按 server.py 需求手动配置 */
static void ESP_Setup(void)
{
    char cmd_topic[64];                         // 命令主题缓冲区

    ui.esp_state = 1;                           // ESP状态设为连接中
    g_mqtt_ok = 0;                              // MQTT连接状态设为未连接

    HGQ_USART2_SendString("AT+RST\r\n");        // 发送重启命令
    delay_ms(3000);                             // 等待重启完成（3秒）

    if(HGQ_ESP8266_SendCmd("AT\r\n", "OK", 800) != ESP8266_OK) // 测试AT指令
    { 
        ui.esp_state = 0;                       // 设置ESP状态为失败
        return; 
    }
    HGQ_ESP8266_SendCmd("ATE0\r\n", "OK", 800); // 关闭回显
    HGQ_ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", 800); // 设置为STA模式

    if(HGQ_ESP8266_JoinAP((char*)WIFI_SSID, (char*)WIFI_PASS) != ESP8266_OK) // 连接WiFi
    { 
        ui.esp_state = 0;                       // 设置ESP状态为失败
        return; 
    }
    if(HGQ_ESP8266_ConnectMQTT((char*)MQTT_BROKER, MQTT_PORT, (char*)MQTT_USER, (char*)MQTT_PASS) != ESP8266_OK) // 连接MQTT
    { 
        ui.esp_state = 0;                       // 设置ESP状态为失败
        return; 
    }

    /* 订阅 studyroom/<dev_id>/cmd */
    //Topic_Make(cmd_topic, sizeof(cmd_topic), "cmd"); // 构建命令主题
    if(HGQ_ESP8266_MQTTSUB("stm32/cmd", 0) != ESP8266_OK) // 订阅命令主题
    { 
        ui.esp_state = 0;                       // 设置ESP状态为失败
        return; 
    }

    ui.esp_state = 2;                           // ESP状态设为已连接
    g_mqtt_ok = 1;                              // MQTT连接状态设为已连接

    /* 上线先上报一次 state + heartbeat */
    MQTT_PubState();                            // 上报状态
    MQTT_PubHeartbeat();                        // 上报心跳
}

/* ================== 任务：传感器 ================== */
static void Task_AHT20(void)
{
    float tc = 0.0f, rh = 0.0f;                 // 临时变量
    HGQ_AHT20_Read(&tc, &rh);                   // 读取温湿度
    g_temp_c = tc;                              // 存储温度
    g_humi_rh = rh;                             // 存储湿度

    ui.temp_x10 = (int)(g_temp_c * 10.0f + 0.5f); // 转换为整数（乘以10）
    ui.humi = (int)(g_humi_rh + 0.5f);          // 四舍五入取整
}

static void Task_BH1750(void)
{
    if(!g_bh1750_ok) return;                    // 如果BH1750未初始化则返回
    HGQ_BH1750_ReadLux(&g_lux);                 // 读取光照强度
    ui.lux = g_lux;                             // 存储到UI
}

static void Task_TOF(void)
{
    uint16_t mm = 0;                            // 临时变量
    if(HGQ_VL53L0X_ReadMm(&g_tof, &mm) == HGQ_VL53L0X_OK) // 读取距离
        g_tof_mm = mm;                          // 存储距离值
}

/* ================== 任务：刷卡 ================== */
static void Task_RC522(void)
{
    uint8_t uid_len = 0;                        // UID长度
    uint8_t ret = HGQ_RC522_PollUID(g_rfid_uid, &uid_len); // 轮询RFID卡

    if(ret == 0)                                // 检测到卡
    {
        if(!g_rfid_has_card)                    // 如果是新卡
        {
            g_rfid_has_card = 1;                // 标记有卡
            g_rfid_uid_len = uid_len;           // 存储UID长度
            UID_ToHexNoSpace(g_rfid_uid, g_rfid_uid_len, g_card_hex, sizeof(g_card_hex)); // 转换UID为十六进制

            if(!g_mqtt_ok) return;              // MQTT未连接则返回

            /* RESERVED：必须刷对卡（与 g_expect_uid 一致） */
            if(strcmp(g_state, "RESERVED") == 0) // 如果是预约状态
            {
                if(g_expect_uid[0] == '\0' || strcmp(g_expect_uid, g_card_hex) == 0) // 检查卡号是否匹配
                {
                    MQTT_PubEvent("type=checkin&uid="); // 发送签到事件前缀
                    /* 上面先发固定前缀，再补 uid（避免 sprintf 重入问题） */
                    {
                        char ev[64];
                        snprintf(ev, sizeof(ev), "type=checkin&uid=%s", g_card_hex); // 格式化完整事件
                        MQTT_PubEvent(ev);      // 发送签到事件
                    }

                    strncpy(g_state, "IN_USE", sizeof(g_state)-1); // 状态改为使用中
                    strncpy(ui.status, "InUse", sizeof(ui.status)-1); // UI状态更新
                    strncpy(g_expect_uid, g_card_hex, sizeof(g_expect_uid)-1); // 用实际卡号锁定
                    MQTT_PubState();            // 上报状态
                }
                else                            // 卡号不匹配
                {
                    /* 刷错卡：记录事件，状态不变 */
                    char ev[96];
                    snprintf(ev, sizeof(ev), "type=card&uid=%s&result=deny", g_card_hex); // 格式化拒绝事件
                   // MQTT_PubEvent(ev);          // 发送拒绝事件
                }
            }
            else if(strcmp(g_state, "IN_USE") == 0) // 如果是使用中状态
            {
                /* IN_USE：同一张卡签退 */
                if(g_expect_uid[0] == '\0' || strcmp(g_expect_uid, g_card_hex) == 0) // 检查卡号是否匹配
                {
                    char ev[96];
                    snprintf(ev, sizeof(ev), "type=checkout&uid=%s&reason=card", g_card_hex); // 格式化签退事件
                    MQTT_PubEvent(ev);          // 发送签退事件

                    strncpy(g_state, "FREE", sizeof(g_state)-1); // 状态改为空闲
                    g_expect_uid[0] = '\0';     // 清空期望UID
                    strncpy(ui.status, "Free", sizeof(ui.status)-1); // UI状态更新
                    MQTT_PubState();            // 上报状态
                }
                else                            // 卡号不匹配
                {
                    char ev[96];
                    snprintf(ev, sizeof(ev), "type=card&uid=%s&result=deny", g_card_hex); // 格式化拒绝事件
                   // MQTT_PubEvent(ev);          // 发送拒绝事件
                }
            }
            else                                // 空闲状态
            {
                /* FREE：可选记录刷卡 */
                char ev[64];
                snprintf(ev, sizeof(ev), "type=card&uid=%s", g_card_hex); // 格式化刷卡事件
                //MQTT_PubEvent(ev);              // 发送刷卡事件
            }
        }
    }
    else if(ret == 2)                           // 卡离开
    {
        g_rfid_has_card = 0;                    // 标记无卡
        g_rfid_uid_len = 0;                     // 清空UID长度
        g_card_hex[0] = '\0';                   // 清空卡号
    }
}

/* ================== TOF 占位检测事件（管理员用）================== */
static void Task_TOF_Event(void)
{
    if(!g_mqtt_ok) return;                      // MQTT未连接则返回

    /* 只在 FREE 状态下做"占位"判断：座位空闲但检测到物品 */
    if(strcmp(g_state, "FREE") == 0)            // 如果是空闲状态
    {
        int object = (g_tof_mm > 0 && g_tof_mm < TOF_OCCUPIED_MM) ? 1 : 0; // 判断是否有物品
        char ev[96];
        snprintf(ev, sizeof(ev), "type=tof&tof_mm=%d&object=%d", (int)g_tof_mm, object); // 格式化TOF事件
        MQTT_PubEvent(ev);                      // 发送TOF事件
    }
}

/* ================== main ================== */
int main(void)
{
    System_Init();                              // 系统初始化
    UI_Init();                                  // UI初始化
    Sensors_Init();                             // 传感器初始化
    ESP_Setup();                                // ESP8266初始化

    uint32_t tick_ms = 0;                       // 系统时间戳（毫秒）
    // 各个任务的上次执行时间
    uint32_t last_rfid_ms = 0, last_tof_ms = 0, last_aht_ms = 0, last_lux_ms = 0;
    // uint32_t last_touch_ms = 0; // 【优化】移除触摸计时
    uint32_t last_ui_ms = 0;

    // 各个发布任务的上次执行时间
    uint32_t last_pub_tele = 0;
    // uint32_t last_pub_state = 0;
    // uint32_t last_pub_heart = 0;
    // uint32_t last_pub_tofevt = 0;

    while(1)                                    // 主循环
    {
        /* 【优化关键点1】大幅减小延时，提高轮询频率 */
        delay_ms(1);                            // 延时1ms (原10ms)
        tick_ms += 1;                           // 更新时间戳

        /* 【优化关键点2】每次循环都检测触摸（全速运行） */
        APP_TouchProcess();                     

        /* 传感器 */
        if(tick_ms - last_rfid_ms >= PERIOD_RFID_MS) { last_rfid_ms = tick_ms; Task_RC522(); } // RFID任务
        if(tick_ms - last_tof_ms  >= PERIOD_TOF_MS)  { last_tof_ms  = tick_ms; Task_TOF();  } // TOF任务
        if(tick_ms - last_aht_ms  >= PERIOD_AHT_MS)  { last_aht_ms  = tick_ms; Task_AHT20(); } // 温湿度任务
        if(tick_ms - last_lux_ms  >= PERIOD_LUX_MS)  { last_lux_ms  = tick_ms; Task_BH1750(); } // 光照任务

        /* UI */
        if(tick_ms - last_ui_ms >= UI_REFRESH_MS) // UI刷新任务
        {
            last_ui_ms = tick_ms;               // 更新UI刷新时间

            ui.use_min = 120;                   // 设置使用时间（分钟）
            /* 现在的Update函数只更新变化的数值，不再重画所有文字，非常快 */
            HGQ_UI_Update(&ui, "", "");         

            /* PWM 输出 */
            {
                int bri_now = HGQ_UI_GetBrightnessNow(); // 获取当前亮度
                LED0_SetOn(ui.light_on);        // 设置LED开关
                LED0_SetBrightness((u8)bri_now); // 设置LED亮度
            }
        }

        if(!g_mqtt_ok) continue;                // MQTT未连接则跳过发布任务

        /* telemetry */
        if(tick_ms - last_pub_tele >= PUB_TELE_MS) // 遥测发布任务
        {
            last_pub_tele = tick_ms;            // 更新遥测发布时间
            MQTT_PubTelemetry();                // 发布遥测数据
        }
    }
}
