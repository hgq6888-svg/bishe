#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "delay.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lcd.h"
#include "touch.h"
#include "w25qxx.h"
#include "fontupd.h"
#include "text.h"
#include "led.h"
#include "hgq_ui.h"
#include "hgq_vl53l0x.h"
#include "hgq_aht20.h"
#include "hgq_bh1750.h"
#include "hgq_rc522.h"
#include "hgq_esp8266.h"
#include "hgq_usart.h"

/* ================== 配置区域 ================== */
#define WIFI_SSID       "hhh"
#define WIFI_PASS       "13680544157"
#define MQTT_BROKER     "1.14.163.35"
#define MQTT_PORT       1883
#define MQTT_USER       "test01"
#define MQTT_PASS       ""

#define DEV_ID          "A18" 
#define SEAT_NAME_GBK   "\x41\xC7\xF8\x2D\x31\x38\xBA\xC5" // A区-18号

#define TOF_OCCUPIED_MM 380  

/* FreeRTOS 任务优先级与堆栈配置 */
#define START_TASK_PRIO     1
#define START_STK_SIZE      512

#define NET_TASK_PRIO       3
#define NET_STK_SIZE        1024  

#define UI_TASK_PRIO        4     
#define UI_STK_SIZE         1024

#define SENSOR_TASK_PRIO    2
#define SENSOR_STK_SIZE     512

#define RFID_TASK_PRIO      3
#define RFID_STK_SIZE       512

#define TASK_QUEUE_SIZE     10
#define TASK_CMD_LEN        256

/* 操作模式定义 */
typedef enum {
    OP_NORMAL = 0,
    OP_WAIT_CHECKIN,  
    OP_WAIT_CHECKOUT,
    OP_WARNING 
} OpMode_t;

/* 任务队列结构体 */
typedef struct {
    char cmds[TASK_QUEUE_SIZE][TASK_CMD_LEN];
    volatile uint16_t head;
    volatile uint16_t tail;
} TaskQueue_t;

/* ================== 全局变量 ================== */
/* 互斥量 */
SemaphoreHandle_t xMutexUI;   
SemaphoreHandle_t xMutexESP;  

/* 任务句柄 */
TaskHandle_t StartTask_Handler;
TaskHandle_t NetTask_Handler;
TaskHandle_t UITask_Handler;
TaskHandle_t SensorTask_Handler;
TaskHandle_t RFIDTask_Handler;

/* 业务全局变量 */
static OpMode_t g_op_mode = OP_NORMAL;
static uint32_t g_popup_ts = 0; 
static TaskQueue_t g_task_queue = {0};

static char g_state[12] = "FREE";
static char g_expect_uid[24] = "";
static HGQ_UI_Data ui = {0};
static HGQ_VL53L0X_Handle g_tof;
static uint16_t g_lux = 0, g_tof_mm = 0;
static uint8_t  g_bh1750_ok = 0;
static uint8_t  g_rfid_uid[10], g_rfid_has_card = 0;
static char     g_card_hex[24];
static u8       g_mqtt_ok = 0;
static uint8_t  g_need_ui_refresh = 0; 
static uint8_t  g_force_redraw = 0; 

static uint8_t  g_time_h = 12, g_time_m = 0, g_time_s = 0; 
static char     g_time_str[10] = "--:--"; 

/* 弹窗文本GBK */
const u8 STR_POP_IN[]   = {0xC7,0xEB,0xCB,0xA2,0xBF,0xA8,0xC7,0xA9,0xB5,0xBD,0x00}; // 请刷卡签到
const u8 STR_POP_OUT[]  = {0xC7,0xEB,0xCB,0xA2,0xBF,0xA8,0xC7,0xA9,0xCD,0xCB,0x00}; // 请刷卡签退
const u8 STR_POP_ERR[]  = {0xBF,0xA8,0xBA,0xC5,0xB4,0xED,0xCE,0xF3,0x00}; // 卡号错误
const u8 STR_POP_WARN[] = {0xC7,0xEB,0xCF,0xC8,0xB5,0xE3,0xBB,0xF7,0xC6,0xC1,0xC4,0xBB,0x00}; // 请先点击屏幕

