#include "tuya_cloud_types.h"
#include "tkl_adc.h"
#include "tal_log.h"
#include "tuya_error_code.h"

// #include "adc_key_main.h"

// #include <os/os.h>
// #include <os/mem.h>
// #include "adc_key_main.h"
// #include "modules/pm.h"

#include "tal_uart.h"

#include "log_seq.h"
#include "media_src_zh.h"
#include "svc_ai_player.h"

#include "wukong_ai_mode.h"
#include "wukong_kws.h"
#include "tuya_ai_toy.h"
#include "wukong_tm_internal.h"

#include "tal_queue.h"
#include "tal_workq_service.h"

// #include "FreeRTOS.h"

// #include "bk_gpio.h"
// #include "gpio_driver.h"
#include "wukong_ai_agent.h"
#include "wukong_ai_skills.h"
#include "skill_cloudevent.h"
#include "wukong_audio_player.h"
#include "hugo_ai_position_sensor.h"
// #include "hugo_ai_desktop.h"

#include "tdd_sw_i2c.h"

#include <math.h>

// #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define SC7A20_SDA2_PIN           TUYA_GPIO_NUM_14      //Board
#define SC7A20_SCL2_PIN           TUYA_GPIO_NUM_16

#define SC7A20_SDA1_PIN          TUYA_GPIO_NUM_8        //Extra
#define SC7A20_SCL1_PIN          TUYA_GPIO_NUM_20
/***********************************************************
***********************typedef define***********************
***********************************************************/
#define TICK_US             19

#define SW_I2C_DELAY_US     5    // I2C时序延时，5us稳定
#define IGNORE_ACK_MODE     0    // 1=无视ACK强制发完所有字节  0=标准检测ACK

#define RAD_TO_DEG 57.2957795f  // 弧度转角度系数 180/π

#define SC7A20_FILTER_K 0.15

// #define I2C_WRITE_BUFLEN            2u
// #define I2C_READ_BUFLEN             6u

/***********************************************************
***********************variable define**********************
***********************************************************/
float pitch_out = 0;
float roll_out = 0;
float direct_out = 0;


float pitch_pre = 0xFF;
float roll_pre = 0xFF;
float direct_pre = 0xFF;

STATIC UINT8_T firsrun_flag = 30;

extern UINT8_T moto_flag;

/***********************************************************
***********************function define**********************
***********************************************************/
// STATIC VOID_T sda_output_high(VOID_T)
// {
//     tkl_gpio_write(SC7A20_SDA2_PIN, TUYA_GPIO_LEVEL_HIGH);
// }
// STATIC VOID_T sda_output_low(VOID_T)
// {
//     tkl_gpio_write(SC7A20_SDA2_PIN, TUYA_GPIO_LEVEL_LOW);
// }
// STATIC VOID_T scl_output_high(VOID_T)
// {
//     tkl_gpio_write(SC7A20_SCL2_PIN, TUYA_GPIO_LEVEL_HIGH);
// }
// STATIC VOID_T scl_output_low(VOID_T)
// {
//     tkl_gpio_write(SC7A20_SCL1_PIN, TUYA_GPIO_LEVEL_LOW);
// }
// #define sda_output_high()   tkl_gpio_write(SC7A20_SDA2_PIN, TUYA_GPIO_LEVEL_HIGH)
// #define sda_output_low()    tkl_gpio_write(SC7A20_SDA2_PIN, TUYA_GPIO_LEVEL_LOW)
// #define scl_output_high()   tkl_gpio_write(SC7A20_SCL2_PIN, TUYA_GPIO_LEVEL_HIGH)
// #define scl_output_low()    tkl_gpio_write(SC7A20_SCL2_PIN, TUYA_GPIO_LEVEL_LOW)

// #define sw_i2c_delay() \
// do { \
//     volatile UINT_T i = SW_I2C_DELAY_US * TICK_US; \
//     while (i--); \
// } while(0)



// 获取量程对应灵敏度 mg/LSB
STATIC float get_sensitivity(UINT8_T fs)
{
    switch(fs)
    {
        case FS_2G:  return 0.244f;
        case FS_4G:  return 0.488f;
        case FS_8G:  return 0.976f;
        case FS_16G: return 1.952f;
        default: return 0.244f;
    }
}

