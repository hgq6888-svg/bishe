#include "stm32f4xx.h"
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

/* ================== 参数定义 ================== */
#define WIFI_SSID       "9636"
#define WIFI_PASS       "123456789abcc"
#define MQTT_BROKER     "1.14.163.35"
#define MQTT_PORT       1883
#define MQTT_USER       "test01"
#define MQTT_PASS       ""
#define DEV_ID          "A15"

/* 屏幕显示：A区-18号 */
#define SEAT_NAME_GBK   "\x41\xC7\xF8\x2D\x31\x38\xBA\xC5" 

#define TOF_OCCUPIED_MM 380
#define PUB_TELE_MS     1000

/* 任务周期 */
#define PERIOD_RFID_MS   100
#define PERIOD_TOF_MS    200
#define PERIOD_AHT_MS    500
#define PERIOD_LUX_MS    500
#define PERIOD_TOUCH_MS  80   
#define UI_REFRESH_MS    100
#define PERIOD_NET_CHK   10000 
#define PERIOD_TIME_SYNC 60000 

/* 任务队列定义 */
#define TASK_QUEUE_SIZE  10
#define TASK_CMD_LEN     256
typedef struct {
    char cmds[TASK_QUEUE_SIZE][TASK_CMD_LEN];
    volatile uint16_t head;
    volatile uint16_t tail;
} TaskQueue_t;
static TaskQueue_t g_task_queue = {0};

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

/* 全局变量 */
static char g_state[12] = "FREE";
static char g_expect_uid[24] = "";
static HGQ_UI_Data ui = {0};
static HGQ_VL53L0X_Handle g_tof;
static uint16_t g_lux = 0, g_tof_mm = 0;
static uint8_t  g_bh1750_ok = 0;
static uint8_t  g_rfid_uid[10], g_rfid_has_card = 0;
static char     g_card_hex[24];
static u8       g_mqtt_ok = 0;

/* 时间管理变量 */
static uint8_t  g_time_h = 12, g_time_m = 0, g_time_s = 0; 
static uint32_t g_time_tick_ms = 0;
static char     g_time_str[10] = "12:00";