/* ================== 辅助函数实现 ================== */

static int TaskQueue_Push(const char *cmd_str) {
    uint16_t next = (g_task_queue.head + 1) % TASK_QUEUE_SIZE;
    if (next == g_task_queue.tail) return 0;
    strncpy(g_task_queue.cmds[g_task_queue.head], cmd_str, TASK_CMD_LEN - 1);
    g_task_queue.cmds[g_task_queue.head][TASK_CMD_LEN - 1] = '\0';
    g_task_queue.head = next;
    return 1;
}

static int TaskQueue_Pop(char *out_buf) {
    if (g_task_queue.head == g_task_queue.tail) return 0;
    strncpy(out_buf, g_task_queue.cmds[g_task_queue.tail], TASK_CMD_LEN);
    g_task_queue.tail = (g_task_queue.tail + 1) % TASK_QUEUE_SIZE;
    return 1;
}

static void Topic_Make(char *out, u16 out_sz, const char *suffix) {
    snprintf(out, out_sz, "server/%s/%s", suffix, DEV_ID);
}

static void UID_ToHexNoSpace(const uint8_t *uid, uint8_t uid_len, char *out, uint16_t out_sz) {
    uint16_t pos = 0; out[0] = '\0';
    for(uint8_t i=0; i<uid_len && pos+3<out_sz; i++) {
        sprintf(&out[pos], "%02X", uid[i]); pos += 2;
    }
}

static u8 KV_Get(const char *kv, const char *key, char *out, u16 out_sz) {
    const char *p = kv; 
    u16 klen = strlen(key);
    while((p = strstr(p, key)) != 0) {
        char prev = (p == kv) ? 0 : *(p-1);
        if(p == kv || prev == '&' || prev == '\"' || prev == ',' || prev == ' ') {
            if(p[klen] == '=') {
                const char *v = p + klen + 1;
                u16 n = 0;
                while(v[n] != 0 && v[n] != '&' && v[n] != '\"' && v[n] != '\r' && v[n] != '\n') {
                    n++;
                }
                if(n >= out_sz) n = out_sz - 1;
                memcpy(out, v, n); out[n] = 0; 
                return 1;
            }
        }
        p += klen;
    }
    return 0;
}

static int Calc_Auto_Brightness(int lux) {
    int bri;
    if(lux < 0) lux = 0;
    if(lux > 800) lux = 800;
    bri = 100 - (lux * 90 / 800);
    if(bri < 10) bri = 10;
    if(bri > 100) bri = 100;
    return bri;
}

static void MQTT_PubTelemetry(void) {
    char topic[64], msg[196];
    Topic_Make(topic, sizeof(topic), "telemetry");
    snprintf(msg, sizeof(msg), "type=telemetry&seat_id=%s&temp=%d.%d&humi=%d&lux=%d&tof_mm=%d", 
             DEV_ID, ui.temp_x10/10, ui.temp_x10%10, ui.humi, ui.lux, g_tof_mm);
    HGQ_ESP8266_MQTTPUB_Fast(topic, msg, 0); 
}

static void MQTT_PubState(void) {
    char topic[64], msg[196];
    Topic_Make(topic, sizeof(topic), "state");
    snprintf(msg, sizeof(msg), "type=state&seat_id=%s&state=%s&uid=%s&power=1&light=%d&light_mode=%s",
             DEV_ID, g_state, g_expect_uid, ui.light_on, ui.auto_mode?"AUTO":"MANUAL");
    HGQ_ESP8266_MQTTPUB_Fast(topic, msg, 0);
}

static void MQTT_PubSync(void) {
    char topic[64], msg[64];
    Topic_Make(topic, sizeof(topic), "state"); 
    snprintf(msg, sizeof(msg), "type=sync&seat_id=%s", DEV_ID);
    HGQ_ESP8266_MQTTPUB_Fast(topic, msg, 0);
}

static void MQTT_PubEvent(const char *msg_kv) {
    char topic[64];
    Topic_Make(topic, sizeof(topic), "event");
    HGQ_ESP8266_MQTTPUB_Fast(topic, (char*)msg_kv, 0);
}

