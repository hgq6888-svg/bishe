/******************************************************************************
 * @file     hgq_usart.c
 * @brief    STM32F407x 串口驱动实现 - 黄光全(HGQ)
 * @author   黄光全 (Huang Guangquan)
 * @date     2025-12-26
 * @version  v1.0.0
 * @note     文件名前缀说明：HGQ_ 表示由黄光全开发维护
 *           本模块实现USART1/USART2的初始化和数据收发功能
 *           USART1: 调试串口(PA9-TX, PA10-RX) - 连接PC进行调试
 *           USART2: ESP8266通信串口(PA2-TX, PA3-RX) - 连接WiFi模块
 * 
 * 功能特点：
 * 1. 支持查询方式收发，代码简单可靠
 * 2. USART1用于调试，USART2用于外部模块通信
 * 3. 支持字符串收发和带超时的接收功能
 * 4. 完整的错误处理和调试输出
 * 
 * 硬件连接：
 * USART1: PA9(TX) -> USB转串口的RX
 *         PA10(RX) -> USB转串口的TX
 *         波特率通常为115200
 * 
 * USART2: PA2(TX) -> ESP8266的RX
 *         PA3(RX) -> ESP8266的TX
 *         波特率通常为115200
 * 
 * @attention
 * 个人项目文件 - 仅供学习使用
 * 作者：黄光全
 * 创建时间：2025年12月26日
 ******************************************************************************/

#include "hgq_usart.h"
#include "delay.h"


#include "stm32f4xx.h"
#include <stdio.h>

//#pragma import(__use_no_semihosting)  // 禁用半主机

//struct __FILE { int handle; };
//FILE __stdout;

//void _sys_exit(int x) { (void)x; }    // 需要这个避免链接到半主机退出

//static inline int usart_enabled(USART_TypeDef *u)
//{
//    return (u->CR1 & USART_CR1_UE) ? 1 : 0;
//}

//int fputc(int ch, FILE *f)
//{
//    (void)f;

//    if (usart_enabled(USART1)) {
//        while ((USART1->SR & USART_SR_TXE) == 0);
//        USART1->DR = (uint8_t)ch;
//    } else if (usart_enabled(USART2)) {
//        while ((USART2->SR & USART_SR_TXE) == 0);
//        USART2->DR = (uint8_t)ch;
//    }
//    return ch;
//}


/*-------------------------------------
 *            USART1初始化
 *-------------------------------------*/

/**
 * @brief   USART1串口初始化函数（调试串口）
 * @param   bound: 波特率（如115200、9600等）
 * @note    引脚配置：PA9=TX（发送），PA10=RX（接收）
 *          应用场景：连接PC进行调试通信
 *          时钟总线：USART1挂载在APB2总线
 *          初始化步骤：
 *          1. 开启时钟
 *          2. 配置引脚复用
 *          3. 配置GPIO参数
 *          4. 配置USART参数
 *          5. 使能USART
 * @retval  None
 */
void HGQ_USART1_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;    /**< GPIO初始化结构体 */
    USART_InitTypeDef USART_InitStructure;  /**< USART初始化结构体 */
    
    /* 步骤1：开启相关外设时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);      /**< 使能GPIOA时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);     /**< 使能USART1时钟（APB2总线）*/
    
    /* 步骤2：配置引脚复用功能 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);   /**< PA9复用为USART1_TX */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1);  /**< PA10复用为USART1_RX */
    
    /* 步骤3：配置GPIO参数 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;    /**< 操作PA9和PA10引脚 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;               /**< 复用功能模式 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;          /**< 输出速度50MHz */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;             /**< 推挽输出 */
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;               /**< 上拉模式 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);                     /**< 初始化GPIO */
    
    /* 步骤4：配置USART1通信参数 */
    USART_InitStructure.USART_BaudRate = bound;                 /**< 设置波特率 */
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; /**< 8位数据长度 */
    USART_InitStructure.USART_StopBits = USART_StopBits_1;      /**< 1位停止位 */
    USART_InitStructure.USART_Parity = USART_Parity_No;         /**< 无奇偶校验 */
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; /**< 收发模式 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; /**< 无硬件流控 */
    
    /* 步骤5：初始化并使能USART1 */
    USART_Init(USART1, &USART_InitStructure);    /**< 初始化USART1 */
    USART_Cmd(USART1, ENABLE);                   /**< 使能USART1 */
}

