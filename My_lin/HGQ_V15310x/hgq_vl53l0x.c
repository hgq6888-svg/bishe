#include "hgq_vl53l0x.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include <string.h>

/* ================= 软件I2C（独立） ================= */
#define I2C_DELAY_US  2

static inline void SCL_H(void){ GPIO_SetBits(HGQ_VL53L0X_I2C_PORT, HGQ_VL53L0X_SCL_PIN); }
static inline void SCL_L(void){ GPIO_ResetBits(HGQ_VL53L0X_I2C_PORT, HGQ_VL53L0X_SCL_PIN); }
static inline void SDA_H(void){ GPIO_SetBits(HGQ_VL53L0X_I2C_PORT, HGQ_VL53L0X_SDA_PIN); }
static inline void SDA_L(void){ GPIO_ResetBits(HGQ_VL53L0X_I2C_PORT, HGQ_VL53L0X_SDA_PIN); }
static inline uint8_t SDA_READ(void){ return (GPIO_ReadInputDataBit(HGQ_VL53L0X_I2C_PORT, HGQ_VL53L0X_SDA_PIN)==Bit_SET)?1:0; }

static void SDA_OUT_OD(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin   = HGQ_VL53L0X_SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(HGQ_VL53L0X_I2C_PORT, &g);
}
static void SDA_IN(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin   = HGQ_VL53L0X_SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_IN;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(HGQ_VL53L0X_I2C_PORT, &g);
}

void HGQ_VL53L0X_I2C_Init(void)
{
    GPIO_InitTypeDef g;
    RCC_AHB1PeriphClockCmd(HGQ_VL53L0X_I2C_RCC, ENABLE);

    g.GPIO_Pin   = HGQ_VL53L0X_SCL_PIN | HGQ_VL53L0X_SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_OUT;
    g.GPIO_OType = GPIO_OType_OD;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(HGQ_VL53L0X_I2C_PORT, &g);

    SCL_H(); SDA_H();
    delay_us(10);
}

static void I2C_Start(void)
{
    SDA_OUT_OD();
    SDA_H(); SCL_H(); delay_us(I2C_DELAY_US);
    SDA_L();          delay_us(I2C_DELAY_US);
    SCL_L();          delay_us(I2C_DELAY_US);
}
static void I2C_Stop(void)
{
    SDA_OUT_OD();
    SCL_L(); SDA_L(); delay_us(I2C_DELAY_US);
    SCL_H();          delay_us(I2C_DELAY_US);
    SDA_H();          delay_us(I2C_DELAY_US);
}
static void I2C_SendByte(uint8_t b)
{
    SDA_OUT_OD();
    for(int i=0;i<8;i++)
    {
        (b & 0x80) ? SDA_H() : SDA_L();
        delay_us(I2C_DELAY_US);
        SCL_H(); delay_us(I2C_DELAY_US);
        SCL_L(); delay_us(I2C_DELAY_US);
        b <<= 1;
    }
}
static uint8_t I2C_WaitAck(uint16_t timeout_us)
{
    uint16_t t=0;
    SDA_IN();
    SDA_H();
    delay_us(I2C_DELAY_US);
    SCL_H(); delay_us(I2C_DELAY_US);

    while(SDA_READ())
    {
        if(t++ >= timeout_us){ SCL_L(); return 0; }
        delay_us(1);
    }
    SCL_L();
    return 1;
}
static void I2C_Ack(void)
{
    SDA_OUT_OD();
    SDA_L(); delay_us(I2C_DELAY_US);
    SCL_H(); delay_us(I2C_DELAY_US);
    SCL_L(); delay_us(I2C_DELAY_US);
    SDA_H();
}
static void I2C_NAck(void)
{
    SDA_OUT_OD();
    SDA_H(); delay_us(I2C_DELAY_US);
    SCL_H(); delay_us(I2C_DELAY_US);
    SCL_L(); delay_us(I2C_DELAY_US);
}
static uint8_t I2C_ReadByte(uint8_t ack)
{
    uint8_t data=0;
    SDA_IN(); SDA_H();
    for(int i=0;i<8;i++)
    {
        data <<= 1;
        SCL_H(); delay_us(I2C_DELAY_US);
        if(SDA_READ()) data |= 1;
        SCL_L(); delay_us(I2C_DELAY_US);
    }
    ack ? I2C_Ack() : I2C_NAck();
    return data;
}

