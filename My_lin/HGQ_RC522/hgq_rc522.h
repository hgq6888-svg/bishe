#ifndef __HGQ_RC522_H
#define __HGQ_RC522_H

#include "stm32f4xx.h"

/*
 * RC522 RFID模块头文件
 * 定义引脚配置和API接口
 * 
 * 引脚配置说明：
 * RC522使用软件SPI通信，引脚可自由配置
 * 默认使用GPIOC端口：
 *   PC2 = SCK（时钟）
 *   PC3 = MOSI（主机输出）
 *   PC4 = MISO（主机输入）
 *   PC5 = NSS（片选）
 *   PC6 = RST（复位）
 * 
 * 注意事项：
 * 1. 需要外接3.3V电源和天线
 * 2. 通信距离通常3-5cm，受天线设计影响
 * 3. 支持ISO14443A标准的卡片
 */

// ====== RC522 软件SPI 引脚配置（可修改）======

#define RC522_GPIO_RCC    RCC_AHB1Periph_GPIOC    /* GPIO时钟：GPIOC */
#define RC522_GPIO_PORT   GPIOC                   /* GPIO端口：GPIOC */

/* RC522引脚定义 */
#define RC522_SCK_PIN     GPIO_Pin_2              /* SPI时钟线：PC2 */
#define RC522_MOSI_PIN    GPIO_Pin_3              /* SPI主机输出：PC3 */
#define RC522_MISO_PIN    GPIO_Pin_4              /* SPI主机输入：PC4 */
#define RC522_NSS_PIN     GPIO_Pin_5              /* SPI片选：PC5 */
#define RC522_RST_PIN     GPIO_Pin_6              /* 复位引脚：PC6 */

// ====== API函数声明 ======

/**
 * @brief RC522模块初始化
 * @note 配置GPIO、复位模块、设置工作参数
 * @retval None
 */
void HGQ_RC522_Init(void);

/**
 * @brief 寻卡并读取UID
 * @param uid_buf: UID输出缓冲区（建议至少10字节）
 * @param uid_len: UID长度输出指针（通常为4）
 * @retval 0: 成功，1: 错误，2: 无卡片
 * @note 此函数完成完整的寻卡流程：寻卡->防冲突->选卡
 *       适合Mifare 1K卡片，UID长度为4字节
 */
uint8_t HGQ_RC522_PollUID(uint8_t *uid_buf, uint8_t *uid_len);

/**
 * @brief 将UID转换为字符串格式
 * @param uid: UID字节数组
 * @param uid_len: UID长度
 * @param out: 输出字符串缓冲区
 * @param out_size: 缓冲区大小（建议>=40字节）
 * @retval None
 * @note 输出格式："RC522 UID: xx xx xx xx" 或 "RC522: NO CARD"
 *       用于调试显示或日志记录
 */
void HGQ_RC522_UIDToString(const uint8_t *uid, uint8_t uid_len, char *out, uint16_t out_size);

#endif /* __HGQ_RC522_H */
