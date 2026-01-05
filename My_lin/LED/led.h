#ifndef __LED_H
#define __LED_H
#include "sys.h"

/* 原子板：LED0=PF9（DS0，低电平亮），LED1=PF10（DS1，低电平亮） */
#define LED0 PFout(9)
#define LED1 PFout(10)

void LED_Init(void);

/* ====== 新增：PF9(PWM调光) ====== */
void LED0_PWM_Init(void);                 /* 初始化 TIM14_CH1(PF9) PWM */
void LED0_SetOn(u8 on);                   /* 开关灯：1开 0关 */
void LED0_SetBrightness(u8 percent);      /* 设置亮度：0~100（percent=0等同关灯） */

#endif

