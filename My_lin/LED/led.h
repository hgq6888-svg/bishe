#ifndef __LED_H
#define __LED_H
#include "sys.h"

/* * LED0 = PF7 (TIM11_CH1, 接5V上拉, 低电平亮)
 * LED1 = PF10 (普通GPIO, 低电平亮)
 */
#define LED0 PFout(7) 
#define LED1 PFout(10)

void LED_Init(void);
void LED0_PWM_Init(void);
void LED0_SetOn(u8 on);
void LED0_SetBrightness(u8 percent);

#endif
