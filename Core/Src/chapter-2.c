//******** STM.C.2.1 ********//

// ▼ CubeMX 需配置（接续第 1 章）：
// ① 引脚：芯片图上依次点击 PA2、PA3、PA4、PA5 → 各选 GPIO_EXTI
//    下方 GPIO 面板：每脚 Mode → “External Interrupt Mode with Rising/Falling
//    edge trigger detection”；Pull-up/Pull-down → Pull-up（编码器输出上拉读取）
//    User Label 依次填：ENCODERA1、ENCODERA2、ENCODERB1、ENCODERB2
// ② NVIC：Categories → System Core → NVIC → 勾选 EXTI line2 / EXTI line3 /
//    EXTI line4 / EXTI line9~5 四个中断（PA2=line2、PA3=line3、PA4=line4、PA5=line5）
// ③ GENERATE CODE；CubeMX 会在 stm32f1xx_it.c 生成 EXTI2_IRQHandler… 并调用
//    HAL_GPIO_EXTI_IRQHandler，最终进入我们的 HAL_GPIO_EXTI_Callback()

#include "main.h"      /* HAL 类型与引脚宏（编码器引脚宏来自 CubeMX 的 main.h） */
#include <stdbool.h>   /* bool */
#include <stdint.h>    /* int16_t/int32_t/uint8_t */

#include "chapter-1.h"  /* 第 1 章功能库 */

/*
全局变量：count_encoderA / count_encoderB 由中断回调累加，
主循环只需读取即可拿到编码器计数，实现“中断与主程序的数据交换”。
volatile 提醒编译器该变量会被中断异步修改，别缓存到寄存器。
*/
volatile int16_t count_encoderA = 0;   // 左轮（电机A）编码器计数
volatile int16_t count_encoderB = 0;   // 右轮（电机B）编码器计数

/*
中断回调函数：编码器 4 个引脚中任意一个电平变化都会进入这里。
（CubeMX 已把各 EXTIx_IRQHandler 汇总为 HAL_GPIO_EXTI_Callback 的调用）

STM32 的 HAL 回调不直接告诉我们是上升沿还是下降沿，所以这里用
“触发引脚此刻的电平”来判断边沿（回调运行时引脚已稳定到新电平）：
正交解码思路 —— 某相发生跳变时，读另一相当前电平即可判断方向：
  触发脚现为高(上升沿)：另一相高→正转+1，另一相低→反转-1
  触发脚现为低(下降沿)：方向与上面相反
*/
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /* —— 编码器 A（左轮）—— */
    if (GPIO_Pin == ENCODERA1_Pin)
    {
        bool b = (HAL_GPIO_ReadPin(ENCODERA2_GPIO_Port, ENCODERA2_Pin) == GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(ENCODERA1_GPIO_Port, ENCODERA1_Pin) == GPIO_PIN_SET)
            /**此时A相为高电平（GPIO_PIN_SET），说明A相刚刚发生了上升沿跳变 */
            /**如果 b 为真（B相为高电平）：此时A上升沿对齐B的高电平中间，这对应编码器的正转（顺时针），所以计数器 +1 */
            /**如果 b 为假（B相为低电平）：此时A上升沿对齐B的低电平中间，这对应编码器的反转（逆时针），所以计数器 -1*/
            count_encoderA += (b ? +1 : -1);   // A1 上升沿
        else
            /**此时A相为低电平（GPIO_PIN_RESET），说明A相刚刚发生了下降沿跳变 */
            /**如果 b 为真（B相为高电平）：此时A下降沿对齐B的高电平中间，这对应编码器的反转（逆时针），所以计数器 -1 */
            /**如果 b 为假（B相为低电平）：此时A下降沿对齐B的低电平中间，这对应编码器的正转（顺时针），所以计数器 +1 */
            count_encoderA += (b ? -1 : +1);   // A1 下降沿
    }
    else if (GPIO_Pin == ENCODERA2_Pin)
    {
        bool a = (HAL_GPIO_ReadPin(ENCODERA1_GPIO_Port, ENCODERA1_Pin) == GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(ENCODERA2_GPIO_Port, ENCODERA2_Pin) == GPIO_PIN_SET)
            count_encoderA += (a ? -1 : +1);   // A2 上升沿
        else
            count_encoderA += (a ? +1 : -1);   // A2 下降沿
    }
    /* —— 编码器 B（右轮）—— */
    else if (GPIO_Pin == ENCODERB1_Pin)
    {
        bool b = (HAL_GPIO_ReadPin(ENCODERB2_GPIO_Port, ENCODERB2_Pin) == GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(ENCODERB1_GPIO_Port, ENCODERB1_Pin) == GPIO_PIN_SET)
            count_encoderB += (b ? +1 : -1);   // B1 上升沿
        else
            count_encoderB += (b ? -1 : +1);   // B1 下降沿
    }
    else if (GPIO_Pin == ENCODERB2_Pin)
    {
        bool a = (HAL_GPIO_ReadPin(ENCODERB1_GPIO_Port, ENCODERB1_Pin) == GPIO_PIN_SET);
        if (HAL_GPIO_ReadPin(ENCODERB2_GPIO_Port, ENCODERB2_Pin) == GPIO_PIN_SET)
            count_encoderB += (a ? -1 : +1);   // B2 上升沿
        else
            count_encoderB += (a ? +1 : -1);   // B2 下降沿
    }
}