/* ================== 开机动画 ================== */
static void Boot_Animation(void)
{
    LCD_Clear(WHITE);
    POINT_COLOR = BLUE;      
    BACK_COLOR  = WHITE;

    Show_Str(90, 80, 200, 24, (u8*)"智慧自习室", 24, 0);
    
    POINT_COLOR = BLACK;
    Show_Str(85, 110, 200, 16, (u8*)"Smart Study Room", 16, 0);

    LCD_DrawRectangle(60, 160, 240, 176);

    POINT_COLOR = 0x07E0; 
    for(int i=0; i<=100; i++)
    {
        u16 len = 178 * i / 100;
        if(len > 0) {
            LCD_Fill(61, 161, 61 + len, 175, 0x07E0);
        }
        if(i < 60) delay_ms(10);
        else delay_ms(20);
    }
    
    POINT_COLOR = BLACK;
    Show_Str(100, 190, 200, 16, (u8*)"正在启动系统...", 16, 0);
    delay_ms(800); 

    LCD_Clear(WHITE);
    POINT_COLOR = BLACK; 
}

/* ================== 任务函数声明 ================== */
void start_task(void *pvParameters);
void net_task(void *pvParameters);
void ui_task(void *pvParameters);
void sensor_task(void *pvParameters);
void rfid_task(void *pvParameters);

/* ================== 主函数 ================== */
int main(void) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4); 
    delay_init(168); 
    uart_init(115200);      
    HGQ_USART2_Init(115200); 
    
    printf("\r\n========================================\r\n");
    printf("[SYSTEM] 正在启动硬件自检程序...\r\n");

    LED_Init(); printf("[自检] LED指示灯初始化......OK\r\n");
    LCD_Init(); printf("[自检] LCD屏幕底层初始化....OK\r\n");
    LCD_Display_Dir(1); 
    tp_dev.init(); printf("[自检] 电容触摸屏初始化.....OK\r\n");
    W25QXX_Init(); printf("[自检] W25Q128 Flash初始化..OK\r\n");
    font_init(); printf("[自检] 中文字库系统初始化...OK\r\n");
    
    printf("[SYSTEM] 播放开机动画...\r\n");
    Boot_Animation();
    
    printf("[SYSTEM] 正在启动 FreeRTOS 实时操作系统...\r\n");
    printf("========================================\r\n\r\n");
    
    HGQ_UI_Init(); 
    printf("[自检] UI图形界面初始化.....OK\r\n");

    strncpy(ui.area_seat, SEAT_NAME_GBK, sizeof(ui.area_seat)-1);
    strcpy(ui.status, "Free"); strcpy(ui.user_str, "--");
    strcpy(ui.reserve_t, "--"); strcpy(ui.start_t, "--"); strcpy(ui.remain_t, "--");
    
    HGQ_UI_DrawFramework(); 
    printf("[自检] 初始界面绘制.........OK\r\n");
    
    HGQ_AHT20_Init(); 
    printf("[自检] AHT20温湿度传感器....OK\r\n");

    g_bh1750_ok = !HGQ_BH1750_Init(0x23);
    if(g_bh1750_ok) printf("[自检] BH1750光照传感器.....OK\r\n");
    else            printf("[自检] BH1750光照传感器.....异常!\r\n");

    HGQ_RC522_Init(); 
    printf("[自检] RC522射频模块........OK\r\n");

    HGQ_VL53L0X_I2C_Init(); 
    HGQ_VL53L0X_Begin(&g_tof, 0x29);
    printf("[自检] VL53L0X激光测距......OK\r\n");
    
    xMutexUI = xSemaphoreCreateMutex();
    xMutexESP = xSemaphoreCreateMutex();

    xTaskCreate((TaskFunction_t )start_task, (const char* )"start_task", (uint16_t )START_STK_SIZE, (void* )NULL, (UBaseType_t )START_TASK_PRIO, (TaskHandle_t* )&StartTask_Handler);
    
    vTaskStartScheduler();
    while(1) {}; 
}

/* ================== 任务实现 ================== */