/*-------------------------------------
 *            USART2初始化
 *-------------------------------------*/

/**
 * @brief   USART2串口初始化函数（外设串口）
 * @param   bound: 波特率（通常使用115200）
 * @note    引脚配置：PA2=TX（发送），PA3=RX（接收）
 *          应用场景：连接ESP8266等外部通信模块
 *          时钟总线：USART2挂载在APB1总线
 * @retval  None
 */
void HGQ_USART2_Init(uint32_t bound)
{
    GPIO_InitTypeDef GPIO_InitStructure;    /**< GPIO初始化结构体 */
    USART_InitTypeDef USART_InitStructure;  /**< USART初始化结构体 */
    
    /* 步骤1：开启相关外设时钟 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);      /**< 使能GPIOA时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);     /**< 使能USART2时钟（APB1总线）*/
    
    /* 步骤2：配置引脚复用功能 */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);  /**< PA2复用为USART2_TX */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);  /**< PA3复用为USART2_RX */
    
    /* 步骤3：配置GPIO参数 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;     /**< 操作PA2和PA3引脚 */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;               /**< 复用功能模式 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;          /**< 输出速度50MHz */
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;             /**< 推挽输出 */
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;               /**< 上拉模式 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);                     /**< 初始化GPIO */
    
    /* 步骤4：配置USART2通信参数 */
    USART_InitStructure.USART_BaudRate = bound;                 /**< 设置波特率 */
    USART_InitStructure.USART_WordLength = USART_WordLength_8b; /**< 8位数据长度 */
    USART_InitStructure.USART_StopBits = USART_StopBits_1;      /**< 1位停止位 */
    USART_InitStructure.USART_Parity = USART_Parity_No;         /**< 无奇偶校验 */
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx; /**< 收发模式 */
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; /**< 无硬件流控 */
    
    /* 步骤5：初始化并使能USART2 */
    USART_Init(USART2, &USART_InitStructure);    /**< 初始化USART2 */
    USART_Cmd(USART2, ENABLE);                   /**< 使能USART2 */
}

/*-------------------------------------
 *          USART1数据收发函数
 *-------------------------------------*/

/**
 * @brief   通过USART1发送单个字符
 * @param   ch: 要发送的字符（8位数据）
 * @note    使用查询方式发送，等待发送完成标志
 *          阻塞函数，发送完成前不会返回
 *          使用USART_FLAG_TXE标志判断发送寄存器是否为空
 * @retval  None
 */