//******** STM.C.2.2 ********//

// ▼ CubeMX 需配置：本节无需新增（复用已有：PA0 按键、TIM3 电机、PA2~PA5 编码器）

#define KP 30.0f          // 比例增益
#define KI 3.5f           // 积分增益
#define KD 0.0f           // 微分增益
#define PID_OUT_MAX 1000.0f
#define PID_OUT_MIN -1000.0f

/*
限幅函数：clamp_f 把输出值限制在 [lo, hi] 范围内，
防止计算出的控制量超出电机可接受的范围（与第 1 章 drive 里的限幅同理）。
*/
static float clamp_f(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

volatile float outputA = 0.0f, outputB = 0.0f;   // PID 计算出的电机 PWM 输出值
volatile int32_t speedA_target = 0, speedB_target = 0;     // 目标速度（编码器计数/10ms）
volatile int32_t speedA_current = 0, speedB_current = 0;   // 当前速度（编码器计数/10ms）

/*
反馈控制系统：根据“目标速度 - 当前速度”的误差，按 KP/KI/KD 算出输出，
驱动电机向目标速度收敛，形成闭环（区别于第 1 章的开环 drive）。
传入 motor_id 语义：A=左轮(motor0)、B=右轮(motor1)，与 set_motor_speed 一致。
*/
void speed_pid_control(int32_t speedA_target_in, int32_t speedB_target_in,
                       int32_t speedA_cur, int32_t speedB_cur)
{
    float errorA = (float)(speedA_target_in - speedA_cur);
    float errorB = (float)(speedB_target_in - speedB_cur);

    static float integralA = 0, integralB = 0;
    static float last_errorA = 0, last_errorB = 0;

    integralA += errorA;
    integralB += errorB;

    float derivativeA = errorA - last_errorA;
    float derivativeB = errorB - last_errorB;

    outputA = KP * errorA + KI * integralA + KD * derivativeA;
    outputB = KP * errorB + KI * integralB + KD * derivativeB;

    /*
    积分抗饱和：当 PID 输出达到限幅值时，撤销本次积分累加，
    防止积分项在饱和状态下持续增长导致恢复延迟（积分饱和现象）。
    */
    if (outputA <= PID_OUT_MIN || outputA >= PID_OUT_MAX)
        integralA -= errorA;
    if (outputB <= PID_OUT_MIN || outputB >= PID_OUT_MAX)
        integralB -= errorB;

    outputA = clamp_f(outputA, PID_OUT_MIN, PID_OUT_MAX);
    outputB = clamp_f(outputB, PID_OUT_MIN, PID_OUT_MAX);

    /* 输出范围 ±1000 与第 1 章 set_motor_speed 的 PWM 占空比范围一致 */
    set_motor_speed(0, (int)outputA);   // 左轮（电机A）
    set_motor_speed(1, (int)outputB);   // 右轮（电机B）

    last_errorA = errorA;
    last_errorB = errorB;
}

// void chapter_main(void)
// {
//     // （按键 PA0、电机 PWM、编码器 EXTI 均已由 CubeMX 配置）
//     init_pwm_led();      // 启动呼吸灯 PWM
//     init_motor();        // 启动两路电机 PWM

//     while (1)
//     {
//         // 三态循环：每次按键切换 0 → +30 → -30 → 0 → …
//         static uint8_t speed_state = 0;            // 0:停止 1:前进 2:后退
//         if (check_button_pressed())
//         {
//             speed_state = (speed_state + 1) % 3;
//             if (speed_state == 0) { speedA_target = 0;    speedB_target = 0; }
//             if (speed_state == 1) { speedA_target = 30;   speedB_target = 30; }
//             if (speed_state == 2) { speedA_target = -30;  speedB_target = -30; }
//             print(">>> State %d: A=%d  B=%d\n", speed_state, speedA_target, speedB_target);
//         }

//         // 每 10ms 计算一次速度并做 PID 控制
//         static uint32_t pid_last_time = 0;
//         if (pid_last_time == 0) pid_last_time = HAL_GetTick();
//         uint32_t current_time = HAL_GetTick();
//         if (current_time - pid_last_time >= 10)
//         {
//             pid_last_time += 10;
//             /*
//             抽象单位：直接用编码器计数作为速度的抽象单位（计数/10ms）来做 PID，
//             不必换算成真实物理速度，简化计算与调试。
//             B 轮取反：编码器安装方向与电机方向相反，A/B 统一为“正数=前进”。
//             */
//             speedA_current = count_encoderA;
//             speedB_current = -count_encoderB;
//             count_encoderA = 0;   // 每周期清零，得到本周期脉冲数
//             count_encoderB = 0;

//             speed_pid_control(speedA_target, speedB_target,
//                               speedA_current, speedB_current);
//             update_breathing_led(true);
//         }

//         // 每 100ms 打印一次状态（输出 %f 需开启浮点 printf，见工程 CMake 说明）
//         static uint32_t print_last_time = 0;
//         if (current_time - print_last_time >= 100)
//         {
//             print_last_time += 100;
//             print("TgtA:%d CurA:%d OutA:%d | TgtB:%d CurB:%d OutB:%d\n",
//                   speedA_target, speedA_current, (int)outputA,
//                   speedB_target, speedB_current, (int)outputB);
//         }
//     }
// }
//******** STM.C.2.3 ********//

// ▼ CubeMX 需配置（接续 2.2）：
// ① 定时器：左侧 Categories → Timers → TIM1 → Clock Source 选 “Internal Clock”
// ② 参数：TIM1 → Parameter Settings → Prescaler = 71；Counter Period = 9999
//    （TIM1 在 APB2，时钟 72MHz ÷72 = 1MHz，计满 (9999+1)µs = 10ms 中断；)
// ③ NVIC：TIM1 → NVIC Settings → 勾选 “TIM1 update interrupt”（TIM1_UP_IRQn）
// ④ GENERATE CODE；main.c 生成 MX_TIM1_Init() 与 htim1 并自动调用

/* 本节用到 10ms 定时器句柄 */
extern TIM_HandleTypeDef htim1;

/* 10ms 速度 PID 任务：读编码器当周期脉冲 → 当当前速度 → 调速度 PID */
void pid_tick_speed(void)
{
    speedA_current = count_encoderA;
    speedB_current = -count_encoderB;   // B 轮方向取反（与 2.2 一致）
    count_encoderA = 0;                 // 重置，得到下 10ms 的脉冲数
    count_encoderB = 0;

    speed_pid_control(speedA_target, speedB_target,
                      speedA_current, speedB_current);
}

/* 用函数指针中转一下，执行 pid_task 时，实际上执行了 pid_tick_speed 。
此处完全可以直接在中断里调用 pid_tick_speed()，但用函数指针可以在之后的章节中方便地切换到其他任务 */

static void (*pid_task)(void) = pid_tick_speed;

/* TIM1 更新中断回调：每 10ms 进一次，转给 pid_task 指向的任务 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        pid_task();                 // 速度(2.3) 或 位置(2.4/2.8) 任务
    }
}

/* 启动 10ms PID 定时中断（在各节 main 里调用一次） */
void start_pid_timer(void)
{
    HAL_TIM_Base_Start_IT(&htim1);
}

/* ---------------- 升级后的串口控制协议：0xA5 | ctrl | forward | turn | 校验和 | 0x5A ---------------- */
static enum {
    CTRL_IDLE,          // 等待帧头
    CTRL_GET_CONTROL,   // 已收帧头，等待控制字节
    CTRL_GET_FORWARD,   // 已收 ctrl，等待 forward
    CTRL_GET_TURN,      // 已收 forward，等待 turn
    CTRL_GET_CHECKSUM,  // 已收 turn，等待校验和
    CTRL_GET_TAIL       // 已收校验和，等待帧尾
} uart_ctrl_state = CTRL_IDLE;

/* 控制字节里解析出的标志位（volatile：中断/主循环都会读写） */
volatile bool control_switch    = false;   // bit0: 总开关（false=关, true=开）
volatile bool control_mode      = false;   // bit1: 模式（false=手动, true=自动）
volatile bool control_fwd_pulse = false;   // bit2: 前进脉冲指令（2.4 位置控制用）
volatile bool control_bwd_pulse = false;   // bit3: 后退脉冲指令（2.4 位置控制用）
volatile int16_t forward_target = 100, turn_target = 0;   // 前进/转向量（速度目标）

void uart_handler_control(void)
{
    while (uart_rx_available())   /* 环形缓冲里有字节就取出来解析 */
    {
        uint8_t c = uart_rx_read();

        static int8_t byte1 = 0, byte2 = 0;
        static uint8_t byte_ctrl = 0;

        switch (uart_ctrl_state)
        {
        case CTRL_IDLE:
            if (c == 0xA5)
                uart_ctrl_state = CTRL_GET_CONTROL;   // 帧头
            break;
        case CTRL_GET_CONTROL:
            byte_ctrl = c;
            /*
            位运算解析标志位：用按位与 & 提取控制字节中各 bit 的含义，
            把总开关/模式/前后脉冲等多个布尔量打包进一个字节传输，省流量。
            */
            control_switch     = (byte_ctrl & 0x01) != 0;   // bit0: 总开关
            control_mode       = (byte_ctrl & 0x02) != 0;   // bit1: 模式
            control_fwd_pulse  = (byte_ctrl & 0x04) != 0;   // bit2: 前进脉冲
            control_bwd_pulse  = (byte_ctrl & 0x08) != 0;   // bit3: 后退脉冲
            uart_ctrl_state = CTRL_GET_FORWARD;
            break;
        case CTRL_GET_FORWARD:
            byte1 = (int8_t)c;              // 转为有符号，支持后退
            uart_ctrl_state = CTRL_GET_TURN;
            break;
        case CTRL_GET_TURN:
            byte2 = (int8_t)c;
            uart_ctrl_state = CTRL_GET_CHECKSUM;
            break;
        case CTRL_GET_CHECKSUM:
        {
            uint8_t calc = (uint8_t)((byte_ctrl + byte1 + byte2) & 0xFF);
            if (calc == c)
                uart_ctrl_state = CTRL_GET_TAIL;   // 校验通过，等帧尾
            else
                uart_ctrl_state = CTRL_IDLE;       // 校验失败，复位
            break;
        }
        case CTRL_GET_TAIL:
            if (c == 0x5A)              // 帧尾
            {
                /* /4 还原到小范围“速度目标”（编码器计数/10ms 量级） */
                forward_target = byte1 / 4;
                turn_target    = byte2 / 4;

                if (control_switch)
                {
                    if (!control_mode)          // 手动模式：差速 → 左右轮速度目标
                    {
                        speedA_target = forward_target + turn_target;
                        speedB_target = forward_target - turn_target;
                    }
                    /* 自动模式(control_mode=true)：2.7/2.8 由 auto_drive_logic 接管 */
                }
                else
                {
                    speedA_target = 0;
                    speedB_target = 0;
                }
            }
            uart_ctrl_state = CTRL_IDLE;
            break;
        }
    }
}

// void chapter_main(void)
// {
//     init_pwm_led();       // 呼吸灯 PWM
//     init_motor();         // 电机 PWM

//     /* 开两个中断：
//     ① USART1 接收中断：字节一到自动进环形缓冲（同 1.8） */  
//     HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
//     HAL_NVIC_EnableIRQ(USART1_IRQn);
//     HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);
    
//     /*② TIM1 10ms 定时中断：自动执行 pid_task（速度 PID） */
//     start_pid_timer();

//     while (1)
//     {
//         uart_handler_control();    // 解析控制字节：手动时更新 speedA/B_target
//         update_breathing_led(true);
//         HAL_Delay(10);
//     }
// }
//******** STM.C.2.4 ********//

#include <math.h>   /* sqrtf（梯形规划减速段用），本节才用到数学库 */

volatile int32_t positionA_current = 0, positionB_current = 0;   // 当前位置（累计脉冲）
volatile int32_t positionA_target = 0, positionB_target = 0;     // 目标位置（脉冲）

// 梯形运动轨迹参数
#define TRAP_ACCEL 0.2f      // 加速度（脉冲/10ms²）若仍震荡可调小 PID 参数
#define TRAP_SPEED_MAX 30    // 最高速度（脉冲/10ms）
#define POS_THRESHOLD 40     // 到达判定：位置误差小于此值视为接近目标
#define SPEED_THRESHOLD 10   // 到达判定：速度小于此值视为已停下

/*
梯形速度规划器：根据目标位置与当前位置，按恒定加速度加速/减速，
计算出“命令速度”，保证精确停在目标点；A、B 两轮独立计算。
到达判定：位置误差与速度同时小于阈值 → 目标速度归 0。
*/
void trapezoidal_plan(int32_t posA_target, int32_t posB_target,
                      int32_t posA_cur, int32_t posB_cur,
                      int32_t speedA_cur, int32_t speedB_cur)
{
    int32_t errA = posA_target - posA_cur;
    int32_t errB = posB_target - posB_cur;

    /* 用三目运算符计算误差绝对值（到目标还有多远） */
    int32_t distA = (errA < 0 ? -errA : errA);
    int32_t distB = (errB < 0 ? -errB : errB);

    // 到达判定：误差小且已停下 → 停止输出
    if (distA < POS_THRESHOLD && distB < POS_THRESHOLD &&
        (speedA_cur < 0 ? -speedA_cur : speedA_cur) < SPEED_THRESHOLD &&
        (speedB_cur < 0 ? -speedB_cur : speedB_cur) < SPEED_THRESHOLD)
    {
        speedA_target = 0;
        speedB_target = 0;
        return;
    }

    /* ============ 电机 A ============ */
    static float cmdA = 0;
    {
        float dir = (errA >= 0) ? 1.0f : -1.0f;            // 方向：目标在前为正
        float abs_cmd = (cmdA < 0 ? -cmdA : cmdA);         // 当前命令速度绝对值
        float decel_dist = abs_cmd * abs_cmd / (2.0f * TRAP_ACCEL);   // 减速所需距离
        if (distA <= decel_dist)
            abs_cmd = sqrtf((float)distA * 2.0f * TRAP_ACCEL);        // 进入减速段
        else
        {
            abs_cmd += TRAP_ACCEL;                        // 加速/匀速段
            if (abs_cmd > TRAP_SPEED_MAX) abs_cmd = TRAP_SPEED_MAX;   // 限最高速
        }
        cmdA = dir * abs_cmd;
        speedA_target = (int32_t)cmdA;
    }

    /* ============ 电机 B ============ */
    static float cmdB = 0;
    {
        float dir = (errB >= 0) ? 1.0f : -1.0f;
        float abs_cmd = (cmdB < 0 ? -cmdB : cmdB);
        float decel_dist = abs_cmd * abs_cmd / (2.0f * TRAP_ACCEL);
        if (distB <= decel_dist)
            abs_cmd = sqrtf((float)distB * 2.0f * TRAP_ACCEL);
        else
        {
            abs_cmd += TRAP_ACCEL;
            if (abs_cmd > TRAP_SPEED_MAX) abs_cmd = TRAP_SPEED_MAX;
        }
        cmdB = dir * abs_cmd;
        speedB_target = (int32_t)cmdB;
    }

    // 把命令速度交给速度 PID（外环位置 → 内环速度 → 电机）
    speed_pid_control(speedA_target, speedB_target, speedA_cur, speedB_cur);
}

/* 位置 PID 定时任务：每 10ms 累加编码器脉冲得到当前位置，再梯形规划 */
void pid_tick_position(void)
{
    int32_t sA = count_encoderA;
    int32_t sB = -count_encoderB;

    /*
    位置累加：把本次周期的脉冲数累加进位置变量，即“对速度积分”，
    从而得到电机的绝对位置（脉冲）。
    */
    positionA_current += sA;
    positionB_current += sB;
    count_encoderA = 0;
    count_encoderB = 0;

    trapezoidal_plan(positionA_target, positionB_target,
                     positionA_current, positionB_current,
                     sA, sB);
}

/* 位置脉冲指令：检测控制字节 bit2/bit3 上升沿，触发前进/后退 5000 脉冲 */
void check_position_buttons(void)
{
    static bool last_fwd = false, last_bwd = false;

    if (control_fwd_pulse && !last_fwd)      // bit2 上升沿 → 前进
    {
        positionA_target += 5000;
        positionB_target += 5000;
    }
    last_fwd = control_fwd_pulse;

    if (control_bwd_pulse && !last_bwd)      // bit3 上升沿 → 后退
    {
        positionA_target -= 5000;
        positionB_target -= 5000;
    }
    last_bwd = control_bwd_pulse;
}

// void chapter_main(void)
// {
//     init_pwm_led();
//     init_motor();

//     /* 开两个中断：USART1 接收 + TIM1 10ms（同 2.3） */
//     HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
//     HAL_NVIC_EnableIRQ(USART1_IRQn);
//     HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_rx_byte, 1);

//     pid_task = pid_tick_position;   // 定时任务切到“位置环 + 梯形规划”
//     start_pid_timer();

//     while (1)
//     {
//         uart_handler_control();            // 环形缓冲 → 控制协议状态机
//         check_position_buttons();     // 检测 bit2/bit3 上升沿 → 设目标位置
//         update_breathing_led(true);
//         HAL_Delay(10);
//     }
// }
//******** STM.C.2.5 ********//

// ▼ CubeMX 需配置（接续 2.4）：
// ① 引脚：芯片图点击 PB7 → 选 GPIO_Output（TRIG 触发脚）
//    下方 GPIO 面板 User Label 填：ULTRASONIC_TRIG
//    芯片图点击 PB8 → 选 GPIO_Input（ECHO 回声脚；空闲为低，可不设上下拉）
//    User Label 填：ULTRASONIC_ECHO
// ② GENERATE CODE

/* 引脚宏来自 CubeMX 的 User Label（在 main.h 中自动生成），这里不再手写 */
/* 引用：ULTRASONIC_TRIG_Pin/_GPIO_Port = PB7、ULTRASONIC_ECHO_Pin/_GPIO_Port = PB8 */

/* 微秒级计时：STM32 的 HAL 只提供毫秒 HAL_GetTick()，测距需要 µs 精度，
   所以启用内核 DWT 计数器（72MHz，每计数 ≈ 1/72 µs）做微秒计时。 */
void time_init_dwt(void)
{
    if (!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk))
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   // 使能 DWT 访问
        DWT->CYCCNT = 0;
        DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;             // 启动周期计数
    }
}