void start_task(void *pvParameters) {
    taskENTER_CRITICAL(); 
    xTaskCreate(net_task, "Net", NET_STK_SIZE, NULL, NET_TASK_PRIO, &NetTask_Handler);
    xTaskCreate(ui_task, "UI", UI_STK_SIZE, NULL, UI_TASK_PRIO, &UITask_Handler);
    xTaskCreate(sensor_task, "Sens", SENSOR_STK_SIZE, NULL, SENSOR_TASK_PRIO, &SensorTask_Handler);
    xTaskCreate(rfid_task, "RFID", RFID_STK_SIZE, NULL, RFID_TASK_PRIO, &RFIDTask_Handler);
    vTaskDelete(StartTask_Handler); 
    taskEXIT_CRITICAL(); 
}

static void Network_Connect_Flow(void) {
    printf("[网络] 开始执行联网流程...\r\n");
    xSemaphoreTake(xMutexUI, portMAX_DELAY);
    ui.esp_state = 1; g_mqtt_ok = 0;
    HGQ_UI_Update(&ui, g_time_str);
    xSemaphoreGive(xMutexUI);
    
    xSemaphoreTake(xMutexESP, portMAX_DELAY);
    printf("[网络] 复位 ESP8266...\r\n");
    HGQ_USART2_SendString("AT+RST\r\n"); 
    vTaskDelay(3000); 
    
    HGQ_ESP8266_SendCmd("ATE0\r\n","OK",500);
    HGQ_ESP8266_SendCmd("AT+CWMODE=1\r\n","OK",500);
    HGQ_ESP8266_SendCmd("AT+CWQAP\r\n", "OK", 500);
    HGQ_ESP8266_SendCmd("AT+CIPMUX=0\r\n", "OK", 500);

    printf("[网络] 正在连接 WiFi...\r\n");
    if(HGQ_ESP8266_JoinAP(WIFI_SSID, WIFI_PASS) != ESP8266_OK) {
        printf("[网络] WiFi 连接失败!\r\n");
        xSemaphoreGive(xMutexESP);
        xSemaphoreTake(xMutexUI, portMAX_DELAY);
        ui.esp_state = 0; 
        xSemaphoreGive(xMutexUI);
        return;
    }
    printf("[网络] WiFi 连接成功.\r\n");

    printf("[网络] 正在连接 MQTT...\r\n");
    if(HGQ_ESP8266_ConnectMQTT(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS) != ESP8266_OK) {
        printf("[网络] MQTT 连接失败!\r\n");
        xSemaphoreGive(xMutexESP);
        xSemaphoreTake(xMutexUI, portMAX_DELAY);
        ui.esp_state = 0; 
        xSemaphoreGive(xMutexUI);
        return;
    }
    
    if(HGQ_ESP8266_MQTTSUB("stm32/cmd", 0) != ESP8266_OK) {
        printf("[网络] 订阅失败!\r\n");
        xSemaphoreGive(xMutexESP);
        xSemaphoreTake(xMutexUI, portMAX_DELAY);
        ui.esp_state = 0; 
        xSemaphoreGive(xMutexUI);
        return;
    }
    
    printf("[网络] 启动 NTP 网络校时...\r\n");
    HGQ_ESP8266_EnableNTP();

    printf("[网络] 发送状态同步请求 (SYNC)...\r\n");
    MQTT_PubSync(); 
    
    xSemaphoreGive(xMutexESP);
    
    xSemaphoreTake(xMutexUI, portMAX_DELAY);
    ui.esp_state = 2; g_mqtt_ok = 1;
    xSemaphoreGive(xMutexUI);
    printf("[网络] 联网完成.\r\n");
}

