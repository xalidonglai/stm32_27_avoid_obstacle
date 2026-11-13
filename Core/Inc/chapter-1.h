/**
  ******************************************************************************
  * @file    chapter-1.h
  * @brief   模块 chapter-1 的对外接口（第 1 章：1.1～1.8节）
  *          本文件只做“声明”：宏定义、全局变量extern与函数原型等。
  *          实现全部放在 chapter-1.c 中。
  *          其他模块（如 chapter-2）只需#include “chapter-1.h”
  *          即可调用本章已有的全部功能。
  ******************************************************************************
  */

#ifndef __CHAPTER_1_H__
#define __CHAPTER_1_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"      /* HAL 类型、GPIO_PIN_x、USART1_IRQn 等 */
#include <stdbool.h>   /* bool */
#include <stdint.h>    /* uint8_t /int16_t */

/* ===================== 1.2 按键与板载LED的引脚(CubeMX中已配置为GPIO） ===================== */
#define BUTTON_PORT    GPIOA
#define BUTTON_PIN     GPIO_PIN_0      /* 按键：PA0，按下接地（读到低电平） */
#define BOARD_LED_PORT GPIOC
#define BOARD_LED_PIN  GPIO_PIN_13     /* 板载 LED：PC13，低电平点亮 */

/* 1.6节：电机速度限幅（与PWM 的Count Period 一致，即 1000）*/
#define MAX_SPEED 1000

/* 1.7节：USART1 接收环形缓冲区大小*/
#define UART_RX_RING 32

/* Exported constants ------------------------*/
extern const int brightness_step;   /* 1.3节：呼吸灯亮度变化步长 */

/* Exported veriables------------------------*/
/*1.7节: USART1句柄（由CubeMX生成main.c中）与中断当前收到字节*/
extern UART_HandleTypeDef huart1;
extern volatile uint8_t uart_rx_byte;

/*1.8节：协议解析结果（有符号速度量），主循环读取后交给drive（）*/
extern int16_t forward_speed,turn_speed;

/* Exported functions prototypes ---------------------------------------------*/

/* --- 1.3 呼吸灯（PWM + TIM2） --- */
void init_pwm_led(void);

/* --- 1.4 按键与 USB 串口打印 --- */
bool check_button_pressed(void);
int  update_breathing_led(bool breathing);
void print(const char *fmt, ...);

/* --- 1.5/1.6 电机驱动 --- */
void init_motor(void);
void set_motor_speed(int motor_id, int speed);
void drive(int forward_speed, int turn_speed);

/* ---1.7 串口中断环形缓冲 --- */
bool    uart_rx_available(void);
uint8_t uart_rx_read(void);

/* --- 1.8 协议状态机 --- */
void uart_handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __CHAPTER_1_H__ */