/* 返回开机以来的微秒数（µs） */
uint32_t micros(void)
{
    return DWT->CYCCNT / (SystemCoreClock / 1000000U);
}

/* 微秒级阻塞延时（delay_us） */
void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { /* 等待 */ }
}

void init_ultrasonic(void)
{
    time_init_dwt();   // 初始化 µs 计时（TRIG/ECHO 引脚模式已由 CubeMX 配好）
}

void trigger_ultrasonic(void)
{
    /* 给 TRIG 一个 ≥10µs 的高电平脉冲，HC-SR04 便会自动发 8 个 40kHz 声波 */
    HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);
    delay_us(2);
    HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_SET);
    delay_us(12);
    HAL_GPIO_WritePin(ULTRASONIC_TRIG_GPIO_Port, ULTRASONIC_TRIG_Pin, GPIO_PIN_RESET);
}

/* 测量距离：量 ECHO 高电平脉宽 → 距离 = 脉宽µs / 58.2 (cm)
   阻塞式测量：函数会等待回声返回，期间主循环被阻塞（可能影响系统响应）。 */
double measure_distance(void)
{
    const uint32_t timeout_us = 10000;   // 10ms 超时
    const uint32_t cycles_per_us = SystemCoreClock / 1000000U;
    const uint32_t timeout_cycles = timeout_us * cycles_per_us;

    trigger_ultrasonic();

    /* 等待 ECHO 变高（超时则返回 -1 表示测量失败） */
    uint32_t t0 = DWT->CYCCNT;
    while (HAL_GPIO_ReadPin(ULTRASONIC_ECHO_GPIO_Port, ULTRASONIC_ECHO_Pin) == GPIO_PIN_RESET)
    {
        if ((uint32_t)(DWT->CYCCNT - t0) > timeout_cycles) return -1.0;
    }

    uint32_t start = DWT->CYCCNT;        // 记录回声开始
    while (HAL_GPIO_ReadPin(ULTRASONIC_ECHO_GPIO_Port, ULTRASONIC_ECHO_Pin) == GPIO_PIN_SET)
    {
        if ((uint32_t)(DWT->CYCCNT - start) > timeout_cycles) return -1.0;
    }
    uint32_t duration = (uint32_t)(DWT->CYCCNT - start) / cycles_per_us;

    return (double)duration / 58.2;         // ≈ cm
}

