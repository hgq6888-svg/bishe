/*
 * hgq_esp8266.c
 * ESP8266 WiFi模块AT命令驱动程序
 * 功能：WiFi连接、MQTT通信、数据收发
 * 作者：黄光全
 * 日期：2025-12-26
 * 
 * 模块特点：
 * 1. 支持Station模式连接WiFi热点
 * 2. 支持MQTT协议发布/订阅
 * 3. AT命令封装，简化操作
 * 4. 字符串转义处理，支持特殊字符
 * 5. 完整的调试信息输出
 * 
 * ESP8266 AT固件要求：
 * 1. 支持MQTT AT命令集
 * 2. 波特率：115200
 * 3. 固件版本建议1.7.0以上
 * 
 * 典型应用流程：
 * 1. 初始化：AT测试、关闭回显、设置模式
 * 2. 连接WiFi：指定SSID和密码
 * 3. 连接MQTT：配置用户信息，连接服务器
 * 4. 订阅主题：接收云端指令
 * 5. 发布消息：发送传感器数据
 */

#include "hgq_esp8266.h"
#include <stdio.h>   /* 为了 snprintf */

/* 内部缓冲区用于接收 ESP8266 的响应 */
static char esp_res_buf[512];  /* ESP8266响应缓冲区，存储AT命令返回结果 */

/**
 * @brief 字符串转义函数（用于MQTT消息中的特殊字符）
 * @param in: 输入字符串
 * @param out: 输出缓冲区（转义后的字符串）
 * @param out_sz: 输出缓冲区大小
 * @note 转义规则：
 *       \  -> \\   （反斜杠转义）
 *       "  -> \"   （双引号转义）
 *       \r -> \r   （回车字符转义）
 *       \n -> \n   （换行字符转义）
 * @retval None
 */
static void HGQ_ESP8266_EscapeQuotedString(const char *in, char *out, unsigned int out_sz)
{
    unsigned int j = 0;
    if (!out || out_sz == 0) return;
    if (!in) { out[0] = '\0'; return; }
    
    for (unsigned int i = 0; in[i] != '\0'; i++)
    {
        const char *rep = 0;
        char c = in[i];
        
        /* 识别需要转义的字符 */
        if (c == '\\')      rep = "\\\\";   /* 反斜杠转义 */
        else if (c == '"')  rep = "\\\"";   /* 双引号转义 */
        else if (c == '\r') rep = "\\r";    /* 回车转义 */
        else if (c == '\n') rep = "\\n";    /* 换行转义 */
        
        if (rep)  /* 需要转义的字符 */
        {
            for (unsigned int k = 0; rep[k] != '\0'; k++)
            {
                if (j + 1 >= out_sz) break;  /* 缓冲区溢出保护 */
                out[j++] = rep[k];
            }
        }
        else      /* 普通字符 */
        {
            if (j + 1 >= out_sz) break;      /* 缓冲区溢出保护 */
            out[j++] = c;
        }
        
        if (j + 1 >= out_sz) break;          /* 预留结束符位置 */
    }
    out[j] = '\0';  /* 字符串结束符 */
}

/**
 * @brief ESP8266模块初始化
 * @note 执行必要的AT命令序列：
 *       1. 重启模块（可选）
 *       2. 测试通信（AT）
 *       3. 关闭回显（ATE0）
 *       4. 设置为Station模式（AT+CWMODE=1）
 *       5. 连接MQTT代理服务器
 *       6. 订阅命令主题
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_Init(void)
{
    /* 0. 重启ESP8266（可选，确保从干净状态开始）*/
    HGQ_USART2_SendString("AT+RST\r\n");
    delay_ms(3000);  /* 等待重启完成 */
    
    /* 1. 测试AT命令通信 */
    if (HGQ_ESP8266_SendCmd("AT\r\n", "OK", 500) != ESP8266_OK) 
        return ESP8266_ERROR;
    
    /* 2. 关闭命令回显（避免响应中包含发送的命令）*/
    if (HGQ_ESP8266_SendCmd("ATE0\r\n", "OK", 500) != ESP8266_OK) 
        return ESP8266_ERROR;
    
    /* 3. 设置为Station模式（只作为WiFi客户端，不创建热点）*/
    if (HGQ_ESP8266_SendCmd("AT+CWMODE=1\r\n", "OK", 500) != ESP8266_OK) 
        return ESP8266_ERROR;
		
