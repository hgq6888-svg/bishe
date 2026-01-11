#include "delay.h"
#include "sys.h"
#include "FreeRTOS.h"
#include "task.h"

static u8  fac_us=0;
static u16 fac_ms=0;

// FreeRTOS 心跳中断钩子，在 FreeRTOSConfig.h 中定义 xPortSysTickHandler
extern void xPortSysTickHandler(void);

// 此时初始化函数只需初始化 fac_us，SysTick 由 FreeRTOS 启动调度器时自动配置
void delay_init(u8 SYSCLK)
{
	u32 reload;
    // SystemCoreClock / 1000000
	fac_us = SYSCLK; 
	reload = SYSCLK; // 
    // FreeRTOS 配置的 Tick 频率通常是 1000Hz (1ms)
	reload *= 1000000 / configTICK_RATE_HZ; 
	
	fac_ms = 1000 / configTICK_RATE_HZ;

	//SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk; 
	SysTick->LOAD = reload;
	SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk; 
}

// 延时 nus
// FreeRTOS 中微秒延时通常不进行任务调度，直接空转
void delay_us(u32 nus)
{
	u32 ticks;
	u32 told,tnow,tcnt=0;
	u32 reload=SysTick->LOAD;
	ticks=nus*fac_us; 
	told=SysTick->VAL;
	while(1)
	{
		tnow=SysTick->VAL;
		if(tnow!=told)
		{
			if(tnow<told)tcnt+=told-tnow;
			else tcnt+=reload-tnow+told;
			told=tnow;
			if(tcnt>=ticks)break;
		}
	};
}

// 延时 nms
// 在 FreeRTOS 中，这会引起任务调度 (阻塞)
void delay_ms(u16 nms)
{
	if(xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
	{
		if(nms >= fac_ms)
		{
			vTaskDelay(nms / fac_ms); // 调用 FreeRTOS 延时
		}
		nms %= fac_ms;
	}
	delay_us((u32)(nms*1000));
}

// 延时 xms (为了兼容旧代码)
void delay_xms(u16 nms)
{
	delay_ms(nms);
}