/*
指数加权滤波 (EWMA)：对历史数据按指数衰减权重平滑。
alpha 为平滑因子(0~1)，越小越平滑但响应越慢。
*/
double filter_ewma(double current_val)
{
    static double distance_filtered = 0.0;
    const double alpha = 0.2;
    if (distance_filtered == 0.0)
        distance_filtered = current_val;
    else
        distance_filtered = alpha * current_val + (1.0 - alpha) * distance_filtered;
    return distance_filtered;
}

/*
滑动窗均值滤波 (Moving Average)：固定大小缓冲区，新数据覆盖最旧数据，
取窗口平均值，适合去除随机噪声。
*/
double filter_moving_average(double current_val)
{
#define MA_SIZE 5
    static double buffer[MA_SIZE] = {0};
    static uint8_t index = 0;
    buffer[index] = current_val;
    index = (index + 1) % MA_SIZE;
    double sum = 0.0;
    for (uint8_t i = 0; i < MA_SIZE; i++)
        sum += buffer[i];
    return sum / MA_SIZE;
}

/*
滑动窗中值滤波 (Median)：窗口内取中位数，对偶发的极端值（尖峰噪声）抑制很好。
*/
double filter_moving_median(double current_val)
{
#define MEDIAN_SIZE 5
    static double buffer[MEDIAN_SIZE] = {0};
    static uint8_t index = 0;
    buffer[index] = current_val;
    index = (index + 1) % MEDIAN_SIZE;

    double temp[MEDIAN_SIZE];
    for (uint8_t i = 0; i < MEDIAN_SIZE; i++)
        temp[i] = buffer[i];

    /* 冒泡排序：双重 for 逐对比较交换，排序后取中间值 */
    for (uint8_t i = 0; i < MEDIAN_SIZE - 1; i++)
        for (uint8_t j = 0; j < MEDIAN_SIZE - i - 1; j++)
            if (temp[j] > temp[j + 1])
            {
                double t = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = t;
            }
    return temp[MEDIAN_SIZE / 2];
}

