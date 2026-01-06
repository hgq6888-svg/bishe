#include "led.h"

/* ---------------- PWM 参数 ----------------
   TIM11 挂载在 APB2 总线上 (168MHz)
   设 PSC=167 -> 168MHz/(167+1)=1MHz (计数频率)
   设 ARR=999 -> 1MHz/(999+1)=1kHz (PWM频率)
*/
#define LED0_PWM_PSC   (167)
#define LED0_PWM_ARR   (999)

static u8  s_led0_on = 0;        /* 0关 1开 */
static u8  s_led0_bri = 0;       /* 0~100 */

/* 计算 CCR（占空比） */
static void LED0_UpdateCCR(void)
{
    u32 ccr;

    if (s_led0_on == 0 || s_led0_bri == 0)
    {
        ccr = 0; /* CCR=0 => 一直是非有效电平 */
    }
    else
    {
        /* percent -> 0~ARR */
        ccr = ((u32)s_led0_bri * LED0_PWM_ARR) / 100;
        if (ccr > LED0_PWM_ARR) ccr = LED0_PWM_ARR;
    }

    /* 使用 TIM11 */
    TIM_SetCompare1(TIM11, (u16)ccr);
}

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

    /* PF10 仍然做普通输出（LED1） */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOF, &GPIO_InitStructure);

    GPIO_SetBits(GPIOF, GPIO_Pin_10); 

    /* PF7 的 PWM 初始化 */
    LED0_PWM_Init();
}

/* PF7 -> TIM11_CH1 PWM 初始化 (接5V上拉，必须用开漏OD) */
void LED0_PWM_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM11, ENABLE); // TIM11 在 APB2

    /* PF7 复用为 TIM11_CH1 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;        // 【关键】开漏输出
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;         
    GPIO_Init(GPIOF, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOF, GPIO_PinSource7, GPIO_AF_TIM11);

    /* 定时器配置 */
    TIM_TimeBaseStructure.TIM_Prescaler     = LED0_PWM_PSC;
    TIM_TimeBaseStructure.TIM_Period        = LED0_PWM_ARR;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM11, &TIM_TimeBaseStructure);

    /* PWM1 模式 + 低极性（低电平为有效=点亮） */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0;                 
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_Low; // 低电平有效
    TIM_OC1Init(TIM11, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM11, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM11, ENABLE);
    TIM_Cmd(TIM11, ENABLE);

    /* 默认关灯 */
    s_led0_on  = 0;
    s_led0_bri = 0;
    LED0_UpdateCCR();
}

void LED0_SetOn(u8 on)
{
    s_led0_on = on ? 1 : 0;
    LED0_UpdateCCR();
}

void LED0_SetBrightness(u8 percent)
{
    if (percent > 100) percent = 100;
    s_led0_bri = percent;
    LED0_UpdateCCR();
}
