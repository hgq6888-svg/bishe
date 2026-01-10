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

#define PERIOD_RFID_MS   100
#define PERIOD_TOF_MS    200
#define PERIOD_AHT_MS    500
#define PERIOD_LUX_MS    500
#define PERIOD_TOUCH_MS  80   
#define UI_REFRESH_MS    100
#define PERIOD_NET_CHK   10000 
#define PERIOD_TIME_SYNC 60000 

#define TASK_QUEUE_SIZE  10
#define TASK_CMD_LEN     256

/* 操作模式定义 */
typedef enum {
    OP_NORMAL = 0,
    OP_WAIT_CHECKIN,  
    OP_WAIT_CHECKOUT,
    OP_WARNING // 【新增】警告弹窗模式，用于暂停UI刷新
} OpMode_t;

static OpMode_t g_op_mode = OP_NORMAL;
static uint32_t g_popup_ts = 0; 

typedef struct {
    char cmds[TASK_QUEUE_SIZE][TASK_CMD_LEN];
    volatile uint16_t head;
    volatile uint16_t tail;
} TaskQueue_t;
static TaskQueue_t g_task_queue = {0};

/* 弹窗文本GBK */
const u8 STR_POP_IN[]   = {0xC7,0xEB,0xCB,0xA2,0xBF,0xA8,0xC7,0xA9,0xB5,0xBD,0x00}; // 请刷卡签到
const u8 STR_POP_OUT[]  = {0xC7,0xEB,0xCB,0xA2,0xBF,0xA8,0xC7,0xA9,0xCD,0xCB,0x00}; // 请刷卡签退
const u8 STR_POP_ERR[]  = {0xBF,0xA8,0xBA,0xC5,0xB4,0xED,0xCE,0xF3,0x00}; // 卡号错误
// 【新增】提示语：请先点击屏幕 (GBK编码)
const u8 STR_POP_WARN[] = {0xC7,0xEB,0xCF,0xC8,0xB5,0xE3,0xBB,0xF7,0xC6,0xC1,0xC4,0xBB,0x00}; 

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
static uint32_t g_time_tick_ms = 0;
static char     g_time_str[10] = "--:--"; 

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
static void MQTT_PubEvent(const char *msg_kv) {
    char topic[64];
    Topic_Make(topic, sizeof(topic), "event");
    HGQ_ESP8266_MQTTPUB_Fast(topic, (char*)msg_kv, 0);
}

static void Task_Process_Commands(void) {
    char kv[TASK_CMD_LEN], cmd_val[20], uid[24], sid[20], user[32], reason[20];
    
    while(TaskQueue_Pop(kv)) {
        printf("[Cmd] %s\r\n", kv); 
        
        if(!KV_Get(kv, "cmd", cmd_val, sizeof(cmd_val))) continue;
        
        if(strcmp(cmd_val, "time_sync") == 0) {
            char t_buf[16];
            if(KV_Get(kv, "time", t_buf, sizeof(t_buf))) {
                if(strlen(t_buf) >= 8) {
                    t_buf[2] = 0; t_buf[5] = 0;
                    g_time_h = atoi(t_buf);
                    g_time_m = atoi(t_buf+3);
                    g_time_s = atoi(t_buf+6);
                    sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
                    printf("[Sync] Time Updated: %s\r\n", g_time_str);
                    g_need_ui_refresh = 1;
                }
            }
            continue;
        }

        if(KV_Get(kv, "seat_id", sid, sizeof(sid))) {
            if(strcmp(sid, DEV_ID) != 0) continue; 
        }
        
        if(strcmp(cmd_val, "deny") == 0) {
            printf("[INFO] Access Denied!\r\n");
            HGQ_UI_ShowPopup((char*)STR_POP_ERR);
            g_popup_ts = 15 * 200; 
            g_op_mode = OP_WAIT_CHECKIN; 
        }
        else if(strcmp(cmd_val, "reserve") == 0) {
            if(KV_Get(kv, "user", user, sizeof(user))) strncpy(ui.user_str, user, sizeof(ui.user_str)-1);
            else strcpy(ui.user_str, "User");
            if(KV_Get(kv, "uid", uid, sizeof(uid))) strncpy(g_expect_uid, uid, sizeof(g_expect_uid)-1);
            else g_expect_uid[0] = 0;
            
            char t_buf[32];
            if(KV_Get(kv, "expires_at", t_buf, sizeof(t_buf))) {
                if(strlen(t_buf) >= 16) { 
                    strncpy(ui.reserve_t, t_buf+11, 5); 
                    ui.reserve_t[5] = 0;
                    sprintf(ui.remain_t, "To %s", ui.reserve_t);
                } else {
                    strncpy(ui.reserve_t, t_buf, 5);
                }
            }
            strcpy(ui.start_t, "--:--"); 
            strncpy(g_state, "RESERVED", sizeof(g_state)-1);
            strncpy(ui.status, "Rsrv(15m)", sizeof(ui.status)-1);
            MQTT_PubState();
            g_need_ui_refresh = 1;
        } 
        else if(strcmp(cmd_val, "checkin_ok") == 0) {
            sprintf(ui.start_t, "%02d:%02d", g_time_h, g_time_m);
            strncpy(g_state, "IN_USE", sizeof(g_state)-1);
            strncpy(ui.status, "In Use", sizeof(ui.status)-1);
            ui.light_on = 1; 
            MQTT_PubState();
            g_need_ui_refresh = 1;
        }
        else if(strcmp(cmd_val, "release") == 0 || strcmp(cmd_val, "checkout_ok") == 0) {
            g_expect_uid[0] = 0;
            strncpy(g_state, "FREE", sizeof(g_state)-1);
            strncpy(ui.status, "Free", sizeof(ui.status)-1);
            strcpy(ui.user_str, "--");
            strcpy(ui.reserve_t, "--");
            strcpy(ui.start_t, "--");
            strcpy(ui.remain_t, "--");
            ui.light_on = 0; 
            MQTT_PubState(); 
            g_need_ui_refresh = 1;
        }
    }
}