// void chapter_main(void)
// {
//     init_pwm_led();           // 呼吸灯
//     init_ultrasonic();        // 超声波 + µs 计时

//     while (1)
//     {
//         // 每 100ms 测一次距（HC-SR04 最高约 40Hz，100ms 有余量）
//         static uint32_t measure_last_time = 0;
//         if (measure_last_time == 0) measure_last_time = HAL_GetTick();
//         uint32_t current_time = HAL_GetTick();
//         if (current_time - measure_last_time >= 100)
//         {
//             measure_last_time += 100;
//             double distance = measure_distance();

//             // 三种滤波同时处理原始数据，对比各自效果
//             // 单位换算：measure_distance() 返回 cm，×10 取整后按 mm 用 %d 输出（1 cm = 10 mm）
//             double distance_ewma   = filter_ewma(distance);
//             double distance_ma     = filter_moving_average(distance);
//             double distance_median = filter_moving_median(distance);
//             print("Raw:%d EWMA:%d MA:%d Med:%d mm\n",
//                   (int)(distance * 10.0), (int)(distance_ewma * 10.0),
//                   (int)(distance_ma * 10.0), (int)(distance_median * 10.0));
//         }
//         update_breathing_led(true);
//         HAL_Delay(10);
//     }
// }
//******** STM.C.2.6 ********//

