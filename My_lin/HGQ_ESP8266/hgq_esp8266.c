#include "hgq_esp8266.h"
#include "hgq_usart.h"
#include "delay.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // for atoi

static char s_init_buf[256]; 

/* 内部简单接收 */
static int Wait_Reply(const char *reply, uint32_t timeout_ms)
{
    uint32_t time = 0;
    uint8_t ch;
    uint16_t idx = 0;
    
    memset(s_init_buf, 0, sizeof(s_init_buf));
    while(time < timeout_ms)
    {
        while(HGQ_USART2_IT_GetChar(&ch))
        {
            if(idx < sizeof(s_init_buf)-1) s_init_buf[idx++] = ch;
        }
        if(strstr(s_init_buf, reply)) return 1; 
        delay_ms(1);
        time++;
    }
    return 0; 
}

ESP8266_Status HGQ_ESP8266_SendCmd(char *cmd, char *reply, uint32_t timeout)
{
    HGQ_USART2_IT_ClearRxBuffer(); 
    HGQ_USART2_SendString(cmd);    
    if(Wait_Reply(reply, timeout)) return ESP8266_OK;
    return ESP8266_ERROR;
}

static void EscapeString(const char *in, char *out, int out_sz)
{
    int j = 0;
    for (int i = 0; in[i] && j < out_sz - 2; i++) {
        if (in[i] == '\\' || in[i] == '"') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = '\0';
}

ESP8266_Status HGQ_ESP8266_MQTTPUB(char *topic, char *message, uint8_t qos)
{
    char esc[256];
    char cmd[512];
    EscapeString(message, esc, sizeof(esc));
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",%d,0\r\n", topic, esc, qos);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 500); 
}

void HGQ_ESP8266_MQTTPUB_Fast(char *topic, char *message, uint8_t qos)
{
    char esc[256];
    char cmd[512];
    EscapeString(message, esc, sizeof(esc));
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",%d,0\r\n", topic, esc, qos);
    HGQ_USART2_SendString(cmd);
}

ESP8266_Status HGQ_ESP8266_Init(void) { return ESP8266_OK; } 

ESP8266_Status HGQ_ESP8266_JoinAP(char *ssid, char *pwd)
{
    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 8000); 
}

ESP8266_Status HGQ_ESP8266_ConnectMQTT(char *broker, int port, char *username, char *password)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"\",0,0,\"\"\r\n", username, password);
    HGQ_ESP8266_SendCmd(cmd, "OK", 1000);
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", broker, port);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 5000);
}

ESP8266_Status HGQ_ESP8266_MQTTSUB(char *topic, uint8_t qos)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",%d\r\n", topic, qos);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 2000);
}

/* ====== 新增实现 ====== */

void HGQ_ESP8266_EnableNTP(void)
{
    /* 开启NTP，时区+8，使用阿里云NTP */
    HGQ_ESP8266_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp1.aliyun.com\"\r\n", "OK", 500);
}

uint8_t HGQ_ESP8266_GetNTPTime(uint8_t *h, uint8_t *m, uint8_t *s)
{
    uint32_t time = 0;
    uint8_t ch;
    uint16_t idx = 0;
    char *p_start, *p_time;
    
    HGQ_USART2_IT_ClearRxBuffer();
    HGQ_USART2_SendString("AT+CIPSNTPTIME?\r\n");
    
    memset(s_init_buf, 0, sizeof(s_init_buf));
    while(time < 1000) 
    {
        while(HGQ_USART2_IT_GetChar(&ch))
        {
            if(idx < sizeof(s_init_buf)-1) s_init_buf[idx++] = ch;
        }
        if(strstr(s_init_buf, "+CIPSNTPTIME:")) break;
        delay_ms(1);
        time++;
    }
    
    /* 解析格式: +CIPSNTPTIME:Mon Nov 11 11:11:11 2024 */
    p_start = strstr(s_init_buf, "+CIPSNTPTIME:");
    if(p_start)
    {
        p_start += 13; 
        p_time = strchr(p_start, ':'); // 找第一个冒号 (HH:MM)
        while(p_time)
        {
            /* 简单验证前后是否为数字 */
            if(p_time > s_init_buf && *(p_time-1) >= '0' && *(p_time-1) <= '9')
            {
                *h = (uint8_t)atoi(p_time - 2); 
                *m = (uint8_t)atoi(p_time + 1);
                char *p_sec = strchr(p_time + 1, ':');
                if(p_sec) {
                    *s = (uint8_t)atoi(p_sec + 1);
                    return 1; 
                }
            }
            p_time = strchr(p_time + 1, ':');
        }
    }
    return 0; 
}

/* 检查是否在线 (通过查询 AP 连接状态) */
uint8_t HGQ_ESP8266_CheckStatus(void)
{
    if(HGQ_ESP8266_SendCmd("AT+CWJAP?\r\n", "+CWJAP:", 3000) == ESP8266_OK) {
        return 1;
    }
    return 0;
}
