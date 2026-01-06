#include "hgq_esp8266.h"
#include "hgq_usart.h"
#include "delay.h"
#include <string.h>
#include <stdio.h>

/* 仅在初始化时使用的简单缓冲区 */
static char s_init_buf[256]; 

/* 内部简单接收，仅用于初始化阶段的阻塞等待 */
static int Wait_Reply(const char *reply, uint32_t timeout_ms)
{
    uint32_t time = 0;
    uint8_t ch;
    uint16_t idx = 0;
    
    memset(s_init_buf, 0, sizeof(s_init_buf));
    
    while(time < timeout_ms)
    {
        /* 从串口缓冲区取数据 */
        while(HGQ_USART2_IT_GetChar(&ch))
        {
            if(idx < sizeof(s_init_buf)-1) s_init_buf[idx++] = ch;
        }
        
        if(strstr(s_init_buf, reply)) return 1; // 找到回复
        
        delay_ms(1);
        time++;
    }
    return 0; // 超时
}

ESP8266_Status HGQ_ESP8266_SendCmd(char *cmd, char *reply, uint32_t timeout)
{
    HGQ_USART2_IT_ClearRxBuffer(); // 清空旧数据
    HGQ_USART2_SendString(cmd);    // 发送
    if(Wait_Reply(reply, timeout)) return ESP8266_OK;
    return ESP8266_ERROR;
}

/* 极速转义字符处理 */
static void EscapeString(const char *in, char *out, int out_sz)
{
    int j = 0;
    for (int i = 0; in[i] && j < out_sz - 2; i++) {
        if (in[i] == '\\' || in[i] == '"') out[j++] = '\\';
        out[j++] = in[i];
    }
    out[j] = '\0';
}

/* 普通发布：会阻塞等待 OK，不建议在 UI 线程高频使用 */
ESP8266_Status HGQ_ESP8266_MQTTPUB(char *topic, char *message, uint8_t qos)
{
    char esc[256];
    char cmd[512];
    EscapeString(message, esc, sizeof(esc));
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",%d,0\r\n", topic, esc, qos);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 500); // 等待 500ms
}

/* 【核心优化】极速发布：发完就跑，不等待回复，耗时 < 2ms */
void HGQ_ESP8266_MQTTPUB_Fast(char *topic, char *message, uint8_t qos)
{
    char esc[256];
    char cmd[512];
    
    EscapeString(message, esc, sizeof(esc));
    
    /* 组包 */
    snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",%d,0\r\n", topic, esc, qos);
    
    /* 直接发送到硬件 FIFO，不等待 ESP8266 回复 */
    HGQ_USART2_SendString(cmd);
}

/* 剩下的初始化代码保持阻塞模式，保证启动稳定 */
ESP8266_Status HGQ_ESP8266_Init(void) { return ESP8266_OK; } // 实际逻辑在 main 调用

ESP8266_Status HGQ_ESP8266_JoinAP(char *ssid, char *pwd)
{
    char cmd[128];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 8000); // 连 WiFi 慢，给 8s
}

ESP8266_Status HGQ_ESP8266_ConnectMQTT(char *broker, int port, char *username, char *password)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"\",0,0,\"\"\r\n", username, password);
    HGQ_ESP8266_SendCmd(cmd, "OK", 1000);
    
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", broker, port);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 5000);
}

/* 修正原 .h 定义错误，这里实现正确的 SUB */
ESP8266_Status HGQ_ESP8266_MQTTSUB(char *topic, uint8_t qos)
{
    char cmd[128];
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",%d\r\n", topic, qos);
    return HGQ_ESP8266_SendCmd(cmd, "OK", 2000);
}