// ▼ CubeMX 需配置（接续 2.5）：
// ① 定时器：Categories → Timers → TIM4 → Channel1 下拉 → 选 “PWM Generation CH1”
//    （芯片图 PB6 自动复用为 TIM4_CH1）
// ② 参数：TIM4 → Parameter Settings → Prescaler = 71；Counter Period = 19999
//    （APB1 定时器时钟 72MHz /72 = 1MHz，周期 20000µs = 20ms = 50Hz，适合舵机；
//     占空比脉宽 500~2500µs 对应 0°~180°）
// ③ GENERATE CODE；

#define SERVO_TIM_CH  TIM_CHANNEL_1    /* 舵机挂在 TIM4_CH1（PB6） */

/* 本节才用到舵机定时器句柄 → 才声明（句柄由 CubeMX 在 main.c 生成） */
extern TIM_HandleTypeDef htim4;

/* PWM 舵机：50Hz/20ms，通过改变高电平脉宽(µs)控制角度 */
void init_servo(void)
{
    HAL_TIM_PWM_Start(&htim4, SERVO_TIM_CH);
    __HAL_TIM_SET_COMPARE(&htim4, SERVO_TIM_CH, 1500);   // 初始中位 1500µs
}

/*
线性映射：把 x 从 [in_min, in_max] 映射到 [out_min, out_max]。
y = (x-in_min)*(out_max-out_min)/(in_max-in_min) + out_min
这里把角度索引(0~10)映射到脉宽(500~2100µs)。
*/
int32_t map(int32_t x, int32_t in_min, int32_t in_max, int32_t out_min, int32_t out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

double distances_radar[11];     // 11 个角度的雷达距离缓存

/* 转动舵机到指定索引角度（并线性映射成 PWM 脉宽） */
void update_servo_angle(uint8_t index)
{
    uint16_t pulse_width = (uint16_t)map(index, 0, 10, 500, 2400);
    __HAL_TIM_SET_COMPARE(&htim4, SERVO_TIM_CH, pulse_width);
}

/* 在某角度测 5 次，冒泡排序取中位数，写入雷达缓存 */
void update_radar(uint8_t index)
{
    double samples[5];
    for (int i = 0; i < 5; i++)
    {
        samples[i] = measure_distance();
    }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4 - i; j++)
            if (samples[j] > samples[j + 1])
            {
                double temp = samples[j];
                samples[j] = samples[j + 1];
                samples[j + 1] = temp;
            }
    distances_radar[index] = samples[2];   // 取中位数
}

