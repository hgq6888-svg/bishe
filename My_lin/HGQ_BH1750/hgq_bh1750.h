#ifndef __HGQ_BH1750_H
#define __HGQ_BH1750_H

#include "stm32f4xx.h"

/*
 * BH1750光照强度传感器头文件
 * 
 * 传感器特性：
 * 1. 测量范围：0-65535 lux
 * 2. 光谱响应接近人眼（可见光）
 * 3. 数字输出，I2C接口
 * 4. 低功耗（典型0.12mA）
 * 5. 内置16位ADC，无需外部元件
 * 
 * 典型应用：
 * 1. 室内光照度监测
 * 2. 自动调光系统
 * 3. 智能照明控制
 * 4. 环境监测系统
 * 
 * 连接方式：
 * VCC -> 3.3V/5V
 * GND -> GND
 * SCL -> PC0（第三路I2C时钟）
 * SDA -> PC1（第三路I2C数据）
 * ADDR -> GND（地址0x23）或VCC（地址0x5C）
 */

/**
 * @brief BH1750传感器初始化
 * @param addr_7bit: BH1750的7位I2C地址
 *       0x23: 地址引脚接地或悬空
 *       0x5C: 地址引脚接VCC
 * @retval 0: 初始化成功
 *         1: 上电失败
 *         2: 模式设置失败
 */
uint8_t HGQ_BH1750_Init(uint8_t addr_7bit);

/**
 * @brief 读取光照强度值
 * @param lux: 光照强度输出指针（单位：lux）
 * @note 值范围：0-65535 lux
 *       典型室内光照：100-1000 lux
 *       阴天室外：1000-5000 lux
 *       晴天室外：10000-100000 lux
 * @retval 0: 读取成功，1: 读取失败
 */
uint8_t HGQ_BH1750_ReadLux(uint16_t *lux);

#endif /* __HGQ_BH1750_H */
