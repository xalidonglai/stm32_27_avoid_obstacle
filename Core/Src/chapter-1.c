//******** STM.C.1.1 ********//

// ▼ CubeMX 需配置（在STM32CubeMX 中打开.ioc工程配置文件）：
// ① 时钟源：左侧 Pinout & Configuration → Categories → System Core → RCC → High Speed Clock (HSE) → 选 “Crystal/Ceramic Resonator”
// ② 调试口：Categories → System Core → SYS → Debug → 选 “Serial Wire”（否则会禁用 SWD，导致之后无法烧录，如果烧录了禁用SWD的版本，需要将boot0连接到1 然后烧录新版本进行覆盖）
// ③ 时钟树：上方 Clock Configuration 标签页 → Input frequency 填 8（MHz，外部晶振）
//    → PLL Source 选 HSE → PLL Mul 选 ×9 → System Clock Mux 选 PLLCLK（HCLK 自动显示 72MHz）
//    → APB1 Prescaler 选 /2（36MHz，APB1 上限 36MHz）→ APB2 Prescaler 选 /1（72MHz）
//    （72MHz 是最常用的主频；TIM2/TIM3 因此在 72MHz 下计数）
// ④ 引脚：芯片图上点击 PC13 → 选 GPIO_Output（推挽输出；本章不填 User Label，代码用原始 PC13）
// ⑤ 点右上角 GENERATE CODE；main() 会自动调用 SystemClock_Config() 与 MX_GPIO_Init() 完成时钟与 PC13 配置

#include "main.h"      /* HAL 类型与 GPIO_PIN_x 引脚编号都来自它 */

//******** STM.C.1.2 ********//

// ▼ CubeMX 需配置（接续第 1 节）：
// ① 引脚：芯片图上点击 PA0 → 选 GPIO_Input
// ② 下方 GPIO 面板 → PA0 的 Pull-up/Pull-down → 选 “Pull-up”（上拉：平时高电平，按下接地为低）
// ③ GENERATE CODE；PA0 初始化也会进 main() 的 MX_GPIO_Init()

#include <stdbool.h>   /* 本节起使用 bool 类型，故在本节开头引入 */
#include "chapter-1.h" /* 本章对外接口声明 */

/* 宏定义：使用 #define 定义一个常量或宏，以便在代码中使用更具描述性的名称来代替具体的值。 */


//******** STM.C.1.3 ********//
// 呼吸的光才有灵魂：PWM + TIM 定时器

//▼ CubeMX 需配置（接续第 2 节）：
// ① 定时器：左侧 Categories → Timers → TIM2 → 在 TIM2 面板 Channel2 下拉 → 选 “PWM Generation CH2”
//    （芯片图 PA1 会自动变绿并复用为 TIM2_CH2）
// ② 参数：TIM2 → Parameter Settings → Prescaler = 71；Counter Period = 1000（Pulse 初始占空比可填 0）
// ③ GENERATE CODE（生成 MX_TIM2_Init 与 htim2）；代码中还需 HAL_TIM_PWM_Start() 真正启动 PWM 输出

/*
PWM配置结构体：通过 pwm_config 结构体配置 PWM 的频率、分频等参数，再初始化到对应的 PWM 切片中。
（STM32 中这些参数已由 CubeMX 的 MX_TIM2_Init() 配好，代码里只需启动 PWM 并改占空比）
*/
/* 本节才用到定时器句柄 → 才声明（句柄本体由 CubeMX 在 main.c 生成） */
extern TIM_HandleTypeDef htim2;

/*
const变量：在函数内定义的 const 变量在程序周期内值不可修改，且只初始化一次。适用于常量值的定义。
全局变量：在函数外部定义的变量，在整个程序中都可以访问和修改。
*/
const int brightness_step = 10;                    // 亮度变化步长

void init_pwm_led(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2);      // 启动 TIM2_CH2 的 PWM 输出（PA1）
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);   // 初始占空比为 0（LED 关闭）
}


//******** STM.C.1.4 ********//

// ▼ CubeMX 需配置（接续第 3 节）——本节的打印走“USB 虚拟串口”：
// ① USB：左侧 Categories → Connectivity → USB → Mode → 选 “Device (FS)”
//    （芯片图 PA11/PA12 自动复用为 USB_DM / USB_DP）
// ② 中间件：左侧列表往下 → “Middleware and Software Packs” → USB_DEVICE → 勾选 Activate
//    → Class for FS IP → 选 “Communication Device Class (Virtual Port Com)”（即 CDC 虚拟串口）
// ③ 时钟：上方 Clock Configuration 标签页 → USB → 确保 48 MHz（72MHz 主频下自动按 /1.5 分频）
// ④ GENERATE CODE；main() 会自动调用 MX_USB_DEVICE_Init()（若 chapter_main(); 被清空需补回）

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "usbd_cdc_if.h"   /* USB 虚拟串口(CDC)：本节打印用它发给电脑，故在本节引入 */

