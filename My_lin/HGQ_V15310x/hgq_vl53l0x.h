#ifndef __HGQ_VL53L0X_H
#define __HGQ_VL53L0X_H

#include "stm32f4xx.h"
#include "delay.h"

/* ========= 引脚：PB10=SCL, PB11=SDA（独立软件I2C，不复用你的其它I2C） ========= */
#define HGQ_VL53L0X_I2C_RCC   RCC_AHB1Periph_GPIOB
#define HGQ_VL53L0X_I2C_PORT  GPIOB
#define HGQ_VL53L0X_SCL_PIN   GPIO_Pin_10
#define HGQ_VL53L0X_SDA_PIN   GPIO_Pin_11

#define HGQ_VL53L0X_ADDR      0x29   /* 7-bit address */

typedef enum
{
    HGQ_VL53L0X_OK = 0,
    HGQ_VL53L0X_ERR = 1,
    HGQ_VL53L0X_TIMEOUT = 2,
    HGQ_VL53L0X_TOO_CLOSE = 3
} HGQ_VL53L0X_Status;

typedef struct
{
    uint8_t  addr;
    uint16_t io_timeout_ms;

    /* 标定：corr = k*raw + b */
    float    cal_k;
    float    cal_b;

    /* 滤波配置：采样点数n、去掉两端trim个点再平均 */
    uint8_t  filter_n;          /* 建议 5 或 7 */
    uint8_t  filter_trim;       /* 建议 1 */
    uint8_t  sample_delay_ms;   /* 每次采样间隔，建议 15~25ms */

    /* 太近阈值（mm） */
    uint16_t min_valid_mm;

    /* 内部变量 */
    uint8_t  stop_variable;
} HGQ_VL53L0X_Handle;

/* ========= API ========= */
void HGQ_VL53L0X_I2C_Init(void);

HGQ_VL53L0X_Status HGQ_VL53L0X_Begin(HGQ_VL53L0X_Handle *dev, uint8_t addr_7bit);

/* 设置标定参数：corr = k*raw + b */
void HGQ_VL53L0X_SetCalib(HGQ_VL53L0X_Handle *dev, float k, float b);

/* 设置滤波参数：n=采样点数(建议>=3)，trim=两端剔除点数(建议1)，delay=采样间隔ms */
void HGQ_VL53L0X_SetFilter(HGQ_VL53L0X_Handle *dev, uint8_t n, uint8_t trim, uint8_t sample_delay_ms);

/* 读取：输出校正后的距离(mm)，内部已做滤波+标定+太近判定 */
HGQ_VL53L0X_Status HGQ_VL53L0X_ReadMm(HGQ_VL53L0X_Handle *dev, uint16_t *mm_corr);

/* 读取：同时返回 raw/corr，便于你调试 */
HGQ_VL53L0X_Status HGQ_VL53L0X_ReadMmEx(HGQ_VL53L0X_Handle *dev, uint16_t *mm_raw, uint16_t *mm_corr);

/* 调试：读 ModelID（一般 0xEE） */
HGQ_VL53L0X_Status HGQ_VL53L0X_ReadModelID(HGQ_VL53L0X_Handle *dev, uint8_t *model_id);

#endif