/* ================= 寄存器读写 ================= */
static HGQ_VL53L0X_Status wr8(HGQ_VL53L0X_Handle *d, uint8_t reg, uint8_t val)
{
    I2C_Start();
    I2C_SendByte((d->addr<<1)|0); if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    I2C_SendByte(reg);           if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    I2C_SendByte(val);           if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    I2C_Stop();
    return HGQ_VL53L0X_OK;
}
static HGQ_VL53L0X_Status rd8(HGQ_VL53L0X_Handle *d, uint8_t reg, uint8_t *val)
{
    I2C_Start();
    I2C_SendByte((d->addr<<1)|0); if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    I2C_SendByte(reg);           if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }

    I2C_Start();
    I2C_SendByte((d->addr<<1)|1); if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    *val = I2C_ReadByte(0);
    I2C_Stop();
    return HGQ_VL53L0X_OK;
}
static HGQ_VL53L0X_Status rd16(HGQ_VL53L0X_Handle *d, uint8_t reg, uint16_t *val)
{
    uint8_t hi, lo;
    I2C_Start();
    I2C_SendByte((d->addr<<1)|0); if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    I2C_SendByte(reg);           if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }

    I2C_Start();
    I2C_SendByte((d->addr<<1)|1); if(!I2C_WaitAck(2000)) { I2C_Stop(); return HGQ_VL53L0X_ERR; }
    hi = I2C_ReadByte(1);
    lo = I2C_ReadByte(0);
    I2C_Stop();

    *val = ((uint16_t)hi<<8) | lo;
    return HGQ_VL53L0X_OK;
}

static HGQ_VL53L0X_Status wait_reg_bit(HGQ_VL53L0X_Handle *d, uint8_t reg, uint8_t mask, uint8_t want1, uint16_t timeout_ms)
{
    uint8_t v=0;
    for(uint16_t t=0;t<timeout_ms;t++)
    {
        if(rd8(d, reg, &v)!=HGQ_VL53L0X_OK) return HGQ_VL53L0X_ERR;
        if( ((v & mask)?1:0) == want1 ) return HGQ_VL53L0X_OK;
        delay_ms(1);
    }
    return HGQ_VL53L0X_TIMEOUT;
}

HGQ_VL53L0X_Status HGQ_VL53L0X_ReadModelID(HGQ_VL53L0X_Handle *dev, uint8_t *model_id)
{
    if(!dev || !model_id) return HGQ_VL53L0X_ERR;
    return rd8(dev, 0xC0, model_id);
}

/* ====== tuning settings（常用公开实现）====== */
static void load_tuning(HGQ_VL53L0X_Handle *d)
{
    wr8(d, 0xFF, 0x01); wr8(d, 0x00, 0x00);

    wr8(d, 0xFF, 0x00); wr8(d, 0x09, 0x00); wr8(d, 0x10, 0x00); wr8(d, 0x11, 0x00);

    wr8(d, 0x24, 0x01); wr8(d, 0x25, 0xFF); wr8(d, 0x75, 0x00);

    wr8(d, 0xFF, 0x01); wr8(d, 0x4E, 0x2C); wr8(d, 0x48, 0x00); wr8(d, 0x30, 0x20);

    wr8(d, 0xFF, 0x00); wr8(d, 0x30, 0x09); wr8(d, 0x54, 0x00); wr8(d, 0x31, 0x04);
    wr8(d, 0x32, 0x03); wr8(d, 0x40, 0x83); wr8(d, 0x46, 0x25); wr8(d, 0x60, 0x00);
    wr8(d, 0x27, 0x00); wr8(d, 0x50, 0x06); wr8(d, 0x51, 0x00); wr8(d, 0x52, 0x96);
    wr8(d, 0x56, 0x08); wr8(d, 0x57, 0x30); wr8(d, 0x61, 0x00); wr8(d, 0x62, 0x00);
    wr8(d, 0x64, 0x00); wr8(d, 0x65, 0x00); wr8(d, 0x66, 0xA0);

    wr8(d, 0xFF, 0x01); wr8(d, 0x22, 0x32); wr8(d, 0x47, 0x14); wr8(d, 0x49, 0xFF);
    wr8(d, 0x4A, 0x00);

    wr8(d, 0xFF, 0x00); wr8(d, 0x7A, 0x0A); wr8(d, 0x7B, 0x00); wr8(d, 0x78, 0x21);

    wr8(d, 0xFF, 0x01); wr8(d, 0x23, 0x34); wr8(d, 0x42, 0x00); wr8(d, 0x44, 0xFF);
    wr8(d, 0x45, 0x26); wr8(d, 0x46, 0x05); wr8(d, 0x40, 0x40); wr8(d, 0x0E, 0x06);
    wr8(d, 0x20, 0x1A); wr8(d, 0x43, 0x40);

    wr8(d, 0xFF, 0x00); wr8(d, 0x34, 0x03); wr8(d, 0x35, 0x44);

    wr8(d, 0xFF, 0x01); wr8(d, 0x31, 0x04); wr8(d, 0x4B, 0x09); wr8(d, 0x4C, 0x05);
    wr8(d, 0x4D, 0x04);

    wr8(d, 0xFF, 0x00); wr8(d, 0x44, 0x00); wr8(d, 0x45, 0x20); wr8(d, 0x47, 0x08);
    wr8(d, 0x48, 0x28); wr8(d, 0x67, 0x00); wr8(d, 0x70, 0x04); wr8(d, 0x71, 0x01);
    wr8(d, 0x72, 0xFE); wr8(d, 0x76, 0x00); wr8(d, 0x77, 0x00);

    wr8(d, 0xFF, 0x01); wr8(d, 0x0D, 0x01);

    wr8(d, 0xFF, 0x00); wr8(d, 0x80, 0x01); wr8(d, 0x01, 0xF8);

    wr8(d, 0xFF, 0x01); wr8(d, 0x8E, 0x01); wr8(d, 0x00, 0x01); wr8(d, 0xFF, 0x00);
    wr8(d, 0x80, 0x00);
}

