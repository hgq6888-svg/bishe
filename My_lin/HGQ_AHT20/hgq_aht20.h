#ifndef __HGQ_AHT20_H
#define __HGQ_AHT20_H

#include "stm32f4xx.h"

/*
 * AHT20数字温湿度传感器头文件
 * 
 * 传感器特性：
 * 1. 温度范围：-40℃ ~ +85℃
 * 2. 湿度范围：0%RH ~ 100%RH
 * 3. 温度精度：±0.3℃（典型值）
 * 4. 湿度精度：±2%RH（典型值）
 * 5. 响应时间：温度8秒，湿度5秒
 * 6. I2C地址：0x38（固定）
 * 
 * 典型应用：
 * 1. 环境监测系统
 * 2. 智能家居温湿度控制
 * 3. HVAC系统
 * 4. 农业温室监测
 * 5. 仓库环境监控
 * 
 * 连接方式：
 * VCC -> 3.3V
 * GND -> GND
 * SCL -> PB6（第二路I2C时钟）
 * SDA -> PB7（第二路I2C数据）
 */

/**
 * @brief AHT20传感器初始化
 * @note 初始化I2C总线并发送校准命令
 *       传感器需要40ms上电稳定时间
 * @retval 0: 初始化成功，1: 初始化失败
 */
uint8_t HGQ_AHT20_Init(void);

/**
 * @brief 读取AHT20的温湿度数据
 * @param temp_c: 温度输出指针（单位：℃）
 * @param humi_rh: 湿度输出指针（单位：%RH）
 * @note 测量过程需要约80ms（75ms测量+5ms处理）
 *       建议测量间隔至少2秒以保证精度
 * @retval 0: 读取成功
 *         1: 触发测量失败
 *         2: 状态读取失败
 *         3: 测量超时
 *         4: 数据读取失败
 */
uint8_t HGQ_AHT20_Read(float *temp_c, float *humi_rh);

#endif /* __HGQ_AHT20_H */