/*
函数返回值：
函数可以返回一个值，表示函数执行的结果或状态。
check_button_pressed 函数用于检测按钮是否被按下，并返回一个布尔值表示按钮状态。
*/
bool check_button_pressed(void)
{
    static bool last_button_state = true;             // 记录上一次按钮的状态，初始为未按下（高电平）
    bool cur = (HAL_GPIO_ReadPin(BUTTON_PORT, BUTTON_PIN) == GPIO_PIN_SET);  // 读取当前按钮状态

    if (last_button_state && !cur)
    {
        last_button_state = cur;                      // 更新 last_button_state
        return true;                                  // 检测到按钮按下，返回 true
    }

    last_button_state = cur;                          // 更新 last_button_state
    return false;                                     // 没有检测到按钮按下，返回 false
}
/*
函数传入参数：
函数可以接受参数，这些参数在函数内部使用来执行特定的操作。
update_breathing_led 函数接受一个布尔参数 breathing，表示当前是否处于呼吸模式，根据该参数来更新 LED 的状态。
*/
int update_breathing_led(bool breathing)
{
    if (!breathing)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, 0);   // 关闭 LED
        return -1;                                         // 用 -1 表示“当前未在呼吸”
    }
    else
    {
        static int brightness = 0;                    // 记录当前亮度
        static bool direction = true;                 // 记录亮度变化方向

        if (direction)
        {
            brightness += brightness_step;            // 增加亮度
            if (brightness >= 1000)
            {                                          // 达到最大亮度
                brightness = 1000;
                direction = false;                     // 切换为减少亮度
            }
        }
        else
        {
            brightness -= brightness_step;            // 减少亮度
            if (brightness <= 0)
            {                                          // 达到最小亮度
                brightness = 0;
                direction = true;                      // 切换为增加亮度
            }
        }
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, brightness);   // 设置 PWM 占空比以调整 LED 亮度
        return brightness;                             // 返回当前亮度值
    }
}

// 自定义 print()：把格式化文本通过 USB 虚拟串口(CDC) 发到电脑。

void print(const char *fmt, ...)
{
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);                // 先把格式化结果写入 buf
    va_end(args);
    CDC_Transmit_FS((uint8_t *)buf, (uint16_t)strlen(buf)); // 通过 USB 虚拟串口发给电脑
}
//******** STM.C.1.5 ********//

// ▼ CubeMX 需配置（接续第 4 节）：
// ① 方向脚：芯片图依次点击 PB12、PB13、PB14、PB15 → 各选 GPIO_Output
//    下方 GPIO 面板 User Label（建议）：PB12=IN1_A、PB13=IN2_A、PB14=IN1_B、PB15=IN2_B
//    （main.h 会生成 IN1_A_Pin/IN2_A_Pin/IN1_B_Pin/IN2_B_Pin 等宏）
// ② 定时器：左侧 Categories → Timers → TIM3 → Channel1、Channel2 都选 “PWM Generation CH1/CH2”
//    （芯片图 PA6/PA7 自动复用为 TIM3_CH1/CH2）
// ③ 参数：TIM3 → Parameter Settings → Prescaler=71；Counter Period=1000（同 TIM2）
// ④ GENERATE CODE；代码中还需 HAL_TIM_PWM_Start() 启动两路 PWM

/*
引脚资源分配：单片机的引脚支持不同的功能，因此要合理分配引脚资源，避免冲突。
这里两个电机的 4 个方向脚（PB12~PB15，输出）与 2 路 PWM（PA6/PA7 = TIM3_CH1/CH2）
已在 CubeMX 里分配好，引脚宏 IN1_A_Pin/IN2_A_Pin/IN1_B_Pin/IN2_B_Pin 由 main.h 生成。
*/
/* 本节才用到电机定时器句柄 → 才声明 */
extern TIM_HandleTypeDef htim3;

void init_motor(void)
{
    // 方向脚(GPIOB)已在 MX_GPIO_Init() 配为输出；这里只需启动两路 PWM（速度由占空比决定）
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
}

//******** STM.C.1.6 ********//