//		HGQ_ESP8266_DisconnectWiFi();
//		delay_ms(3000);
		
    /* 4. 连接WiFi（只作为WiFi客户端，不创建热点）*/
		if (HGQ_ESP8266_JoinAP("9636", "123456789abcc") != ESP8266_OK) 		
        return ESP8266_ERROR;

    /* 5. 连接到MQTT代理服务器（这里直接连接，实际应先连接WiFi）*/
    /* 注意：这里跳过了WiFi连接步骤，实际使用时需要先连接WiFi */
    if (HGQ_ESP8266_ConnectMQTT("1.14.163.35", 1883, "test01", "") != ESP8266_OK) 
        return ESP8266_ERROR;
    
//    /* 6. 订阅MQTT主题，用于接收云端指令 */
//    if (HGQ_ESP8266_MQTTSUB("stm32/cmd", 0) != ESP8266_OK) 
//        return ESP8266_ERROR;
    
    return ESP8266_OK;
}

/**
 * @brief 连接WiFi热点
 * @param ssid: WiFi名称
 * @param pwd: WiFi密码
 * @note 发送AT+CWJAP命令连接指定WiFi
 *       超时时间较长（5000ms），因为连接WiFi需要时间
 * @retval ESP8266_OK: 连接成功，ESP8266_ERROR: 连接失败
 */
ESP8266_Status HGQ_ESP8266_JoinAP(char *ssid, char *pwd)
{
    char cmd[128];
    /* 构建连接WiFi命令：AT+CWJAP="SSID","PASSWORD" */
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, pwd);
    
    /* 等待响应"WIFI CONNECTED"，超时5秒 */
	if (HGQ_ESP8266_SendCmd(cmd, "OK", 5000) != ESP8266_OK) 
        return ESP8266_ERROR;  /* MQTT连接超时较长 */
    return ESP8266_OK;
}

/**
 * @brief 获取ESP8266的IP地址
 * @param ip: IP地址输出缓冲区
 * @note 发送AT+CIFSR命令获取IP地址
 *       解析响应中的STAIP字段
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_GetIP(char *ip)
{
    /* 获取IP地址命令 */
    if (HGQ_ESP8266_SendCmd("AT+CIFSR\r\n", "OK", 2000) != ESP8266_OK) 
        return ESP8266_ERROR;
    
    /* 解析IP地址：响应格式 +CIFSR:STAIP,"192.168.1.100" */
    sscanf(esp_res_buf, "+CIFSR:STAIP,\"%s\"", ip);
    
    return ESP8266_OK;
}

/**
 * @brief 断开WiFi连接
 * @note 发送AT+CWQAP命令断开当前WiFi连接
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_DisconnectWiFi(void)
{
    if (HGQ_ESP8266_SendCmd("AT+CWQAP\r\n", "OK", 2000) != ESP8266_OK) 
        return ESP8266_ERROR;
    
    return ESP8266_OK;
}

/**
 * @brief 连接MQTT代理服务器
 * @param broker: MQTT服务器地址（IP或域名）
 * @param port: MQTT服务器端口（通常1883）
 * @param username: MQTT用户名（可为空）
 * @param password: MQTT密码（可为空）
 * @note 执行两个步骤：
 *       1. 配置MQTT用户信息（AT+MQTTUSERCFG）
 *       2. 连接到MQTT服务器（AT+MQTTCONN）
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_ConnectMQTT(char *broker, int port, char *username, char *password)
{
    char cmd[128];
    
    /* 步骤1: 设置MQTT用户配置
     * 参数：0=客户端索引，1=MQTT版本3.1.1，username，password，不使用证书，clean_session=0，无CA证书
     */
    sprintf(cmd, "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"\",0,0,\"\"\r\n", username, password);
    if (HGQ_ESP8266_SendCmd(cmd, "OK", 2000) != ESP8266_OK) 
        return ESP8266_ERROR;
    
    /* 步骤2: 连接MQTT代理服务器
     * 参数：0=客户端索引，broker地址，端口号，1=保持连接（keepalive）
     */
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%d,1\r\n", broker, port);
    if (HGQ_ESP8266_SendCmd(cmd, "OK", 5000) != ESP8266_OK) 
        return ESP8266_ERROR;  /* MQTT连接超时较长 */
    
    return ESP8266_OK;
}