void net_task(void *pvParameters) {
    Network_Connect_Flow();
    
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t cnt_pub = 0;
    uint32_t cnt_net_chk = 50; 
    uint32_t cnt_sync = 0;

    static char line[512]; 
    static u16 idx = 0; 
    uint8_t ch;

    while(1) {
        while(HGQ_USART2_IT_GetChar(&ch)) {
            if(idx < sizeof(line)-1) line[idx++] = ch;
            if(ch == '\n') {
                line[idx] = 0; idx = 0;
                if(strstr(line, "+MQTTSUBRECV")) {
                    char *p_start = strstr(line, "cmd=");
                    if(p_start) TaskQueue_Push(p_start);
                    else {
                        char *last_comma = strrchr(line, ',');
                        if(last_comma && *(last_comma+1) != '\0') {
                            TaskQueue_Push(last_comma + 1);
                        }
                    }
                }
            }
        }

        char kv[TASK_CMD_LEN];
        while(TaskQueue_Pop(kv)) {
            printf("[指令] %s\r\n", kv);
            char cmd_val[20], uid_str[24], sid[20], user[32];
            
            if(!KV_Get(kv, "cmd", cmd_val, sizeof(cmd_val))) continue;
            
            xSemaphoreTake(xMutexUI, portMAX_DELAY);
            
            if(strcmp(cmd_val, "time_sync") == 0) {
                char t_buf[16];
                if(KV_Get(kv, "time", t_buf, sizeof(t_buf)) && strlen(t_buf) >= 8) {
                    t_buf[2] = 0; t_buf[5] = 0;
                    g_time_h = atoi(t_buf); g_time_m = atoi(t_buf+3); g_time_s = atoi(t_buf+6);
                    sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
                    g_need_ui_refresh = 1;
                    printf("[校时] 服务器时间同步成功: %02d:%02d\r\n", g_time_h, g_time_m);
                }
            }
            else if(KV_Get(kv, "seat_id", sid, sizeof(sid)) && strcmp(sid, DEV_ID) == 0) {
                if(strcmp(cmd_val, "deny") == 0) {
                    HGQ_UI_ShowPopup((char*)STR_POP_ERR);
                    g_popup_ts = 3; g_op_mode = OP_WAIT_CHECKIN; 
                }
                else if(strcmp(cmd_val, "reserve") == 0) {
                    if(KV_Get(kv, "user", user, sizeof(user))) strncpy(ui.user_str, user, sizeof(ui.user_str)-1);
                    if(KV_Get(kv, "uid", uid_str, sizeof(uid_str))) strncpy(g_expect_uid, uid_str, sizeof(g_expect_uid)-1);
                    
                    char t_buf[32];
                    if(KV_Get(kv, "expires_at", t_buf, sizeof(t_buf))) {
                        if(strlen(t_buf) >= 16) { strncpy(ui.reserve_t, t_buf+11, 5); ui.reserve_t[5]=0; }
                    }
                    
                    strncpy(g_state, "RESERVED", sizeof(g_state)-1);
                    strncpy(ui.status, "Rsrv(15m)", sizeof(ui.status)-1);
                    
                    xSemaphoreTake(xMutexESP, portMAX_DELAY);
                    MQTT_PubState();
                    xSemaphoreGive(xMutexESP);
                    
                    g_need_ui_refresh = 1; 
                    printf("[预约] 状态已同步：RESERVED\r\n");
                }
                else if(strcmp(cmd_val, "checkin_ok") == 0) {
                    sprintf(ui.start_t, "%02d:%02d", g_time_h, g_time_m);
                    strncpy(g_state, "IN_USE", sizeof(g_state)-1);
                    strncpy(ui.status, "In Use", sizeof(ui.status)-1);
                    ui.light_on = 1; 
                    
                    g_op_mode = OP_NORMAL; 
                    
                    xSemaphoreTake(xMutexESP, portMAX_DELAY);
                    MQTT_PubState();
                    xSemaphoreGive(xMutexESP);
                    g_need_ui_refresh = 1;
                    g_force_redraw = 1; 
                    printf("[签到] 状态已同步：IN_USE\r\n");
                }
                else if(strcmp(cmd_val, "release") == 0 || strcmp(cmd_val, "checkout_ok") == 0) {
                    g_expect_uid[0] = 0;
                    strncpy(g_state, "FREE", sizeof(g_state)-1);
                    strncpy(ui.status, "Free", sizeof(ui.status)-1);
                    strcpy(ui.user_str, "--"); strcpy(ui.reserve_t, "--");
                    strcpy(ui.start_t, "--"); ui.light_on = 0; 
                    
                    g_op_mode = OP_NORMAL;
                    
                    xSemaphoreTake(xMutexESP, portMAX_DELAY);
                    MQTT_PubState();
                    xSemaphoreGive(xMutexESP);
                    g_need_ui_refresh = 1;
                    g_force_redraw = 1;
                }
            }
            xSemaphoreGive(xMutexUI);
        }

        if(++cnt_pub >= 40) { // 2s
            cnt_pub = 0;
            if(g_mqtt_ok) {
                xSemaphoreTake(xMutexESP, portMAX_DELAY);
                MQTT_PubTelemetry(); 
                xSemaphoreGive(xMutexESP);
            }
        }

        if(++cnt_net_chk >= 200) { // 10s
            cnt_net_chk = 0;
            xSemaphoreTake(xMutexESP, portMAX_DELAY);
            uint8_t status = HGQ_ESP8266_CheckStatus();
            if(status == 0) { delay_ms(200); status = HGQ_ESP8266_CheckStatus(); }
            if(status == 0) g_mqtt_ok = 0;
            xSemaphoreGive(xMutexESP);
            
            if(!g_mqtt_ok) Network_Connect_Flow();
        }

        if(++cnt_sync >= 1200) { // 60s
            cnt_sync = 0;
            if(g_mqtt_ok) {
                xSemaphoreTake(xMutexESP, portMAX_DELAY);
                uint8_t h, m, s;
                if(HGQ_ESP8266_GetNTPTime(&h, &m, &s)) {
                    xSemaphoreTake(xMutexUI, portMAX_DELAY);
                    g_time_h = h; g_time_m = m; g_time_s = s;
                    sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
                    g_need_ui_refresh = 1;
                    xSemaphoreGive(xMutexUI);
                    printf("[校时] NTP 时间更新: %02d:%02d\r\n", h, m);
                }
                xSemaphoreGive(xMutexESP);
            }
        }

        vTaskDelayUntil(&xLastWakeTime, 50); 
    }
}