// 第一层 API，传入电机ID与速度，直接控制电机转动
void set_motor_speed(int motor_id, int speed)
{
    // motor_id: 0 - 左轮, 1 - 右轮
    uint16_t in1, in2;            // 方向引脚掩码（值是 GPIO_PIN_xx，uint16_t）
    GPIO_TypeDef *port1, *port2;  // 方向引脚所在端口（值是 GPIOB 等，是 GPIO_TypeDef* 指针）
    uint32_t ch;                  // 定时器通道
    if (motor_id == 0)
    {
        // 左轮 ← 物理 Motor A（PB12/PB13 = IN1_A/IN2_A）
        in1 = IN1_A_Pin; 
        in2 = IN2_A_Pin; 
        port1 = IN1_A_GPIO_Port;
        port2 = IN2_A_GPIO_Port;
        ch = TIM_CHANNEL_1;
    }
    else
    {
        // 右轮 ← 物理 Motor B（PB14/PB15 = IN1_B/IN2_B）
        in1 = IN2_B_Pin; 
        in2 = IN1_B_Pin; 
        port1 = IN2_B_GPIO_Port;
        port2 = IN1_B_GPIO_Port;
        ch = TIM_CHANNEL_2;
    }

    if (speed > 0)
    {
        HAL_GPIO_WritePin(port1, in1, GPIO_PIN_SET);    // IN1/IN2 对调以纠正接线反向
        HAL_GPIO_WritePin(port2, in2, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim3, ch, speed);            // 正转，占空比 = speed
    }
    else if (speed < 0)
    {
        HAL_GPIO_WritePin(port1, in1, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(port2, in2, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim3, ch, -speed);           // 反转，占空比 = -speed
    }
    else
    {
        // 停止
        HAL_GPIO_WritePin(port1, in1, GPIO_PIN_SET);
        HAL_GPIO_WritePin(port2, in2, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim3, ch, 0);
    }
}

// 第二层 API，传入正向速度和转向速度，控制两轮速度实现差速转向
void drive(int forward_speed, int turn_speed)
{
    // forward_speed: -1000 到 1000，负数表示后退，正数表示前进
    // turn_speed: -1000 到 1000，负数表示左转，正数表示右转

    int left_motor_speed = forward_speed + turn_speed;   // 左轮速度 = 前进速度 + 转向速度
    int right_motor_speed = forward_speed - turn_speed;  // 右轮速度 = 前进速度 - 转向速度

    /*
    速度限幅：通过检查计算后的速度值是否超过 MAX_SPEED 阈值，
    将左右轮速度限制在 [-1000, 1000] 范围内，防止电机驱动芯片过载。
    */
    if (left_motor_speed > MAX_SPEED)  left_motor_speed = MAX_SPEED;
    if (left_motor_speed < -MAX_SPEED) left_motor_speed = -MAX_SPEED;
    if (right_motor_speed > MAX_SPEED) right_motor_speed = MAX_SPEED;
    if (right_motor_speed < -MAX_SPEED) right_motor_speed = -MAX_SPEED;

    set_motor_speed(0, left_motor_speed);   // 控制左轮
    set_motor_speed(1, right_motor_speed);  // 控制右轮
}

//******** STM.C.1.7 ********//

// ▼ CubeMX 需配置（接续第 6 节）：
// ① 串口：左侧 Categories → Connectivity → USART1 → Mode → 选 “Asynchronous”
//    （芯片图 PA9/PA10 自动复用为 USART1_TX / USART1_RX）
// ② 参数：USART1 → Parameter Settings → Baud Rate = 115200；Word Length = 8 Bits；
//    Parity = None；Stop Bits = 1（与 HC-05 蓝牙模块默认串口参数一致）
// ③ NVIC：Categories → System Core → NVIC → 勾选 “USART1 global interrupt”
//    （本节改用中断接收，每个字节到达时自动进入 USART1_IRQHandler）
// ④ GENERATE CODE；生成 MX_USART1_UART_Init() 与 huart1（若 chapter_main(); 被清空需补回）

/* huart1(USART1) 由 CubeMX 生成，供第 7/8 节蓝牙透传使用 */


/* ---------- USART1 环形缓冲区的接收与发送 ----------
 * F103 串口只有一个字节的接收缓冲，不会自动放到环形缓存区中，
 * 因此要手动配置中断，每次收到新的字符自动放置到环形缓冲区中。
 * 关于中断将在 2.1 中更详细介绍，这里只大概理解即可
*/

static volatile uint8_t uart_rx_ring[UART_RX_RING];  /* 收到的字节环形缓冲 */
static volatile uint8_t uart_rx_head = 0;            /* 中断写入位置 */
static volatile uint8_t uart_rx_tail = 0;            /* 主循环读出位置 */

/* 串口中断接收的"单字节暂存区"：HAL_UART_Receive_IT 每次只收 1 字节到这里，
   收完触发 HAL_UART_RxCpltCallback，再搬进环形缓冲区。
   注意：chapter-1.h 里用的是 extern 声明，这里是它唯一的"定义"，
   且不能加 static（chapter-2.c 也要取它的地址）。 */
volatile uint8_t uart_rx_byte = 0;


/* 收完一个字节的回调：存入环形缓冲，并立即重新挂起接收（等下一个字节） */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uint8_t next = (uint8_t)((uart_rx_head + 1) % UART_RX_RING);
        if (next != uart_rx_tail)         /* 缓冲未满才写入（满则丢弃） */
        {
            uart_rx_ring[uart_rx_head] = uart_rx_byte;
            uart_rx_head = next;
        }
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);
    }
}