// STATIC VOID_T sda_set_input(VOID_T)
// {
//     OPERATE_RET rt = OPRT_OK;
//     /*GPIO input init*/
//     TUYA_GPIO_BASE_CFG_T in_pin_cfg = {
//         .mode = TUYA_GPIO_PULLUP,
//         .direct = TUYA_GPIO_INPUT,
//         .level = TUYA_GPIO_LEVEL_HIGH
//     };
//     TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SDA1_PIN, &in_pin_cfg));

//     // TUYA_GPIO_BASE_CFG_T pin_cfg;   
//     // pin_cfg.mode = TUYA_GPIO_PULLUP;
//     // pin_cfg.direct = TUYA_GPIO_INPUT;
//     // pin_cfg.level = TUYA_GPIO_LEVEL_HIGH;
//     // tkl_gpio_init(SC7A20_SDA1_PIN, &pin_cfg);

// }
// STATIC VOID_T sda_set_output(VOID_T)
// {
//     OPERATE_RET rt = OPRT_OK;
//     /*GPIO output init*/
//     TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
//         .mode = TUYA_GPIO_PULLUP,
//         .direct = TUYA_GPIO_OUTPUT,
//         .level = TUYA_GPIO_LEVEL_HIGH
//     };
//     TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SDA1_PIN, &out_pin_cfg));
//     sda_output_high();

//     // TUYA_GPIO_BASE_CFG_T pin_cfg;   
//     // pin_cfg.mode = TUYA_GPIO_PUSH_PULL;
//     // pin_cfg.direct = TUYA_GPIO_OUTPUT;
//     // pin_cfg.level = TUYA_GPIO_LEVEL_LOW;    
//     // tkl_gpio_init(SC7A20_SDA1_PIN, &pin_cfg);
// }
// STATIC UINT8_T sda_read_level(VOID_T)
// {
//     TUYA_GPIO_LEVEL_E read_level = 0;
//     OPERATE_RET rt = OPRT_OK;
//     TUYA_CALL_ERR_LOG(tkl_gpio_read(SC7A20_SDA1_PIN, &read_level));
//     if(read_level == 1) {
//         // TAL_PR_DEBUG("GPIO read high level");
//         return 1u;
//     } else {
//         // TAL_PR_DEBUG("GPIO read low level");
//         return 0u;
//     }

//     // TUYA_GPIO_LEVEL_E level;
//     // tkl_gpio_read(SC7A20_SDA1_PIN, &level);
//     // return level;

// }
// // STATIC VOID_T sw_i2c_delay(VOID_T)
// // {
// //     volatile UINT_T i = SW_I2C_DELAY_US * TICK_US;       
// //     while (i--);    
// // }

// /**
//  * @brief 软件I2C GPIO初始化
//  */
// STATIC VOID_T sc7a20_sw_i2c_gpio_init(VOID_T)
// {
//     OPERATE_RET rt = OPRT_OK;
//     /*GPIO output init*/
//     TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
//         .mode = TUYA_GPIO_PULLUP,
//         .direct = TUYA_GPIO_OUTPUT,
//         .level = TUYA_GPIO_LEVEL_HIGH
//     };
//     TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SDA2_PIN, &out_pin_cfg));
//     TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SCL2_PIN, &out_pin_cfg));


//     // TUYA_GPIO_BASE_CFG_T pin_cfg = {
//     //     .mode = TUYA_GPIO_PUSH_PULL,
//     //     .direct = TUYA_GPIO_OUTPUT,
//     //     .level = TUYA_GPIO_LEVEL_LOW
//     // };

//     // tkl_gpio_init(SC7A20_SCL1_PIN, &pin_cfg);
//     // tkl_gpio_init(SC7A20_SDA1_PIN, &pin_cfg);
// }
// // I2C 起始信号 S
// STATIC VOID_T sw_i2c_start(VOID_T)
// {
//     sda_output_high();
//     scl_output_high();
//     sw_i2c_delay();
//     sda_output_low();
//     sw_i2c_delay();
//     scl_output_low();
// }

// // I2C 停止信号 P
// STATIC VOID_T sw_i2c_stop(VOID_T)
// {
//     sda_output_low();
//     scl_output_high();
//     sw_i2c_delay();
//     sda_output_high();
//     sw_i2c_delay();
// }


