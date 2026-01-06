#ifndef __HGQ_ESP8266_H
#define __HGQ_ESP8266_H

#include "stm32f4xx.h"

typedef enum {
    ESP8266_OK = 0,
    ESP8266_ERROR,
    ESP8266_TIMEOUT
} ESP8266_Status;

ESP8266_Status HGQ_ESP8266_Init(void);
ESP8266_Status HGQ_ESP8266_JoinAP(char *ssid, char *pwd);
ESP8266_Status HGQ_ESP8266_ConnectMQTT(char *broker, int port, char *username, char *password);
ESP8266_Status HGQ_ESP8266_MQTTSUB(char *topic, uint8_t qos);

/* 标准发布 (带等待) */
ESP8266_Status HGQ_ESP8266_MQTTPUB(char *topic, char *message, uint8_t qos);
/* 极速发布 (不等待) */
void HGQ_ESP8266_MQTTPUB_Fast(char *topic, char *message, uint8_t qos);
/* 底层指令发送 */
ESP8266_Status HGQ_ESP8266_SendCmd(char *cmd, char *reply, uint32_t timeout);

/* ====== 新增：NTP 时间与状态检查 ====== */
void HGQ_ESP8266_EnableNTP(void);
uint8_t HGQ_ESP8266_GetNTPTime(uint8_t *h, uint8_t *m, uint8_t *s);
uint8_t HGQ_ESP8266_CheckStatus(void);

#endif
