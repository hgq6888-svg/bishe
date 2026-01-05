#ifndef __HGQ_ESP8266_H
#define __HGQ_ESP8266_H

#include "stm32f4xx.h"
#include "hgq_usart.h"
#include <string.h>
#include "delay.h"

/*
 * ESP8266 WiFi模块头文件
 * 
 * 功能概述：
 * 1. WiFi连接管理：连接/断开热点，获取IP
 * 2. MQTT通信：连接服务器，发布/订阅消息
 * 3. AT命令封装：简化ESP8266操作
 * 4. 状态码定义：统一错误处理
 * 
 * 硬件连接：
 * ESP8266 TX -> STM32 PA3 (USART2_RX)
 * ESP8266 RX -> STM32 PA2 (USART2_TX)
 * ESP8266 VCC -> 3.3V
 * ESP8266 GND -> GND
 * ESP8266 CH_PD -> 3.3V (使能引脚)
 * 
 * 通信参数：
 * 波特率：115200
 * 数据位：8
 * 停止位：1
 * 校验位：无
 */

/* ESP8266 状态枚举 */
typedef enum {
    ESP8266_OK = 0,      /* 操作成功 */
    ESP8266_ERROR,       /* 操作失败 */
    ESP8266_TIMEOUT      /* 操作超时 */
} ESP8266_Status;

/* ================= API函数声明 ================= */

/**
 * @brief ESP8266模块初始化
 * @note 执行基本配置：测试通信、关闭回显、设置模式
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_Init(void);

/**
 * @brief 连接WiFi热点
 * @param ssid: WiFi名称
 * @param password: WiFi密码
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_JoinAP(char *ssid, char *password);

/**
 * @brief 连接到MQTT代理服务器
 * @param broker: MQTT服务器地址
 * @param port: MQTT服务器端口（通常1883）
 * @param username: MQTT用户名（可为空）
 * @param password: MQTT密码（可为空）
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_ConnectMQTT(char *broker, int port, char *username, char *password);

/**
 * @brief 订阅MQTT主题
 * @param topic: 要订阅的主题名称
 * @param qos: 服务质量等级（0,1,2）
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_MQTTSUB(char *topic, uint8_t qos);

/**
 * @brief 发布MQTT消息
 * @param topic: 发布主题名称
 * @param message: 消息内容
 * @param qos: 服务质量等级（0,1,2）
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_MQTTPUB(char *topic, char *message, uint8_t qos);

/**
 * @brief 断开WiFi连接
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_DisconnectWiFi(void);

/**
 * @brief 断开MQTT连接
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_DisconnectMQTT(void);

/**
 * @brief 发送AT命令并检查响应
 * @param cmd: AT命令字符串
 * @param reply: 期待的响应关键字
 * @param timeout: 超时时间（毫秒）
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_SendCmd(char *cmd, char *reply, uint32_t timeout);

#endif /* __HGQ_ESP8266_H */