// /**
//  * @brief I2C发送1字节
//  * @param dat 发送数据
//  * @retval IGNORE_ACK_MODE=1 永远返回0；0=ACK应答，1=NACK无应答
//  */
// STATIC UINT8_T sw_i2c_send_byte(UINT8_T dat)
// {
//     UINT8_T i;
//     // 发送8bit数据
//     for(i = 0; i < 8; i++)
//     {
//         if(dat & 0x80)
//             sda_output_high();
//         else
//             sda_output_low();
//         sw_i2c_delay();
//         scl_output_high();
//         sw_i2c_delay();
//         scl_output_low();
//         dat <<= 1;
//     }

//     // 第9个时钟：ACK位处理
//     sda_set_input();
//     sw_i2c_delay();
//     scl_output_high();
//     sw_i2c_delay();

// #if IGNORE_ACK_MODE
//     // 【满足你的需求：不读取ACK电平，直接跳过，强制继续传输】
//     UINT8_T ack_ret = 0;
// #else
//     // 标准模式：读取SDA判断ACK/NACK
//     UINT8_T ack_ret = sda_read_level();
// #endif

//     scl_output_low();
//     // SDA切回输出模式
//     sda_set_output();
//     return ack_ret;
// }

// /**
//  * @brief I2C读取1字节
//  * @param ack_flag 1=主机发ACK(继续读) 0=主机发NACK(停止读)
//  */
// STATIC UINT8_T sw_i2c_read_byte(UINT8_T ack_flag)
// {
//     UINT8_T i, dat = 0;
//     sda_set_input();
//     sw_i2c_delay();
//     for(i = 0; i < 8; i++)
//     {
//         scl_output_high();
//         sw_i2c_delay();
//         dat <<= 1;
//         if(sda_read_level())
//             dat |= 0x01;
//         scl_output_low();
//         sw_i2c_delay();
//     }
//     // 第9位 主机输出ACK/NACK
//     sda_set_output();
//     if(ack_flag)
//         scl_output_high();
//     else
//         sda_output_low();
//     sw_i2c_delay();
//     scl_output_high();
//     sw_i2c_delay();
//     scl_output_low();
//     sda_output_high();
//     return dat;
// }

// //==================== I2C寄存器读写封装 ====================
// /**
//  * @brief 写单个寄存器
//  */
// STATIC UINT8_T sc7a20_write_reg(UINT8_T reg, UINT8_T val)
// {
//     // sw_i2c_start();
//     // // 发送7bit地址+写位
//     // if(sw_i2c_send_byte((SC7A20_DEV_ADDR << 1) | 0x00))
//     // {
//     //     sw_i2c_stop();
//     //     return 1;
//     // }
//     // // 寄存器地址
//     // if(sw_i2c_send_byte(reg))
//     // {
//     //     sw_i2c_stop();
//     //     return 1;
//     // }
//     // // 写入数值
//     // if(sw_i2c_send_byte(val))
//     // {
//     //     sw_i2c_stop();
//     //     return 1;
//     // }
//     // sw_i2c_stop();
//     // return 0;


    
//     UCHAR_T      write_buff[2] = {reg, val};
//     // UCHAR_T      read_buf[I2C_READ_BUFLEN] = {0};
//     SW_I2C_MSG_T i2c_msg = {0};

//     /*IIC write data*/
//     i2c_msg.addr  = SC7A20_DEV_ADDR;
//     i2c_msg.flags = SW_I2C_FLAG_WR;
//     i2c_msg.buff  = write_buff;
//     i2c_msg.len   = 2;//I2C_WRITE_BUFLEN;
//     tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_0, &i2c_msg);
//     return 0;
// }

// /**
//  * @brief 连续读取多个寄存器
//  */
// STATIC UINT8_T sc7a20_read_multi_reg(UINT8_T reg, UINT8_T *buf, UINT8_T len)
// {
//     // UINT8_T i;
//     // sw_i2c_start();
//     // // 写地址+寄存器
//     // if(sw_i2c_send_byte((SC7A20_DEV_ADDR << 1) | 0x00))
//     // {
//     //     sw_i2c_stop();
//     //     return 1;
//     // }
//     // if(sw_i2c_send_byte(reg))
//     // {
//     //     sw_i2c_stop();
//     //     return 1;
//     // }
//     // // 重复起始Sr
//     // sw_i2c_start();
//     // // 读地址
//     // if(sw_i2c_send_byte((SC7A20_DEV_ADDR << 1) | 0x01))
//     // {
//     //     sw_i2c_stop();
//     //     return 1;
//     // }
//     // // 循环读取数据
//     // for(i = 0; i < len; i++)
//     // {
//     //     if(i == len - 1)
//     //         buf[i] = sw_i2c_read_byte(0); // 最后一字节NACK
//     //     else
//     //         buf[i] = sw_i2c_read_byte(1); // 非最后一字节ACK
//     // }
//     // sw_i2c_stop();





