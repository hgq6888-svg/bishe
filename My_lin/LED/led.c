#include "led.h"

/* ---------------- PWM 参数 ----------------
   TIM14 时钟一般为 84MHz（APB1 定时器倍频后）
   设 PSC=83 -> 84MHz/(83+1)=1MHz
   设 ARR=999 -> PWM频率=1MHz/(999+1)=1kHz（肉眼不闪）
*/
#define LED0_PWM_PSC   (83)
#define LED0_PWM_ARR   (999)

static u8  s_led0_on = 0;        /* 0关 1开 */
static u8  s_led0_bri = 0;       /* 0~100 */

/* 计算 CCR（占空比），PF9是“低亮高灭”，所以用低极性输出 */
static void LED0_UpdateCCR(void)
{
    u32 ccr;

    if (s_led0_on == 0 || s_led0_bri == 0)
    {
        ccr = 0; /* CCR=0 => 一直是非有效电平 => LED灭 */
    }
    else
    {
        /* percent -> 0~ARR */
        ccr = ((u32)s_led0_bri * LED0_PWM_ARR) / 100;
        if (ccr > LED0_PWM_ARR) ccr = LED0_PWM_ARR;
    }

    TIM_SetCompare1(TIM14, (u16)ccr);
}

/* 原来的初始化：PF10 仍然 GPIO；PF9 将交给 PWM 初始化 */
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

    GPIO_SetBits(GPIOF, GPIO_Pin_10); /* 高电平=灭（原子板LED低亮） */

    /* PF9 的 PWM 初始化 */
    LED0_PWM_Init();
}

/* PF9 -> TIM14_CH1 PWM 初始化 */
void LED0_PWM_Init(void)
{
    GPIO_InitTypeDef        GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef       TIM_OCInitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM14, ENABLE);

    /* PF9 复用为 TIM14_CH1 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOF, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOF, GPIO_PinSource9, GPIO_AF_TIM14);

    /* 定时器：1kHz PWM */
    TIM_TimeBaseStructure.TIM_Prescaler     = LED0_PWM_PSC;
    TIM_TimeBaseStructure.TIM_Period        = LED0_PWM_ARR;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM14, &TIM_TimeBaseStructure);

    /* PWM1 模式 + 低极性（低电平为有效=点亮LED） */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 0;                 /* 默认0% */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_Low;/* 关键：低有效 */
    TIM_OC1Init(TIM14, &TIM_OCInitStructure);

    TIM_OC1PreloadConfig(TIM14, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM14, ENABLE);
    TIM_Cmd(TIM14, ENABLE);

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