void ui_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    static uint8_t s_last_touch = 0;

    while(1) {
        static uint16_t time_cnt = 0;
        if(++time_cnt >= 10) { 
            time_cnt = 0;
            xSemaphoreTake(xMutexUI, portMAX_DELAY);
            if(++g_time_s >= 60) { g_time_s = 0; g_time_m++; if(g_time_m >= 60) { g_time_m = 0; g_time_h = (g_time_h+1)%24; } }
            if(g_time_str[0] != '-') sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
            
            if(g_op_mode != OP_NORMAL && g_popup_ts > 0) {
                g_popup_ts--;
                if(g_popup_ts == 0) { g_op_mode = OP_NORMAL; g_force_redraw = 1; }
            }
            xSemaphoreGive(xMutexUI);
        }

        u8 is_touched = tp_dev.scan(0);
        if(is_touched && !s_last_touch) {
            u16 x = tp_dev.x[0], y = tp_dev.y[0];
            printf("[UI] Touch: x=%d, y=%d\r\n", x, y);
            
            xSemaphoreTake(xMutexUI, portMAX_DELAY);
            if(g_op_mode != OP_NORMAL) {
                g_op_mode = OP_NORMAL; g_force_redraw = 1;
            } else {
                int changed = 0;
                if(HGQ_UI_TouchBtn_Check(x, y)) {
                    printf("[UI] Button Pressed. State: %s\r\n", g_state);
                    if(strcmp(g_state, "IN_USE") == 0) {
                        g_op_mode = OP_WAIT_CHECKOUT; HGQ_UI_ShowPopup((char*)STR_POP_OUT);
                    } else {
                        g_op_mode = OP_WAIT_CHECKIN; HGQ_UI_ShowPopup((char*)STR_POP_IN);
                    }
                    g_popup_ts = 15;
                } else {
                    if(strcmp(g_state, "IN_USE") == 0) {
                        if(HGQ_UI_TouchBtn_Mode(x, y)) { ui.auto_mode = !ui.auto_mode; changed=1; }
                        else if(HGQ_UI_TouchBtn_On(x, y)) { ui.light_on = 1; changed=1; }
                        else if(HGQ_UI_TouchBtn_Off(x, y)) { ui.light_on = 0; changed=1; }
                        if(ui.auto_mode==0 && ui.light_on) {
                            if(HGQ_UI_TouchBtn_BriUp(x, y)) { if(ui.bri_target<=90) ui.bri_target+=10; else ui.bri_target=100; }
                            else if(HGQ_UI_TouchBtn_BriDown(x, y)) { if(ui.bri_target>=10) ui.bri_target-=10; else ui.bri_target=0; }
                        }
                    }
                    if(changed) {
                        xSemaphoreTake(xMutexESP, portMAX_DELAY);
                        MQTT_PubState();
                        xSemaphoreGive(xMutexESP);
                    }
                }
            }
            xSemaphoreGive(xMutexUI);
        }
        s_last_touch = is_touched;

        xSemaphoreTake(xMutexUI, portMAX_DELAY);
        if(g_force_redraw) {
            g_force_redraw = 0;
            HGQ_UI_ResetCache(); 
            HGQ_UI_DrawFramework(); 
            g_need_ui_refresh = 1; 
        }
        if(g_need_ui_refresh || 1) { 
            if(g_op_mode == OP_NORMAL) HGQ_UI_Update(&ui, g_time_str);
            LED0_SetOn(ui.light_on);
            LED0_SetBrightness((u8)HGQ_UI_GetBrightnessNow());
            Relay_Set(strcmp(g_state, "IN_USE") == 0 ? 1 : 0);
            g_need_ui_refresh = 0;
        }
        xSemaphoreGive(xMutexUI);

        vTaskDelayUntil(&xLastWakeTime, 100); 
    }
}