/* 打印 11 个角度距离（分段 print，避免超过缓冲区） */
void print_radar_results(void)
{
    print("Radar(cm):%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,\r\n",
          (int)distances_radar[0],  (int)distances_radar[1],  (int)distances_radar[2],
          (int)distances_radar[3],  (int)distances_radar[4],  (int)distances_radar[5],
          (int)distances_radar[6],  (int)distances_radar[7],  (int)distances_radar[8],
          (int)distances_radar[9],  (int)distances_radar[10]);
}

// void chapter_main(void)
// {
//     init_pwm_led();
//     init_ultrasonic();     // 超声波 + µs 计时
//     init_servo();          // 舵机 PWM（TIM4_CH1）

//     while (1)
//     {
//         /* 正向扫描 0~10：定角度 → 等舵机到位 → 测距 → 打印 */
//         for (uint8_t index = 0; index <= 10; index++)
//         {
//             update_servo_angle(index);
//             HAL_Delay(100);               // 等舵机稳定
//             update_radar(index);
//             print_radar_results();
//         }
//         /* 反向扫描 9~1：来回摆动，避免单向快速回转的冲击与误差 */
//         for (int8_t index = 9; index > 0; index--)
//         {
//             update_servo_angle(index);
//             HAL_Delay(100);
//             update_radar(index);
//             print_radar_results();
//         }
//     }
// }
//******** STM.C.2.7 ********//

// ============================================================
//  可调参数（集中放在一起方便调）
// ============================================================
static const int AUTO_DRIVE_FORWARD_SPEED = 50;  // 前进速度（编码器计数/10ms）
static const int AUTO_DRIVE_TURN_SPEED   = 15;   // 转向速度
static const double EMERGENCY_CM = 30.0;         // 紧急刹车距离(cm)
static const int SERVO_WAIT_MS   = 100;          // 舵机到位等待时间(ms)
static const int BACKUP_MS       = 500;          // 后退持续时间(ms)
static const int TURN_MS         = 300;          // 自转持续时间(ms)
static const double CENTER_INDEX = 5.0;          // 7 通道中间值(2~8 中间)
static const double DEVIATION_GAIN = 20.0;        // 偏差 → 速度增益

/*
枚举状态机：自动驾驶的四个状态：
- FORWARD   前进中，交替扫描 7 个通道，重心法微调方向
- BACKUP    遇障后退，倒计时结束进入全向扫描
- SCAN_FULL 全向扫描 11 个通道，比较左右决定转向
- TURN      原地自转，结束后重新前进
*/
static enum {
    FORWARD,
    BACKUP,
    SCAN_FULL,
    TURN
} drive_state = FORWARD;

// ============================================================
//  各状态专用变量（按状态分组）
// ============================================================
// --- FORWARD ---
static uint8_t fwd_scan_i = 0;             // 当前扫描的缓冲索引(0~6)
static bool fwd_scan_toward_right = true;  // true=左→右, false=右→左
static double fwd_buf[7] = {0};            // 7 个通道距离缓存
static uint8_t fwd_count = 0;              // 本轮已扫通道数
static uint32_t fwd_servo_time = 0;        // 舵机等待计时(ms)
// --- BACKUP ---
static uint32_t backup_timer = 0;          // 后退计时
// --- SCAN_FULL ---
static uint8_t scan_index = 0;             // 当前扫描通道(0~10)
static double scan_buffer[11] = {0};       // 11 通道距离缓存
static uint32_t scan_servo_time = 0;       // 舵机等待计时
// --- TURN ---
static int turn_direction = 0;             // -1 左转, 1 右转
static uint32_t turn_timer = 0;            // 自转计时