/* 参考校准 */
static HGQ_VL53L0X_Status do_ref_cal(HGQ_VL53L0X_Handle *d)
{
    wr8(d, 0x01, 0x01);   // VHV
    wr8(d, 0x00, 0x01);
    if(wait_reg_bit(d, 0x13, 0x07, 1, d->io_timeout_ms) != HGQ_VL53L0X_OK) return HGQ_VL53L0X_TIMEOUT;
    wr8(d, 0x0B, 0x01);
    wr8(d, 0x00, 0x00);

    wr8(d, 0x01, 0x02);   // PHASE
    wr8(d, 0x00, 0x01);
    if(wait_reg_bit(d, 0x13, 0x07, 1, d->io_timeout_ms) != HGQ_VL53L0X_OK) return HGQ_VL53L0X_TIMEOUT;
    wr8(d, 0x0B, 0x01);
    wr8(d, 0x00, 0x00);

    wr8(d, 0x01, 0xE8);   // restore
    return HGQ_VL53L0X_OK;
}

/* ====== 单次读取 raw(mm) ====== */
static HGQ_VL53L0X_Status read_raw_mm_once(HGQ_VL53L0X_Handle *dev, uint16_t *mm)
{
    if(!dev || !mm) return HGQ_VL53L0X_ERR;

    /* stop_variable restore */
    wr8(dev, 0x80, 0x01);
    wr8(dev, 0xFF, 0x01);
    wr8(dev, 0x00, 0x00);
    wr8(dev, 0x91, dev->stop_variable);
    wr8(dev, 0x00, 0x01);
    wr8(dev, 0xFF, 0x00);
    wr8(dev, 0x80, 0x00);

    /* start */
    wr8(dev, 0x00, 0x01);

    /* wait ready: RESULT_INTERRUPT_STATUS(0x13) bit[2:0] != 0 */
    if(wait_reg_bit(dev, 0x13, 0x07, 1, dev->io_timeout_ms) != HGQ_VL53L0X_OK)
        return HGQ_VL53L0X_TIMEOUT;

    if(rd16(dev, 0x1E, mm) != HGQ_VL53L0X_OK)
        return HGQ_VL53L0X_ERR;

    wr8(dev, 0x0B, 0x01); /* clear */
    return HGQ_VL53L0X_OK;
}

/* ====== 小工具：排序（滤波用）====== */
static void sort_u16(uint16_t *a, int n)
{
    for(int i=0;i<n-1;i++)
        for(int j=0;j<n-1-i;j++)
            if(a[j] > a[j+1]) { uint16_t t=a[j]; a[j]=a[j+1]; a[j+1]=t; }
}

/* ====== 对 raw 做滤波，输出 raw_filtered ====== */
static HGQ_VL53L0X_Status read_raw_filtered(HGQ_VL53L0X_Handle *dev, uint16_t *raw_out)
{
    uint16_t buf[9]; /* 最多支持9点，够用了 */
    int ok = 0;

    uint8_t n = dev->filter_n;
    if(n < 1) n = 1;
    if(n > 9) n = 9;

    for(uint8_t i=0;i<n;i++)
    {
        uint16_t mm=0;
        HGQ_VL53L0X_Status st = read_raw_mm_once(dev, &mm);
        if(st == HGQ_VL53L0X_OK)
            buf[ok++] = mm;

        if(dev->sample_delay_ms) delay_ms(dev->sample_delay_ms);
    }

    if(ok == 0) return HGQ_VL53L0X_TIMEOUT;

    sort_u16(buf, ok);

    uint8_t trim = dev->filter_trim;
    if(ok < 3) trim = 0;
    if(trim*2 >= ok) trim = 0;

    uint32_t sum = 0;
    uint8_t cnt = 0;
    for(int i=trim;i<ok-trim;i++) { sum += buf[i]; cnt++; }

    *raw_out = (cnt ? (uint16_t)(sum/cnt) : buf[ok/2]);
    return HGQ_VL53L0X_OK;
}