/* 环形缓冲读取接口：USART1 中断负责写入；uart_handler 与
   uart_handler_control 都从这里读字节。 */
bool uart_rx_available(void)
{
    return uart_rx_tail != uart_rx_head;
}

uint8_t uart_rx_read(void)
{
    uint8_t c = uart_rx_ring[uart_rx_tail];
    uart_rx_tail = (uint8_t)((uart_rx_tail + 1) % UART_RX_RING);
    return c;
}


//******** STM.C.1.8 ********//

// 协议帧：0xA5 | forward(有符号) | turn(有符号) | 校验和 | 0x5A
static enum {
    IDLE,          // 等待帧头
    GET_FORWARD,   // 已收帧头，等待 forward 字节
    GET_TURN,      // 已收 forward，等待 turn 字节
    GET_CHECKSUM,  // 已收 turn，等待校验和字节
    GET_TAIL       // 已收校验和，等待帧尾
} uart_state = IDLE;




/* 主循环：把缓冲里攒下的字节逐个喂给状态机解析 */
void uart_handler(void)
{
    while (uart_rx_available())
    {
        uint8_t c = uart_rx_read();

        static int8_t byte1 = 0, byte2 = 0;

        /*
        switch 条件语句：根据 uart_state 的不同值执行不同的代码块，实现状态机的逻辑。
        */
        
        switch (uart_state)
        {
        case IDLE:
            if (c == 0xA5)
            {
                uart_state = GET_FORWARD;   // 检测到帧头，进入下一状态
                byte1 = 0;                  // 清零中间变量
                byte2 = 0;
            }
            break;
        case GET_FORWARD:
            byte1 = (int8_t)c;              // 转换为有符号，支持负数（后退）
            uart_state = GET_TURN;
            break;
        case GET_TURN:
            byte2 = (int8_t)c;
            uart_state = GET_CHECKSUM;
            break;
        case GET_CHECKSUM:
            /*
            校验和验证：通过 (byte1 + byte2) & 0xFF 计算校验和，
            与接收到的校验字节比较，确保数据传输的准确性。
            */
            if (((byte1 + byte2) & 0xFF) == c)
            {
                uart_state = GET_TAIL;      // 校验通过，等待帧尾
            }
            else
            {
                uart_state = IDLE;          // 校验失败，复位
            }
            break;
        case GET_TAIL:
            if (c == 0x5A)                  // 帧尾校验
            {
                forward_speed = byte1 * 4;  // 前进速度（*4 放大到 -1000~1000）
                turn_speed = byte2 * 4;     // 转向速度
            }
            uart_state = IDLE;
            break;
        }
    }
}

/* ==========================================================================
 * 原 1.8 节的主函数已整体注释掉：
 * 程序入口迁移到 chapter-2.c 的 chapter_main()，由 main() 调用。
 * ==========================================================================
 */
// void chapter_main(void)
// {
//     // 所有 GPIO/PWM/串口初始化已在 main() 里由 CubeMX 生成的
//     // MX_GPIO_Init() / MX_TIM2_Init() / MX_TIM3_Init() / MX_USART1_UART_Init() 完成，
//     // 这里只需要把两路 PWM 真正“开始输出”（CubeMX 不会自动 Start）。
//     init_pwm_led();                                // 启动 TIM2_CH2 呼吸灯 PWM
//     init_motor();                                  // 启动 TIM3 两路电机 PWM
//
//     /* 开启 USART1 接收中断：每个字节一到就存入环形缓冲，不再 10ms 轮询丢帧 */
//     HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
//     HAL_NVIC_EnableIRQ(USART1_IRQn);
//     HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);
//
//     while (1)
//     {
//         /*
//         状态机协议解析：主循环把“中断收进缓冲”的字节逐个取出来解析（uart_drain），
//         提取 forward_speed 和 turn_speed 两个控制量。
//         */
//         uart_handler();                              // 缓冲字节 → 状态机 → forward_speed/turn_speed
//
//         /*
//         控制指令应用：将解析出的 forward_speed 和 turn_speed 传入 drive 函数，
//         drive 内部通过差速转向算法控制左右轮电机，实现小车运动。
//         */
//         drive(forward_speed, turn_speed);          // 根据解析的指令控制小车运动
//
//         update_breathing_led(true);                // 呼吸灯
//
//         HAL_Delay(10);                             // 延迟
//     }
// }