static void Network_Connect_Flow(void) {
    ui.esp_state = 1; 
    g_mqtt_ok = 0;
    HGQ_UI_Update(&ui, g_time_str);
    
    HGQ_USART2_SendString("AT+RST\r\n"); delay_ms(2000);
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
    MQTT_PubState(); 
}

static void Task_Network_Check(void) {
    if(g_mqtt_ok == 0) {
        Network_Connect_Flow();
    } else {
        if(HGQ_ESP8266_CheckStatus() == 0) {
            g_mqtt_ok = 0;
        }
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
        if(g_time_str[0] != '-') {
            sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
        }
    }
    
    if(g_op_mode != OP_NORMAL) {
        if(g_popup_ts > 0) g_popup_ts--;
        if(g_popup_ts == 0) {
            g_op_mode = OP_NORMAL;
            g_force_redraw = 1; 
        }
    }
}

static void Task_Receive_Network(void) {
    static char line[512]; 
    static u16 idx = 0; 
    uint8_t ch;
    
    while(HGQ_USART2_IT_GetChar(&ch)) {
        if(idx < sizeof(line)-1) line[idx++] = ch;
        if(ch == '\n') {
            line[idx] = 0; 
            idx = 0;
            if(strstr(line, "+MQTTSUBRECV")) {
                char *p_start = strstr(line, "cmd=");
                if(p_start) {
                    TaskQueue_Push(p_start);
                } else {
                    char *last_comma = strrchr(line, ',');
                    if(last_comma && *(last_comma+1) != '\0') {
                        TaskQueue_Push(last_comma + 1);
                    }
                }
            }
        }
    }
}