/* ====== 对 raw 做线性标定 -> corr ====== */
static uint16_t apply_calib(HGQ_VL53L0X_Handle *dev, uint16_t raw)
{
    float corr = dev->cal_k * (float)raw + dev->cal_b;
    if(corr < 0.0f) corr = 0.0f;
    if(corr > 2000.0f) corr = 2000.0f;
    return (uint16_t)(corr + 0.5f);
}

/* ================= 对外 API ================= */
HGQ_VL53L0X_Status HGQ_VL53L0X_Begin(HGQ_VL53L0X_Handle *dev, uint8_t addr_7bit)
{
    if(!dev) return HGQ_VL53L0X_ERR;

    memset(dev, 0, sizeof(*dev));
    dev->addr = addr_7bit ? addr_7bit : HGQ_VL53L0X_ADDR;
    dev->io_timeout_ms = 300;

    /* 默认滤波+标定参数（你用起来 main 就很短） */
    dev->cal_k = 1.0f;
    dev->cal_b = 0.0f;
    dev->filter_n = 5;
    dev->filter_trim = 1;
    dev->sample_delay_ms = 20;
    dev->min_valid_mm = 30;

    /* 建议：在 main 里先调用一次 HGQ_VL53L0X_I2C_Init()，这里不强制重复 */
    wr8(dev, 0x88, 0x00);

    /* stop_variable */
    wr8(dev, 0x80, 0x01);
    wr8(dev, 0xFF, 0x01);
    wr8(dev, 0x00, 0x00);
    rd8(dev, 0x91, &dev->stop_variable);
    wr8(dev, 0x00, 0x01);
    wr8(dev, 0xFF, 0x00);
    wr8(dev, 0x80, 0x00);

    /* 2V8 */
    uint8_t v=0;
    if(rd8(dev, 0x89, &v)==HGQ_VL53L0X_OK) wr8(dev, 0x89, v | 0x01);

    /* interrupt config */
    wr8(dev, 0x0A, 0x04);
    if(rd8(dev, 0x84, &v)==HGQ_VL53L0X_OK) wr8(dev, 0x84, v & ~0x10);
    wr8(dev, 0x0B, 0x01);

    load_tuning(dev);
    wr8(dev, 0x01, 0xE8);

    if(do_ref_cal(dev) != HGQ_VL53L0X_OK) return HGQ_VL53L0X_TIMEOUT;

    return HGQ_VL53L0X_OK;
}

void HGQ_VL53L0X_SetCalib(HGQ_VL53L0X_Handle *dev, float k, float b)
{
    if(!dev) return;
    dev->cal_k = k;
    dev->cal_b = b;
}

void HGQ_VL53L0X_SetFilter(HGQ_VL53L0X_Handle *dev, uint8_t n, uint8_t trim, uint8_t sample_delay_ms)
{
    if(!dev) return;
    dev->filter_n = n;
    dev->filter_trim = trim;
    dev->sample_delay_ms = sample_delay_ms;
}

HGQ_VL53L0X_Status HGQ_VL53L0X_ReadMmEx(HGQ_VL53L0X_Handle *dev, uint16_t *mm_raw, uint16_t *mm_corr)
{
    if(!dev || !mm_corr) return HGQ_VL53L0X_ERR;

    uint16_t raw=0;
    HGQ_VL53L0X_Status st = read_raw_filtered(dev, &raw);
    if(st != HGQ_VL53L0X_OK) return st;

    if(mm_raw) *mm_raw = raw;

    if(raw < dev->min_valid_mm) return HGQ_VL53L0X_TOO_CLOSE;

    *mm_corr = apply_calib(dev, raw);
    return HGQ_VL53L0X_OK;
}

HGQ_VL53L0X_Status HGQ_VL53L0X_ReadMm(HGQ_VL53L0X_Handle *dev, uint16_t *mm_corr)
{
    return HGQ_VL53L0X_ReadMmEx(dev, 0, mm_corr);
}