//     SW_I2C_MSG_T i2c_msg = {0};

//     /*IIC write data*/
//     i2c_msg.addr  = SC7A20_DEV_ADDR;
//     i2c_msg.flags = SW_I2C_FLAG_WR;
//     i2c_msg.buff  = &reg;
//     i2c_msg.len   = 1;//I2C_WRITE_BUFLEN;
//     tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_0, &i2c_msg);

//     /*IIC read data*/
//     i2c_msg.addr  = SC7A20_DEV_ADDR;
//     i2c_msg.flags = SW_I2C_FLAG_RD;
//     i2c_msg.buff  = buf;
//     i2c_msg.len   = len;//I2C_READ_BUFLEN;
//     tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_0, &i2c_msg);

//     return 0;
// }

// //==================== 传感器上层业务函数 ====================
// STATIC UINT8_T sc7a20_check_id(VOID_T)
// {
//     UINT8_T id;
//     if(sc7a20_read_multi_reg(SC7A20_WHO_AM_I, &id, 1))
//     {
//         // TAL_PR_NOTICE("id0 = %.2X",id);
//         return 1;
//     }
//     // TAL_PR_NOTICE("id = %.2X",id);
//     if(id == 0x11)
//         return 0;
//     return 1;
// }

// STATIC UINT8_T sc7a20_init(UINT8_T odr, UINT8_T fs)
// {
//     if(sc7a20_check_id())
//         return 1;
//     // CTRL1 开启XYZ三轴 + 设置采样率
//     sc7a20_write_reg(SC7A20_CTRL1, odr | 0x07);
//     // CTRL4 设置量程
//     sc7a20_write_reg(SC7A20_CTRL4, fs);
//     // 关闭滤波、中断
//     sc7a20_write_reg(SC7A20_CTRL2, 0x00);
//     return 0;
// }



// STATIC UINT8_T sc7a20_read_acc(SC7A20_DATA_T *acc_buf, UINT8_T fs)
// {
//     UINT8_T buf[6];
//     int16_t raw_x, raw_y, raw_z;
//     UINT8_T i = 0;
//     float sens = get_sensitivity(fs);

//     // TAL_PR_NOTICE("start");
//     // if(sc7a20_read_multi_reg(SC7A20_OUT_X_L, buf, 6))
//     //     return 1;
//     for(i=0; i<6; i++)
//     {
//         if(sc7a20_read_multi_reg(SC7A20_OUT_X_L+i, &buf[i], 1))
//             return 1;
//     }

//     // TAL_PR_NOTICE("buf = %.2X %.2X %.2X %.2X %.2X %.2X",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
//     // 12bit左对齐，右移4位
//     raw_x = (int16_t)((buf[1] << 8) | buf[0]) >> 4;
//     raw_y = (int16_t)((buf[3] << 8) | buf[2]) >> 4;
//     raw_z = (int16_t)((buf[5] << 8) | buf[4]) >> 4;

//     acc_buf->raw_x = raw_x;
//     acc_buf->raw_y = raw_y;
//     acc_buf->raw_z = raw_z;

//     // mg转g
//     acc_buf->g_x = raw_x * sens / 1000.0f;
//     acc_buf->g_y = raw_y * sens / 1000.0f;
//     acc_buf->g_z = raw_z * sens / 1000.0f;

//     float ax = raw_x / sens;
//     float ay = raw_y / sens;
//     float az = raw_z / sens;

//     // 3. 计算倾斜角度
//     pitch_out = atan2f(ax, sqrtf(ay*ay + az*az)) * RAD_TO_DEG;    
//     float roll_out  = atan2f(ay, sqrtf(ax*ax + az*az)) * RAD_TO_DEG;
//     float direct_out  = atan2f(az, sqrtf(ax*ax + ay*ay)) * RAD_TO_DEG;    
//     TAL_PR_NOTICE("Extra pitch_out = %.2f     roll_out = %.2f     direct_out=%.2f",pitch_out,roll_out,direct_out);

//     return 0;
// }














//=========================================================================================
/**
 * @brief 写单个寄存器
 */