void sensor_task(void *pvParameters) {
    while(1) {
        float tc, rh;
        HGQ_AHT20_Read(&tc, &rh);
        
        xSemaphoreTake(xMutexUI, portMAX_DELAY);
        ui.temp_x10 = (int)(tc * 10 + 0.5f);
        ui.humi = (int)(rh + 0.5f);
        if(g_bh1750_ok) { HGQ_BH1750_ReadLux(&g_lux); ui.lux = g_lux; }
        else { g_bh1750_ok = !HGQ_BH1750_Init(0x23); if(!g_bh1750_ok) ui.lux = -1; }
        
        if(ui.auto_mode && ui.light_on) {
             ui.bri_target = Calc_Auto_Brightness(ui.lux);
        }

        uint16_t mm;
        if(HGQ_VL53L0X_ReadMm(&g_tof, &mm) == 0) g_tof_mm = mm;
        xSemaphoreGive(xMutexUI);
        
        vTaskDelay(500); 
    }
}

void rfid_task(void *pvParameters) {
    while(1) {
        uint8_t uid_len, ret;
        ret = HGQ_RC522_PollUID(g_rfid_uid, &uid_len);
        
        if(ret == 0) { 
            if(!g_rfid_has_card) {
                g_rfid_has_card = 1;
                xSemaphoreTake(xMutexUI, portMAX_DELAY);
                UID_ToHexNoSpace(g_rfid_uid, uid_len, g_card_hex, sizeof(g_card_hex));
                
                char ev[64];
                int send = 0;
                
                if(g_op_mode == OP_WAIT_CHECKIN) {
                    // 修复：添加 type=event，与服务器 mqtt_service.py 匹配
                    sprintf(ev, "type=event&cmd=checkin&uid=%s&seat_id=%s", g_card_hex, DEV_ID);
                    send = 1;
                }
                else if(g_op_mode == OP_WAIT_CHECKOUT) {
                    sprintf(ev, "type=event&cmd=checkout&uid=%s&seat_id=%s", g_card_hex, DEV_ID);
                    send = 1;
                }
                else {
                    HGQ_UI_ShowPopup((char*)"请先点击屏幕！");
                    g_op_mode = OP_WARNING; 
                    g_popup_ts = 3; 
                    send = 0;
                }
                xSemaphoreGive(xMutexUI);
                
                if(send && g_mqtt_ok) {
                    xSemaphoreTake(xMutexESP, portMAX_DELAY);
                    MQTT_PubEvent(ev);
                    xSemaphoreGive(xMutexESP);
                    printf("[RFID] 刷卡上报: %s\r\n", ev);
                }
            }
        } 
        else if(ret == 2) { 
            g_rfid_has_card = 0;
        }
        
        vTaskDelay(100); 
    }
}