/* 工具函数 */
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
    const char *p = kv; u16 klen = strlen(key);
    while((p = strstr(p, key)) != 0) {
        if(p==kv || *(p-1)=='&') {
            if(p[klen] == '=') {
                const char *v = p + klen + 1;
                const char *e = strchr(v, '&');
                u16 n = (u16)((e ? e : (kv + strlen(kv))) - v);
                if(n >= out_sz) n = out_sz - 1;
                memcpy(out, v, n); out[n] = 0; return 1;
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
    snprintf(msg, sizeof(msg), "type=telemetry&seat_id=%s&state=%s&uid=%s&power=1&light=%d&light_mode=%s",
             DEV_ID, g_state, g_expect_uid, ui.light_on, ui.auto_mode?"AUTO":"MANUAL");
    HGQ_ESP8266_MQTTPUB_Fast(topic, msg, 0);
}
static void MQTT_PubHeartbeat(void) {
    char topic[64];
    Topic_Make(topic, sizeof(topic), "heartbeat");
    HGQ_ESP8266_MQTTPUB_Fast(topic, "ping=1", 0);
}
static void MQTT_PubEvent(const char *msg_kv) {
    char topic[64];
    Topic_Make(topic, sizeof(topic), "event");
    HGQ_ESP8266_MQTTPUB_Fast(topic, (char*)msg_kv, 0);
}

/* 连接流程 */
static void Network_Connect_Flow(void) {
    ui.esp_state = 1; 
    g_mqtt_ok = 0;
    HGQ_UI_Update(&ui, g_time_str);
    
    HGQ_USART2_SendString("AT+RST\r\n"); delay_ms(1500);
    HGQ_ESP8266_SendCmd("ATE0\r\n","OK",500);
    HGQ_ESP8266_SendCmd("AT+CWMODE=1\r\n","OK",500);

    if(HGQ_ESP8266_JoinAP(WIFI_SSID, WIFI_PASS) != ESP8266_OK) {
        ui.esp_state = 0; return;
    }
    if(HGQ_ESP8266_ConnectMQTT(MQTT_BROKER, MQTT_PORT, MQTT_USER, MQTT_PASS) != ESP8266_OK) {
        ui.esp_state = 0; return;
    }
    if(HGQ_ESP8266_MQTTSUB("stm32/cmd", 0) != ESP8266_OK) {
        ui.esp_state = 0; return;
    }

    ui.esp_state = 2; g_mqtt_ok = 1;
    HGQ_ESP8266_EnableNTP();
    MQTT_PubState(); MQTT_PubHeartbeat();
}

static void Task_Network_Check(void) {
    if(g_mqtt_ok == 0) {
        Network_Connect_Flow();
    } else {
        if(HGQ_ESP8266_CheckStatus() == 0) g_mqtt_ok = 0;
    }
}

static void Local_Time_Tick(uint32_t ms) {
    g_time_tick_ms += ms;
    if(g_time_tick_ms >= 1000) {
        g_time_tick_ms -= 1000;
        g_time_s++;
        if(g_time_s >= 60) {
            g_time_s = 0; g_time_m++;
            if(g_time_m >= 60) { g_time_m = 0; g_time_h++; if(g_time_h >= 24) g_time_h = 0; }
        }
        sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
    }
}

static void Task_Process_Commands(void) {
    char kv[TASK_CMD_LEN], typ[20], uid[24];
    while(TaskQueue_Pop(kv)) {
        if(!KV_Get(kv, "type", typ, sizeof(typ))) continue;
        if(strcmp(typ, "reserve") == 0) {
            if(KV_Get(kv, "uid", uid, sizeof(uid))) {
                strncpy(g_expect_uid, uid, sizeof(g_expect_uid)-1);
                /* 如果有需要，这里可以将uid更新到界面: strncpy(ui.user_str, uid, ...); */
            }
            else g_expect_uid[0] = 0;
            strncpy(g_state, "RESERVED", sizeof(g_state)-1);
            strncpy(ui.status, "Reserved", sizeof(ui.status)-1);
            MQTT_PubState();
        } else if(strcmp(typ, "release") == 0) {
            g_expect_uid[0] = 0;
            strncpy(g_state, "FREE", sizeof(g_state)-1);
            strncpy(ui.status, "Free", sizeof(ui.status)-1);
            /* 清空界面信息 */
            strcpy(ui.user_str, "--");
            strcpy(ui.reserve_t, "--");
            strcpy(ui.start_t, "--");
            strcpy(ui.remain_t, "--");
            MQTT_PubState();
        }
    }
}

static void Task_Receive_Network(void) {
    static char line[512]; static u16 idx = 0; uint8_t ch;
    while(HGQ_USART2_IT_GetChar(&ch)) {
        if(idx < sizeof(line)-1) line[idx++] = ch;
        if(ch == '\n') {
            line[idx] = 0; idx = 0;
            if(strstr(line, "+MQTTSUBRECV")) {
                char *last = strrchr(line, '\"');
                if(last) {
                    *last = 0;
                    char *prev = strrchr(line, '\"');
                    if(prev) TaskQueue_Push(prev + 1);
                }
            }
        }
    }
}

static void APP_TouchProcess(void) {
    if(tp_dev.scan(0)) {
        u16 x = tp_dev.x[0], y = tp_dev.y[0];
        if(HGQ_UI_TouchBtn_Manual(x, y)) { ui.auto_mode = 0; MQTT_PubState(); }
        else if(HGQ_UI_TouchBtn_Auto(x, y)) { 
            ui.auto_mode = 1; 
            if(ui.light_on) ui.bri_target = Calc_Auto_Brightness(ui.lux);
            MQTT_PubState(); 
        }
        else if(HGQ_UI_TouchBtn_On(x, y)) { ui.light_on = 1; MQTT_PubState(); }
        else if(HGQ_UI_TouchBtn_Off(x, y)) { ui.light_on = 0; MQTT_PubState(); }
        
        if(ui.auto_mode == 0 && ui.light_on == 1) {
            if(HGQ_UI_TouchBtn_BriUp(x, y)) {
                if(ui.bri_target <= 90) ui.bri_target += 10; else ui.bri_target = 100;
            }
            else if(HGQ_UI_TouchBtn_BriDown(x, y)) {
                if(ui.bri_target >= 10) ui.bri_target -= 10; else ui.bri_target = 0;
            }
        }
    }
}

static void Task_Sensors(void) {
    float tc, rh;
    HGQ_AHT20_Read(&tc, &rh);
    ui.temp_x10 = (int)(tc * 10 + 0.5f);
    ui.humi = (int)(rh + 0.5f);
    
    if(g_bh1750_ok) { HGQ_BH1750_ReadLux(&g_lux); ui.lux = g_lux; } 
    else { g_bh1750_ok = !HGQ_BH1750_Init(0x23); if(!g_bh1750_ok) ui.lux = -1; }
    
    if(ui.auto_mode == 1 && ui.light_on == 1) {
        if(ui.lux >= 0) ui.bri_target = Calc_Auto_Brightness(ui.lux);
        else ui.bri_target = 50; 
    }
    
    uint16_t mm; 
    if(HGQ_VL53L0X_ReadMm(&g_tof, &mm) == 0) g_tof_mm = mm;
}

static void Task_RC522(void) {
    uint8_t uid_len, ret = HGQ_RC522_PollUID(g_rfid_uid, &uid_len);
    if(ret == 0 && !g_rfid_has_card) {
        g_rfid_has_card = 1;
        UID_ToHexNoSpace(g_rfid_uid, uid_len, g_card_hex, sizeof(g_card_hex));
        /* 刷卡后，如果需要可以临时显示UID在用户栏，例如: */
        // strncpy(ui.user_str, g_card_hex, sizeof(ui.user_str)-1);
        if(g_mqtt_ok) {
            char ev[64]; sprintf(ev, "type=checkin&uid=%s", g_card_hex);
            MQTT_PubEvent(ev);
        }
    } else if(ret == 2) g_rfid_has_card = 0;
}

int main(void) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_init(168); uart_init(115200); HGQ_USART2_Init(115200);
    LED_Init(); LCD_Init(); LCD_Display_Dir(1); tp_dev.init();
    W25QXX_Init(); font_init();
    
    HGQ_UI_Init(); 
    /* 初始化默认UI显示内容 */
    strncpy(ui.area_seat, SEAT_NAME_GBK, sizeof(ui.area_seat)-1);
    strcpy(ui.status, "Free");
    strcpy(ui.user_str, "--");
    strcpy(ui.reserve_t, "--");
    strcpy(ui.start_t, "--");
    strcpy(ui.remain_t, "--");
    
    HGQ_UI_DrawFramework();
    
    HGQ_AHT20_Init(); g_bh1750_ok = !HGQ_BH1750_Init(0x23);
    HGQ_RC522_Init(); HGQ_VL53L0X_I2C_Init(); HGQ_VL53L0X_Begin(&g_tof, 0x29);
    
    Network_Connect_Flow();

    uint32_t tick = 0, t_ui=0, t_sens=0, t_rfid=0, t_pub=0, t_net=0, t_sync=0;
    
    while(1) {
        Task_Receive_Network();
        Task_Process_Commands();
        
        delay_ms(5); tick += 5;
        Local_Time_Tick(5);
        
        if(tick - t_rfid >= PERIOD_RFID_MS) { t_rfid = tick; Task_RC522(); }
        if(tick - t_sens >= PERIOD_AHT_MS)  { t_sens = tick; Task_Sensors(); }
        
        if(tick % PERIOD_TOUCH_MS == 0) APP_TouchProcess();
        
        if(tick - t_ui >= UI_REFRESH_MS) {
            t_ui = tick;
            HGQ_UI_Update(&ui, g_time_str);
            LED0_SetOn(ui.light_on);
            LED0_SetBrightness((u8)HGQ_UI_GetBrightnessNow());
        }
        
        if(g_mqtt_ok && tick - t_pub >= PUB_TELE_MS) {
            t_pub = tick;
            MQTT_PubTelemetry();
        }
        
        if(tick - t_net >= PERIOD_NET_CHK) {
            t_net = tick;
            Task_Network_Check();
        }
        
        if(g_mqtt_ok && tick - t_sync >= PERIOD_TIME_SYNC) {
            t_sync = tick;
            uint8_t h, m, s;
            if(HGQ_ESP8266_GetNTPTime(&h, &m, &s)) {
                g_time_h = h; g_time_m = m; g_time_s = s;
                sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
            }
        }
    }
}