STATIC UINT8_T sc7a20B_write_reg(UINT8_T reg, UINT8_T val)
{        
    UCHAR_T      write_buff[2] = {reg, val};
    SW_I2C_MSG_T i2c_msg = {0};

    /*IIC write data*/
    i2c_msg.addr  = SC7A20_DEV_ADDR;
    i2c_msg.flags = SW_I2C_FLAG_WR;
    i2c_msg.buff  = write_buff;
    i2c_msg.len   = 2;//I2C_WRITE_BUFLEN;
    tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_0, &i2c_msg);
    return 0;
}

/**
 * @brief 连续读取多个寄存器
 */
STATIC UINT8_T sc7a20B_read_multi_reg(UINT8_T reg, UINT8_T *buf, UINT8_T len)
{    
    SW_I2C_MSG_T i2c_msg = {0};

    /*IIC write data*/
    i2c_msg.addr  = SC7A20_DEV_ADDR;
    i2c_msg.flags = SW_I2C_FLAG_WR;
    i2c_msg.buff  = &reg;
    i2c_msg.len   = 1;//I2C_WRITE_BUFLEN;
    tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_0, &i2c_msg);

    /*IIC read data*/
    i2c_msg.addr  = SC7A20_DEV_ADDR;
    i2c_msg.flags = SW_I2C_FLAG_RD;
    i2c_msg.buff  = buf;
    i2c_msg.len   = len;//I2C_READ_BUFLEN;
    tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_0, &i2c_msg);
    return 0;
}

//==================== 传感器上层业务函数 ====================
STATIC UINT8_T sc7a20B_check_id(VOID_T)
{
    UINT8_T id;
    if(sc7a20B_read_multi_reg(SC7A20_WHO_AM_I, &id, 1))
    {
        // TAL_PR_NOTICE("id0 = %.2X",id);
        return 1;
    }
    // TAL_PR_NOTICE("id = %.2X",id);
    if(id == 0x11)
        return 0;
    return 1;
}

STATIC UINT8_T sc7a20B_init(UINT8_T odr, UINT8_T fs)
{
    if(sc7a20B_check_id())
        return 1;
    // CTRL1 开启XYZ三轴 + 设置采样率
    sc7a20B_write_reg(SC7A20_CTRL1, odr | 0x07);
    // CTRL4 设置量程
    sc7a20B_write_reg(SC7A20_CTRL4, fs);
    // 关闭滤波、中断
    sc7a20B_write_reg(SC7A20_CTRL2, 0x00);
    return 0;
}

STATIC float gcalculate_average (float pre, float cur)
{
    if (pre > 90)
    {
        pre = cur;
    }    
    return (pre*0.6+cur*0.4);
}

STATIC UINT8_T sc7a20B_read_acc(SC7A20_DATA_T *acc_buf, UINT8_T fs)
{
    UINT8_T buf[6];
    int16_t raw_x, raw_y, raw_z;
    UINT8_T i = 0;
    float sens = get_sensitivity(fs);

    static float ax_pre = 0;
    static float ay_pre = 0;
    static float az_pre = 0;

    // TAL_PR_NOTICE("start");
    // if(sc7a20_read_multi_reg(SC7A20_OUT_X_L, buf, 6))
    //     return 1;
    for(i=0; i<6; i++)
    {
        if(sc7a20B_read_multi_reg(SC7A20_OUT_X_L+i, &buf[i], 1))
            return 1;
        tal_system_sleep(1);
    }

    // TAL_PR_NOTICE("buf = %.2X %.2X %.2X %.2X %.2X %.2X",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
    // 12bit左对齐，右移4位
    raw_x = (int16_t)((buf[1] << 8) | buf[0]) >> 4;
    raw_y = (int16_t)((buf[3] << 8) | buf[2]) >> 4;
    raw_z = (int16_t)((buf[5] << 8) | buf[4]) >> 4;

    acc_buf->raw_x = raw_x;
    acc_buf->raw_y = raw_y;
    acc_buf->raw_z = raw_z;

    // mg转g
    // acc_buf->g_x = raw_x * sens / 1000.0f;
    // acc_buf->g_y = raw_y * sens / 1000.0f;
    // acc_buf->g_z = raw_z * sens / 1000.0f;

    float ax = raw_x * 0.001f;
    float ay = raw_y * 0.001f;
    float az = raw_z * 0.001f;


     //一阶低通滤波
     ax_pre = ax_pre*(1.0f - SC7A20_FILTER_K) + ax * SC7A20_FILTER_K;
     ay_pre = ay_pre*(1.0f - SC7A20_FILTER_K) + ay * SC7A20_FILTER_K;
     az_pre = az_pre*(1.0f - SC7A20_FILTER_K) + az * SC7A20_FILTER_K;

    // 3. 计算倾斜角度
    //x
    // pitch_out = atan2f(ay_pre, az_pre) * RAD_TO_DEG;  
    // //y
    // roll_out  = atan2f(-ax_pre, sqrtf(ay_pre*ay_pre + az_pre*az_pre)) * RAD_TO_DEG;

    // //z
    // direct_out = atan2f(-az_pre, sqrtf(ax_pre*ax_pre + ay_pre*ay_pre)) * RAD_TO_DEG;
    // TAL_PR_NOTICE("prd-1[%.2f  %.2f  %.2f   = %.2f]",pitch_out,roll_out,az_pre,direct_out);


    pitch_out = atan2f(ax_pre, sqrtf(ay_pre*ay_pre + az_pre*az_pre)) * RAD_TO_DEG;    
    roll_out  = atan2f(ay_pre, sqrtf(ax_pre*ax_pre + az_pre*az_pre)) * RAD_TO_DEG;
    direct_out  = atan2f(az_pre, sqrtf(ax_pre*ax_pre + ay_pre*ay_pre)) * RAD_TO_DEG;

    if(roll_out<0)
    {
        if(direct_out<0)
        {
            direct_out = -180- direct_out;
        }
        else
        {
            direct_out = 180- direct_out;
        }
    }
    
    // TAL_PR_NOTICE("angle = %.2f",direct_out);

    // TAL_PR_NOTICE("prd[%.2f  %.2f  %.2f   = %.2f]",pitch_out,roll_out,az_pre,direct_out);


    return 0;
}




