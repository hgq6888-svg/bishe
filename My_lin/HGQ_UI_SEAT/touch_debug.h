#ifndef __TOUCH_DEBUG_H
#define __TOUCH_DEBUG_H

#include "sys.h"

// 初始化调试页（清屏、画边框、提示文字）
void TP_DebugPage_Init(void);

// 任务函数：循环调用（建议 10~20ms 一次）
// 功能：按下就画点 + 串口打印坐标/原始ADC（如果可取到）+ 显示到屏幕
void TP_DebugPage_Task(void);

#endif