// ============================================================
//  工具函数
// ============================================================
static double safe_dist(double d)
{
    return (d < 0) ? 0.0 : d;   // 测量失败时按近距离处理，避免失效后继续盲目前进
}

void auto_drive_logic(void)
{
    switch (drive_state)
    {
    case FORWARD:
    {
        uint8_t idx = 2 + fwd_scan_i;   // 用舵机 2~8 号位置当 7 通道

        /*
        非阻塞延时：用 HAL_GetTick() 时间戳代替 delay 阻塞等待。
        等舵机稳定期间主循环仍可继续处理其它任务（PID 由定时中断在跑）。
        */
        if (fwd_servo_time == 0)
        {
            update_servo_angle(idx);
            fwd_servo_time = HAL_GetTick();
            break;
        }
        if (HAL_GetTick() - fwd_servo_time < SERVO_WAIT_MS)
            break;
        fwd_servo_time = 0;

        fwd_buf[fwd_scan_i] = safe_dist(measure_distance());
        if (fwd_count < 7) fwd_count++;

        // 前方太近 → 立刻后退并进入全向扫描
        if (fwd_buf[fwd_scan_i] < EMERGENCY_CM)
        {
            drive_state = BACKUP;
            backup_timer = HAL_GetTick();
            speedA_target = -AUTO_DRIVE_FORWARD_SPEED;
            speedB_target = -AUTO_DRIVE_FORWARD_SPEED;
            fwd_count = 0;
            fwd_servo_time = 0;
            update_servo_angle(0);
            break;
        }

        /*
        重心加权计算：以各通道距离为权重算加权中心，与正中间比较得到偏差。
        偏差指向“障碍较远”的一侧 → 用偏差微调差速转向，远离障碍。
        */
        double total_weight = 0, weighted_sum = 0;
        for (uint8_t i = 0; i < 7; i++)
        {
            weighted_sum += (2 + i) * fwd_buf[i];
            total_weight += fwd_buf[i];
        }
        float deviation = (float)(weighted_sum / total_weight) - (float)CENTER_INDEX;
        speedA_target = AUTO_DRIVE_FORWARD_SPEED + (int)(-deviation * DEVIATION_GAIN);
        speedB_target = AUTO_DRIVE_FORWARD_SPEED - (int)(-deviation * DEVIATION_GAIN);

        /*
        折返扫描：扫描指针在 0~6 来回移动，让舵机平滑摆动，
        避免每次从头扫造成的机械冲击与测量延迟。
        */
        if (fwd_scan_toward_right)
        {
            fwd_scan_i++;
            if (fwd_scan_i >= 7)
            {
                fwd_scan_toward_right = false;
                fwd_scan_i = 6;
                fwd_count = 0;
            }
        }
        else
        {
            if (fwd_scan_i == 0)
            {
                fwd_scan_toward_right = true;
                fwd_count = 0;
            }
            else
                fwd_scan_i--;
        }
        break;
    }
    case BACKUP:
    {
        if (HAL_GetTick() - backup_timer >= BACKUP_MS)
        {
            drive_state = SCAN_FULL;
            scan_index = 0;
            scan_servo_time = 0;
            speedA_target = 0;
            speedB_target = 0;
        }
        break;
    }
    case SCAN_FULL:
    {
        // 非阻塞等舵机到位
        if (scan_servo_time == 0)
        {
            update_servo_angle(scan_index);
            scan_servo_time = HAL_GetTick();
            break;
        }
        if (HAL_GetTick() - scan_servo_time < SERVO_WAIT_MS)
            break;
        scan_servo_time = 0;

        // 逐个扫描 11 通道
        scan_buffer[scan_index] = safe_dist(measure_distance());
        scan_index++;
        if (scan_index < 11)
            break;
        scan_index = 0;

        // 比较左半(6~10)与右半(0~4)平均距离，转向更堵(更近)一侧
        double left_avg = 0, right_avg = 0;
        for (int i = 0; i <= 4; i++)  right_avg += scan_buffer[i];
        for (int i = 6; i <= 10; i++) left_avg  += scan_buffer[i];
        left_avg  /= 5.0;
        right_avg /= 5.0;
        if (left_avg < right_avg)
            turn_direction = 1;      // 左边更堵 → 右转
        else
            turn_direction = -1;     // 右边更堵 → 左转

        drive_state = TURN;
        speedA_target =  turn_direction * AUTO_DRIVE_TURN_SPEED;
        speedB_target = -turn_direction * AUTO_DRIVE_TURN_SPEED;
        break;
    }
    case TURN:
    {
        if (turn_timer == 0)
            turn_timer = HAL_GetTick();
        if (HAL_GetTick() - turn_timer >= TURN_MS)
        {
            turn_timer = 0;
            drive_state = FORWARD;
            scan_index = 0;
            scan_servo_time = 0;
        }
        break;
    }
    }
}

void chapter_main(void)
{
    init_motor();          // 电机 PWM
    init_ultrasonic();     // 超声波
    init_servo();          // 舵机

    start_pid_timer();     // 10ms 定时中断，pid_task 默认 = 速度 PID

    while (1)
    {
        // 自动避障状态机直接改写 speedA/B_target，由定时中断的 PID 去执行
        auto_drive_logic();
    }
}


