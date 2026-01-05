/*
 * hgq_hcsr501.c
 * HCSR501人体红外感应模块驱动程序
 * 功能：检测人体移动，输出高/低电平信号
 * 作者：黄光全
 * 日期：2025-12-26
 * 
 * 模块特点：
 * 1. 基于热释电红外原理，检测人体移动
 * 2. 检测距离：3-7米（可调）
 * 3. 检测角度：<120°
 * 4. 输出信号：高电平（有人），低电平（无人）
 * 5. 延时时间：可调（默认约2.5-200秒）
 * 
 * 引脚说明：
 * 模块三个引脚：
 *   VCC: 5V电源
 *   OUT: 数字信号输出（默认PA5）
 *   GND: 地线
 * 
 * 工作原理：
 * 当检测到人体移动时，OUT引脚输出高电平
 * 高电平持续时间由延时电位器调节
 * 模块有约2秒上电初始化时间
 */

#include "hgq_hcsr501.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"

// ===== 默认引脚配置：PA5 =====
#define PIR_GPIO_RCC   RCC_AHB1Periph_GPIOA  /* GPIOA时钟 */
#define PIR_GPIO_PORT  GPIOA                 /* GPIOA端口 */
#define PIR_PIN        GPIO_Pin_5            /* PA5引脚 */

/**
 * @brief HCSR501模块初始化
 * @note 配置GPIO为输入模式，下拉电阻
 *       模块上电后需要约2秒初始化时间
 * @retval None
 */
void HGQ_HCSR501_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    
    /* 开启GPIOA时钟 */
    RCC_AHB1PeriphClockCmd(PIR_GPIO_RCC, ENABLE);
    
    /* 配置GPIO参数 */
    GPIO_InitStructure.GPIO_Pin   = PIR_PIN;          /* PA5引脚 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;     /* 输入模式 */
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;   /* 下拉电阻：默认低电平，检测到人变高电平 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; /* 高速（对输入模式影响不大）*/
    GPIO_Init(PIR_GPIO_PORT, &GPIO_InitStructure);
    
    /* 注意：HCSR501上电后需要约2秒初始化时间才能正常工作 */
}

/**
 * @brief 读取HCSR501状态
 * @note 返回当前检测状态
 *       1 = 检测到人体移动（高电平）
 *       0 = 未检测到人体（低电平）
 * @retval uint8_t 检测状态
 */
uint8_t HGQ_HCSR501_Read(void)
{
    /* 读取GPIO引脚状态
     * Bit_SET = 高电平 = 有人
     * Bit_RESET = 低电平 = 无人
     */
    return (GPIO_ReadInputDataBit(PIR_GPIO_PORT, PIR_PIN) == Bit_SET) ? 1 : 0;
}