/**
 * @brief 断开MQTT连接
 * @note 发送AT+MQTTDISC命令断开MQTT连接
 *       参数：0=客户端索引，0=立即断开
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_DisconnectMQTT(void)
{
    if (HGQ_ESP8266_SendCmd("AT+MQTTDISC=0,0\r\n", "OK", 2000) != ESP8266_OK) 
        return ESP8266_ERROR;
    
    return ESP8266_OK;
}

/**
 * @brief 订阅MQTT主题
 * @param topic: 要订阅的主题名称
 * @param qos: 服务质量等级（0,1,2）
 * @note 发送AT+MQTTSUB命令订阅指定主题
 *       通常订阅指令主题接收云端控制命令
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_MQTTSUB(char *topic, uint8_t qos)
{
    char cmd[128];
    
    /* 构建订阅命令：AT+MQTTSUB=0,"topic",qos */
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",%d\r\n", topic, qos);
    
    /* 发送命令并检查响应 */
    if (HGQ_ESP8266_SendCmd(cmd, "OK", 5000) != ESP8266_OK)
    {
        HGQ_USART1_SendString("-> 订阅主题失败\r\n");
        return ESP8266_ERROR;
    }
    
    HGQ_USART1_SendString("-> 订阅主题成功\r\n");
    return ESP8266_OK;
}

/**
 * @brief 发布MQTT消息
 * @param topic: 发布主题名称
 * @param message: 消息内容
 * @param qos: 服务质量等级（0,1,2）
 * @note 发送AT+MQTTPUB命令发布消息
 *       对消息内容进行转义处理，支持特殊字符
 * @retval ESP8266_OK: 成功，ESP8266_ERROR: 失败
 */
ESP8266_Status HGQ_ESP8266_MQTTPUB(char *topic, char *message, uint8_t qos)
{
    char esc[256];  /* 转义后的消息缓冲区 */
    char cmd[512];  /* 完整命令缓冲区 */
    
    /* 对消息内容进行转义处理 */
    HGQ_ESP8266_EscapeQuotedString(message, esc, sizeof(esc));
    
    /* 构建发布命令：AT+MQTTPUB=0,"topic","message",qos,0（retain=0）*/
    snprintf(cmd, sizeof(cmd),
             "AT+MQTTPUB=0,\"%s\",\"%s\",%d,0\r\n",
             topic, esc, qos);
    
    if (HGQ_ESP8266_SendCmd(cmd, "OK", 5000) != ESP8266_OK)
    {
        HGQ_USART1_SendString("-> 发布消息失败\r\n");
        return ESP8266_ERROR;
    }
    
    HGQ_USART1_SendString("-> 发布消息成功\r\n");
    return ESP8266_OK;
}

/**
 * @brief 向ESP8266发送AT命令并等待响应
 * @param cmd: AT命令字符串（必须包含\r\n）
 * @param reply: 期待的响应字符串（如"OK"）
 * @param timeout: 超时时间（毫秒）
 * @note 完整的AT命令通信流程：
 *       1. 清空接收缓冲区
 *       2. 发送AT命令（带调试输出）
 *       3. 接收ESP8266响应
 *       4. 检查是否包含期待的关键字
 *       5. 返回执行状态
 * @retval ESP8266_OK: 命令执行成功，ESP8266_ERROR: 执行失败
 */
ESP8266_Status HGQ_ESP8266_SendCmd(char *cmd, char *reply, uint32_t timeout)
{
    /* 清空本地接收缓冲区 */
    memset(esp_res_buf, 0, sizeof(esp_res_buf));
    
    /* 调试输出：显示发送的命令 */
    HGQ_USART1_SendString("-> 发送AT命令: ");
    HGQ_USART1_SendString(cmd);
    HGQ_USART1_SendString("\r\n");
    
    /* 通过USART2发送命令到ESP8266 */
    HGQ_USART2_SendString(cmd);
    
    /* 接收ESP8266的响应，使用带超时的接收函数 */
		if(strstr(cmd, "AT+CWJAP") != NULL || strstr(cmd, "AT+MQTTPUB") != NULL || strstr(cmd, "AT+MQTTUSERCFG") != NULL)
			HGQ_USART2_ReceiveString_wifi(esp_res_buf, sizeof(esp_res_buf), timeout);
		else
			HGQ_USART2_ReceiveString(esp_res_buf, sizeof(esp_res_buf), timeout);
    
    /* 调试输出：显示接收到的响应 */
    HGQ_USART1_SendString("<- 接收到的响应: \n {");
    HGQ_USART1_SendString(esp_res_buf);
    HGQ_USART1_SendString("}\r\n");
    
    /* 检查响应中是否包含期待的关键字 */
    if (strstr(esp_res_buf, reply) != NULL)
    {
        /* 响应匹配，命令执行成功 */
        //HGQ_USART1_SendString("-> 命令成功执行，响应匹配。\r\n");
        return ESP8266_OK;
    }
    else
    {
        /* 响应不匹配，命令执行失败 */
        //HGQ_USART1_SendString("-> 错误: 响应未匹配，可能出现问题。\r\n");
        return ESP8266_ERROR;
    }
}