/* ================== 【修改】优化触摸处理 ================== */
static void APP_TouchProcess(void) {
    static u8 s_last_touch = 0; // 上一次触摸状态
    u8 is_touched = tp_dev.scan(0); // 获取当前触摸状态

    // 只有在检测到触摸且上次未触摸时（上升沿），才执行操作
    // 这可以防止手指按住不放时，连续触发导致弹窗瞬间打开又关闭
    if(is_touched && !s_last_touch) {
        u16 x = tp_dev.x[0], y = tp_dev.y[0];
        
        // 如果当前有弹窗 (模式不是 NORMAL)，任何点击都关闭弹窗
        if(g_op_mode != OP_NORMAL) {
            g_op_mode = OP_NORMAL;
            g_force_redraw = 1;
            // 此次操作仅用于关闭弹窗，不处理按钮逻辑
        } else {
            // 正常的按钮处理逻辑
            int changed = 0;
            if(HGQ_UI_TouchBtn_Check(x, y)) {
                if(strcmp(g_state, "IN_USE") == 0) {
                    g_op_mode = OP_WAIT_CHECKOUT;
                    g_popup_ts = 15 * 200; 
                    HGQ_UI_ShowPopup((char*)STR_POP_OUT);
                } else {
                    g_op_mode = OP_WAIT_CHECKIN;
                    g_popup_ts = 15 * 200; 
                    HGQ_UI_ShowPopup((char*)STR_POP_IN);
                }
                // 这里不需要return，因为外层有了else保护，逻辑更清晰
            } else {
                if(strcmp(g_state, "IN_USE") == 0) {
                    if(HGQ_UI_TouchBtn_Mode(x, y)) { ui.auto_mode = !ui.auto_mode; changed=1; }
                    else if(HGQ_UI_TouchBtn_On(x, y)) { ui.light_on = 1; changed=1; }
                    else if(HGQ_UI_TouchBtn_Off(x, y)) { ui.light_on = 0; changed=1; }
                    
                    if(ui.auto_mode == 0 && ui.light_on == 1) {
                        if(HGQ_UI_TouchBtn_BriUp(x, y)) { if(ui.bri_target <= 90) ui.bri_target += 10; else ui.bri_target = 100; }
                        else if(HGQ_UI_TouchBtn_BriDown(x, y)) { if(ui.bri_target >= 10) ui.bri_target -= 10; else ui.bri_target = 0; }
                    }
                }
                if(changed) MQTT_PubState();
            }
        }
    }
    s_last_touch = is_touched; // 更新触摸状态
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

/* ================== 【修改】修复RFID逻辑 ================== */
static void Task_RC522(uint32_t tick) {
    uint8_t uid_len, ret;
    ret = HGQ_RC522_PollUID(g_rfid_uid, &uid_len);
    
    if(ret == 0) { 
        if(!g_rfid_has_card) {
            g_rfid_has_card = 1;
            UID_ToHexNoSpace(g_rfid_uid, uid_len, g_card_hex, sizeof(g_card_hex));
            
            char ev[64];
            int send = 0;
            
            if(g_op_mode == OP_WAIT_CHECKIN) {
                sprintf(ev, "cmd=checkin&uid=%s&seat_id=%s", g_card_hex, DEV_ID);
                g_op_mode = OP_NORMAL; g_force_redraw = 1; 
                send = 1;
            }
            else if(g_op_mode == OP_WAIT_CHECKOUT) {
                sprintf(ev, "cmd=checkout&uid=%s&seat_id=%s", g_card_hex, DEV_ID);
                g_op_mode = OP_NORMAL; g_force_redraw = 1; 
                send = 1;
            }
            else {
                // 【核心修复】未点击屏幕时刷卡，不发送数据，只弹窗提示
                printf("[RFID] Ignored: Please click button first.\r\n");
                //HGQ_UI_ShowPopup((char*)STR_POP_WARN); // 弹窗：请先点击屏幕
								HGQ_UI_ShowPopup((char*)"点击签到/签退在刷卡！");
                
                // 【关键修改】设置模式为 WARNING，防止主循环的 HGQ_UI_Update 刷新背景覆盖弹窗
                g_op_mode = OP_WARNING; 
                g_popup_ts = 15 * 200; // 显示3秒
                send = 0;
            }
            
            if(send && g_mqtt_ok) {
                MQTT_PubEvent(ev);
                printf("[RFID] Sent: %s\r\n", ev);
            }
        } 
    } 
    else if(ret == 2) { 
        g_rfid_has_card = 0;
    }
}

int main(void) {
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    delay_init(168); 
    uart_init(115200); 
    HGQ_USART2_Init(115200);
    
    LED_Init(); LCD_Init(); LCD_Display_Dir(1); tp_dev.init();
    W25QXX_Init(); font_init();
    
    printf("\r\n[SYSTEM] Start...\r\n");
    
    HGQ_UI_Init(); 
    strncpy(ui.area_seat, SEAT_NAME_GBK, sizeof(ui.area_seat)-1);
    strcpy(ui.status, "Free"); strcpy(ui.user_str, "--");
    strcpy(ui.reserve_t, "--"); strcpy(ui.start_t, "--"); strcpy(ui.remain_t, "--");
    
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
        
        if(tick - t_rfid >= PERIOD_RFID_MS) { t_rfid = tick; Task_RC522(tick); }
        if(tick - t_sens >= PERIOD_AHT_MS)  { t_sens = tick; Task_Sensors(); }
        
        if(tick % PERIOD_TOUCH_MS == 0) APP_TouchProcess();
        
        if(g_force_redraw) {
            g_force_redraw = 0;
            HGQ_UI_ResetCache(); 
            HGQ_UI_DrawFramework(); 
            g_need_ui_refresh = 1; 
        }

        if(g_need_ui_refresh || tick - t_ui >= UI_REFRESH_MS) {
            g_need_ui_refresh = 0;
            t_ui = tick;
            // 【关键点】如果模式不是 NORMAL (包括 WAIT_CHECKIN, WARNING)，则不刷新UI
            // 这保护了弹窗不被数据覆盖
            if(g_op_mode == OP_NORMAL) {
                HGQ_UI_Update(&ui, g_time_str);
            }
            LED0_SetOn(ui.light_on);
            LED0_SetBrightness((u8)HGQ_UI_GetBrightnessNow());
            if(strcmp(g_state, "IN_USE") == 0) Relay_Set(1); else Relay_Set(0);
        }
        
        if(g_mqtt_ok && tick - t_pub >= 2000) {
            t_pub = tick;
            MQTT_PubTelemetry();
        }
        
        if(tick - t_net >= PERIOD_NET_CHK) {
            t_net = tick;
            Task_Network_Check();
        }
        
        if(g_mqtt_ok && tick - t_sync >= PERIOD_TIME_SYNC) {
            t_sync = tick;
            if(g_time_str[0] == '-') {
                 uint8_t h, m, s;
                 if(HGQ_ESP8266_GetNTPTime(&h, &m, &s)) {
                     g_time_h = h; g_time_m = m; g_time_s = s;
                     sprintf(g_time_str, "%02d:%02d", g_time_h, g_time_m);
                 }
            }
        }
    }
}