void HGQ_USART1_SendChar(uint8_t ch)
{
    USART_SendData(USART1, ch);  /**< 将要发送的数据写入数据寄存器 */
    
    /* 等待发送完成（TXE标志：发送数据寄存器空）*/
    while(USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

/**
 * @brief   通过USART1发送字符串
 * @param   str: 要发送的字符串（以'\0'结尾）
 * @note    循环调用SendChar发送每个字符
 *          自动处理字符串结束符
 * @retval  None
 */
void HGQ_USART1_SendString(char *str)
{
    /* 遍历字符串直到遇到结束符'\0' */
    while(*str)
    {
        HGQ_USART1_SendChar(*str++);  /**< 发送当前字符并移动指针 */
    }
}

/**
 * @brief   通过USART1接收单个字符
 * @param   None
 * @note    使用查询方式接收，等待接收完成标志
 *          阻塞函数，接收到数据前不会返回
 *          使用USART_FLAG_RXNE标志判断接收寄存器是否有数据
 * @retval  接收到的字符（8位数据）
 */
uint8_t HGQ_USART1_ReceiveChar(void)
{
    /* 等待接收完成（RXNE标志：接收数据寄存器非空）*/
    while(USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET);
    
    return (uint8_t)USART_ReceiveData(USART1);  /**< 读取数据寄存器并返回 */
}

/**
 * @brief   通过USART1接收字符串（带超时功能）
 * @param   buf: 接收缓冲区指针
 * @param   len: 缓冲区最大长度
 * @param   timeout: 超时时间（单位：毫秒）
 * @note    接收到指定长度或遇到换行符或超时后停止接收
 *          自动在字符串末尾添加'\0'
 *          包含简单的超时检测机制
 * @retval  实际接收到的字符数
 */
uint16_t HGQ_USART1_ReceiveString(char *buf, uint16_t len, uint32_t timeout)
{
    uint16_t count = 0;
    uint8_t ch;
    uint32_t current_time = 0;
    volatile uint32_t i;
    
    while(count < len - 1)  /* 预留一个位置给字符串结束符 */
    {
        /* 检查是否有数据可接收 */
        if(USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET)
        {
            ch = (uint8_t)USART_ReceiveData(USART1);
            buf[count++] = ch;
            
            /* 如果遇到换行符或回车符，停止接收 */
            if(ch == '\n' || ch == '\r')
            {
                break;
            }
        }
        else
        {
            /* 简单延时（需要根据实际系统时钟调整）*/
            delay_us(1);
            
            /* 检查超时（简化实现）*/
            if(timeout > 0)
            {
                current_time++;
                if(current_time > timeout * 1000)  /* 粗略转换为毫秒 */
                {
                    break;
                }
            }
        }
    }
    
    buf[count] = '\0';  /**< 添加字符串结束符 */
    return count;
}

/**
 * @brief   检查USART1是否有数据可接收
 * @param   None
 * @note    非阻塞函数，立即返回
 *          使用USART_FLAG_RXNE标志判断
 * @retval  1: 有数据可接收，0: 无数据
 */
uint8_t HGQ_USART1_Available(void)
{
    return (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}

/*-------------------------------------
 *          USART2数据收发函数
 *-------------------------------------*/

/**
 * @brief   通过USART2发送单个字符
 * @param   ch: 要发送的字符（8位数据）
 * @note    使用查询方式发送，等待发送完成标志
 *          阻塞函数，发送完成前不会返回
 * @retval  None
 */
void HGQ_USART2_SendChar(uint8_t ch)
{
    USART_SendData(USART2, ch);  /**< 将要发送的数据写入数据寄存器 */
    
    /* 等待发送完成（TXE标志：发送数据寄存器空）*/
    while(USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
}

/**
 * @brief   通过USART2发送字符串
 * @param   str: 要发送的字符串（以'\0'结尾）
 * @note    循环调用SendChar发送每个字符
 *          自动处理字符串结束符
 * @retval  None
 */
void HGQ_USART2_SendString(char *str)
{
    /* 遍历字符串直到遇到结束符'\0' */
    while(*str)
    {
        HGQ_USART2_SendChar(*str++);  /**< 发送当前字符并移动指针 */
    }
}

/**
 * @brief   通过USART2接收单个字符
 * @param   None
 * @note    使用查询方式接收，等待接收完成标志
 *          阻塞函数，接收到数据前不会返回
 * @retval  接收到的字符（8位数据）
 */
uint8_t HGQ_USART2_ReceiveChar(void)
{
    /* 等待接收完成（RXNE标志：接收数据寄存器非空）*/
    while(USART_GetFlagStatus(USART2, USART_FLAG_RXNE) == RESET);
    
    return (uint8_t)USART_ReceiveData(USART2);  /**< 读取数据寄存器并返回 */
}

/**
 * @brief   通过USART2接收字符串（带超时功能 - 改进版）
 * @param   buf: 接收缓冲区指针
 * @param   len: 缓冲区最大长度
 * @param   timeout_ms: 超时时间（单位：毫秒）
 * @note    改进的接收算法：
 *          1. 首次等待完整超时时间
 *          2. 收到数据后，如果2ms内无新数据则认为帧结束
 *          3. 轮询间隔10us，平衡响应速度和CPU占用
 *          适用于ESP8266等模块的AT命令响应
 * @retval  实际接收到的字符数
 */
uint16_t HGQ_USART2_ReceiveString(char *buf, uint16_t len, uint32_t timeout_ms)
{
    uint16_t count = 0;
    uint32_t idle_us = 0;                  /**< 空闲时间计数器 */
    
    const uint32_t poll_us = 10;           /**< 轮询间隔：10微秒 */
    const uint32_t first_timeout_us = timeout_ms * 1000UL; /**< 首次超时：转换为微秒 */
    const uint32_t gap_us = 2000;          /**< 数据间隔超时：2ms无新数据认为结束 */
    
    while (count < (len - 1))              /**< 预留结束符位置 */
    {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            uint8_t ch = (uint8_t)USART_ReceiveData(USART2);
            buf[count++] = ch;
            idle_us = 0;                   /**< 收到数据，重置空闲计时 */
            
            /* 可根据协议添加快速返回条件 */
            /* if (ch == '\n') break; */   /* 如果以换行结尾可启用 */
        }
        else
        {
            delay_us(poll_us);            /**< 轮询等待 */
            idle_us += poll_us;           /**< 累计空闲时间 */
            
            if (count == 0)               /**< 还未收到任何数据 */
            {
                /* 等待首次超时 */
                if (idle_us >= first_timeout_us) 
                    break;
            }
            else                          /**< 已收到部分数据 */
            {
                /* 数据间隔超时，认为一帧结束 */
                if (idle_us >= gap_us) 
                    break;
            }
        }
    }
    
    buf[count] = '\0';                    /**< 添加字符串结束符 */
    return count;
}

/*WiFi指令专属*/
uint16_t HGQ_USART2_ReceiveString_wifi(char *buf, uint16_t len, uint32_t timeout_ms)
{
    uint16_t count = 0;
    uint32_t idle_us = 0;                  /**< 空闲时间计数器 */
    
    const uint32_t poll_us = 10;           /**< 轮询间隔：10微秒 */
    const uint32_t first_timeout_us = timeout_ms * 1000UL; /**< 首次超时：转换为微秒 */
    const uint32_t gap_us = 3000000;          /**< 数据间隔超时：1s无新数据认为结束 */
    
    while (count < (len - 1))              /**< 预留结束符位置 */
    {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET)
        {
            uint8_t ch = (uint8_t)USART_ReceiveData(USART2);
            buf[count++] = ch;
            idle_us = 0;                   /**< 收到数据，重置空闲计时 */
            
            /* 可根据协议添加快速返回条件 */
            /* if (ch == '\n') break; */   /* 如果以换行结尾可启用 */
        }
        else
        {
            delay_us(poll_us);            /**< 轮询等待 */
            idle_us += poll_us;           /**< 累计空闲时间 */
            
            if (count == 0)               /**< 还未收到任何数据 */
            {
                /* 等待首次超时 */
                if (idle_us >= first_timeout_us) 
                    break;
            }
            else                          /**< 已收到部分数据 */
            {
                /* 数据间隔超时，认为一帧结束 */
                if (idle_us >= gap_us) 
                    break;
            }
        }
    }
    
    buf[count] = '\0';                    /**< 添加字符串结束符 */
    return count;
}



/**
 * @brief   检查USART2是否有数据可接收
 * @param   None
 * @note    非阻塞函数，立即返回
 * @retval  1: 有数据可接收，0: 无数据
 */
uint8_t HGQ_USART2_Available(void)
{
    return (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) ? 1 : 0;
}


/*==========================================================================
 *               新增：USART 中断接收（可选）实现
 *==========================================================================
 * 设计思路：
 * 1) RXNE 中断触发 -> 读取 DR（清 RXNE）-> 放入环形缓冲区
 * 2) 主循环通过 HGQ_USARTx_IT_* 系列接口“非阻塞”取数据，避免一直轮询 RXNE
 * 3) 不改变原有查询接收函数，工程里可以自由选择两种方式
 */

typedef struct
{
    volatile uint16_t head;      /**< 写指针：ISR 写入时递增 */
    volatile uint16_t tail;      /**< 读指针：主循环读取时递增 */
    volatile uint8_t  overflow;  /**< 溢出标志：1=发生过溢出（可用于调试） */
} HGQ_RxRingCtrl;

/* USART1 环形缓冲区（数据区 + 控制区） */
static volatile uint8_t     s_usart1_rxbuf[HGQ_USART1_RXBUF_SIZE];
static HGQ_RxRingCtrl       s_usart1_rb = {0, 0, 0};
static HGQ_USART_RxCallback s_usart1_rx_cb = 0;

/* USART2 环形缓冲区（数据区 + 控制区） */
static volatile uint8_t     s_usart2_rxbuf[HGQ_USART2_RXBUF_SIZE];
static HGQ_RxRingCtrl       s_usart2_rb = {0, 0, 0};
static HGQ_USART_RxCallback s_usart2_rx_cb = 0;

static uint16_t HGQ_RB_Count(uint16_t head, uint16_t tail, uint16_t size)
{
    return (head >= tail) ? (head - tail) : (uint16_t)(size - (tail - head));
}

static void HGQ_USART1_RB_Push(uint8_t ch)
{
    uint16_t head = s_usart1_rb.head;
    uint16_t next = (uint16_t)(head + 1U);
    if(next >= HGQ_USART1_RXBUF_SIZE) next = 0;

    if(next == s_usart1_rb.tail)
    {
        s_usart1_rb.overflow = 1;  /* 满了就丢新数据（不覆盖旧数据） */
        return;
    }

    s_usart1_rxbuf[head] = ch;
    s_usart1_rb.head = next;
}

static void HGQ_USART2_RB_Push(uint8_t ch)
{
    uint16_t head = s_usart2_rb.head;
    uint16_t next = (uint16_t)(head + 1U);
    if(next >= HGQ_USART2_RXBUF_SIZE) next = 0;

    if(next == s_usart2_rb.tail)
    {
        s_usart2_rb.overflow = 1;
        return;
    }

    s_usart2_rxbuf[head] = ch;
    s_usart2_rb.head = next;
}

static int HGQ_USART1_RB_Pop(uint8_t *ch)
{
    uint16_t tail = s_usart1_rb.tail;
    if(tail == s_usart1_rb.head) return 0;

    *ch = (uint8_t)s_usart1_rxbuf[tail];
    tail = (uint16_t)(tail + 1U);
    if(tail >= HGQ_USART1_RXBUF_SIZE) tail = 0;

    s_usart1_rb.tail = tail;
    return 1;
}

static int HGQ_USART2_RB_Pop(uint8_t *ch)
{
    uint16_t tail = s_usart2_rb.tail;
    if(tail == s_usart2_rb.head) return 0;

    *ch = (uint8_t)s_usart2_rxbuf[tail];
    tail = (uint16_t)(tail + 1U);
    if(tail >= HGQ_USART2_RXBUF_SIZE) tail = 0;

    s_usart2_rb.tail = tail;
    return 1;
}

/*--------------------------- 对外：中断接收 API ---------------------------*/

void HGQ_USART1_EnableRxIRQ(FunctionalState en)
{
    if(en == ENABLE)
    {
        HGQ_USART1_IT_ClearRxBuffer();
        USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
        NVIC_SetPriority(USART1_IRQn, 5);
        NVIC_EnableIRQ(USART1_IRQn);
    }
    else
    {
        USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
        NVIC_DisableIRQ(USART1_IRQn);
    }
}

void HGQ_USART2_EnableRxIRQ(FunctionalState en)
{
    if(en == ENABLE)
    {
        HGQ_USART2_IT_ClearRxBuffer();
        USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
        NVIC_SetPriority(USART2_IRQn, 5);
        NVIC_EnableIRQ(USART2_IRQn);
    }
    else
    {
        USART_ITConfig(USART2, USART_IT_RXNE, DISABLE);
        NVIC_DisableIRQ(USART2_IRQn);
    }
}

uint16_t HGQ_USART1_IT_AvailableBytes(void)
{
    uint16_t head = s_usart1_rb.head;
    uint16_t tail = s_usart1_rb.tail;
    return HGQ_RB_Count(head, tail, HGQ_USART1_RXBUF_SIZE);
}

uint16_t HGQ_USART2_IT_AvailableBytes(void)
{
    uint16_t head = s_usart2_rb.head;
    uint16_t tail = s_usart2_rb.tail;
    return HGQ_RB_Count(head, tail, HGQ_USART2_RXBUF_SIZE);
}

int HGQ_USART1_IT_GetChar(uint8_t *ch)
{
    if(ch == 0) return 0;
    return HGQ_USART1_RB_Pop(ch);
}

int HGQ_USART2_IT_GetChar(uint8_t *ch)
{
    if(ch == 0) return 0;
    return HGQ_USART2_RB_Pop(ch);
}

uint16_t HGQ_USART1_IT_Read(uint8_t *buf, uint16_t len)
{
    uint16_t n = 0;
    uint8_t ch;
    if(buf == 0 || len == 0) return 0;

    while(n < len)
    {
        if(HGQ_USART1_RB_Pop(&ch) == 0) break; /* 没数据立刻返回 */
        buf[n++] = ch;
    }
    return n;
}

uint16_t HGQ_USART2_IT_Read(uint8_t *buf, uint16_t len)
{
    uint16_t n = 0;
    uint8_t ch;
    if(buf == 0 || len == 0) return 0;

    while(n < len)
    {
        if(HGQ_USART2_RB_Pop(&ch) == 0) break;
        buf[n++] = ch;
    }
    return n;
}

void HGQ_USART1_IT_ClearRxBuffer(void)
{
    s_usart1_rb.head = 0;
    s_usart1_rb.tail = 0;
    s_usart1_rb.overflow = 0;
}

void HGQ_USART2_IT_ClearRxBuffer(void)
{
    s_usart2_rb.head = 0;
    s_usart2_rb.tail = 0;
    s_usart2_rb.overflow = 0;
}

void HGQ_USART1_SetRxCallback(HGQ_USART_RxCallback cb)
{
    s_usart1_rx_cb = cb;
}

void HGQ_USART2_SetRxCallback(HGQ_USART_RxCallback cb)
{
    s_usart2_rx_cb = cb;
}

/*--------------------------- 中断核心处理函数 ---------------------------*/

void HGQ_USART1_RxIRQHandler(void)
{
    if(USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART1);
        HGQ_USART1_RB_Push(ch);
        if(s_usart1_rx_cb) s_usart1_rx_cb(ch);
    }

    if(USART_GetFlagStatus(USART1, USART_FLAG_ORE) != RESET)
    {
        volatile uint32_t tmp;
        tmp = USART1->SR;
        tmp = USART1->DR;
        (void)tmp;
    }
}

void HGQ_USART2_RxIRQHandler(void)
{
    if(USART_GetITStatus(USART2, USART_IT_RXNE) != RESET)
    {
        uint8_t ch = (uint8_t)USART_ReceiveData(USART2);
        HGQ_USART2_RB_Push(ch);
        if(s_usart2_rx_cb) s_usart2_rx_cb(ch);
    }

    if(USART_GetFlagStatus(USART2, USART_FLAG_ORE) != RESET)
    {
        volatile uint32_t tmp;
        tmp = USART2->SR;
        tmp = USART2->DR;
        (void)tmp;
    }
}

/*--------------------------- 可选：驱动提供 IRQHandler ---------------------------*/

//#ifdef HGQ_USART_IRQ_HANDLER_IN_DRIVER
//void USART1_IRQHandler(void)
//{
//    HGQ_USART1_RxIRQHandler();
//}

void USART2_IRQHandler(void)
{
    HGQ_USART2_RxIRQHandler();
}
//#endif










