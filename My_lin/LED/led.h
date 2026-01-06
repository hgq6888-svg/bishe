#ifndef __LED_H
#define __LED_H
#include "sys.h"

/* * LED0  = PF7 (TIM11_CH1 PWM, 调光台灯)
 * RELAY = PF6 (继电器/插座电源, 假设低电平触发导通)
 */

#define LED0  PFout(7) 
#define RELAY PFout(6)  // 将原来的 LED1(PF10) 改为 RELAY(PF6)

void LED_Init(void);
void LED0_PWM_Init(void);
void LED0_SetOn(u8 on);
void LED0_SetBrightness(u8 percent);

/* 新增继电器控制函数 */
void Relay_Set(u8 on);

#endif