VOID_T hugo_ai_position_init(VOID_T)
{
    // sc7a20_sw_i2c_gpio_init();


    OPERATE_RET  op_ret = OPRT_OK;

    // //外接陀螺仪
    // /*i2c init*/
    // SW_I2C_GPIO_T sw_i2c_gpio = {
    //     .scl = SC7A20_SCL1_PIN,
    //     .sda = SC7A20_SDA1_PIN,
    // };
    // op_ret = tdd_sw_i2c_init(SW_I2C_PORT_NUM_0, sw_i2c_gpio);
    // if(OPRT_OK != op_ret) {
    //     TAL_PR_ERR("err<%d>,i2c init fail!", op_ret);
    // }


    //板载陀螺仪
    /*i2c init*/
    SW_I2C_GPIO_T sw_i2c2_gpio = {
        .scl = SC7A20_SCL2_PIN,
        .sda = SC7A20_SDA2_PIN,
    };
    op_ret = tdd_sw_i2c_init(SW_I2C_PORT_NUM_0, sw_i2c2_gpio);
    if(OPRT_OK != op_ret) {
        TAL_PR_ERR("err<%d>,i2c init fail!", op_ret);
    }

}

VOID_T hugo_ai_position_process(VOID_T)
{
    SC7A20_DATA_T acc_data;
    // 初始化传感器 100Hz ±2G
    // if(0 == sc7a20_init(ODR_100HZ, FS_2G))
    // {
    //     // 循环读取加速度
        
    //     sc7a20_read_acc(&acc_data, FS_2G);
    //     // 打印数据
    //     // TAL_PR_NOTICE("ACC X=%.2f g Y=%.2f g Z=%.2f g", acc_data.g_x, acc_data.g_y, acc_data.g_z);
    // }
    // else
    // {
    //     TAL_PR_ERR("Extra SC7A20 init failed!");
    //     tal_system_sleep(1000);
    //     // return;
    // }


    if((moto_flag != 0)||(firsrun_flag != 0))
    {
        // 初始化传感器 100Hz ±2G
        if(0 == sc7a20B_init(ODR_100HZ, FS_2G))
        {
            // 循环读取加速度            
            sc7a20B_read_acc(&acc_data, FS_2G);
            // 打印数据
            // TAL_PR_NOTICE("ACC X=%.2f g Y=%.2f g Z=%.2f g", acc_data.g_x, acc_data.g_y, acc_data.g_z);
        }
        else
        {
            pitch_out = 360;
            TAL_PR_ERR("Board SC7A20 init failed!");
            // tal_system_sleep(500);
            // return;
        }
        // tal_system_sleep(20);
        if (firsrun_flag > 0)
            firsrun_flag--;

            // tal_system_sleep(500);
    }
    
}









