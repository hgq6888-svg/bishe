#ifndef __HGQ_HCSR501_H
#define __HGQ_HCSR501_H

#include "stm32f4xx.h"

/*
 * HCSR501人体红外感应模块头文件
 * 
 * 功能概述：
 * 1. 提供简单的人体移动检测接口
 * 2. 返回二进制状态：有人/无人
 * 3. 默认使用PA5引脚，可修改为其他IO
 * 
 * 典型应用：
 * 1. 自动照明控制
 * 2. 安防监控系统
 * 3. 智能家居感应
 * 4. 节能控制系统
 * 
 * 使用注意事项：
 * 1. 避免阳光直射和热源干扰
 * 2. 安装高度建议2-3米
 * 3. 感应角度有盲区，需合理布置
 * 4. 输出有延时，不适合快速重复检测
 */

/**
 * @brief HCSR501模块初始化
 * @note 配置GPIO为输入模式，下拉电阻
 *       默认使用PA5引脚，可修改hgq_hcsr501.c中的定义
 * @retval None
 */
void    HGQ_HCSR501_Init(void);

/**
 * @brief 读取HCSR501检测状态
 * @note  返回当前人体检测状态
 * @retval 1: 检测到人体移动（有人）
 *         0: 未检测到人体（无人）
 */
uint8_t HGQ_HCSR501_Read(void);   // 1=有人 0=无人

#endif /* __HGQ_HCSR501_H */
