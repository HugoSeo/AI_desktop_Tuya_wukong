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

#include <math.h>

// #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define SC7A20_SDA_PIN           TUYA_GPIO_NUM_14
#define SC7A20_SCL_PIN           TUYA_GPIO_NUM_16


/***********************************************************
***********************typedef define***********************
***********************************************************/
#define TICK_US             19

#define SW_I2C_DELAY_US     5    // I2C时序延时，5us稳定
#define IGNORE_ACK_MODE     0    // 1=无视ACK强制发完所有字节  0=标准检测ACK

#define RAD_TO_DEG 57.2957795f  // 弧度转角度系数 180/π

/***********************************************************
***********************variable define**********************
***********************************************************/





/***********************************************************
***********************function define**********************
***********************************************************/
STATIC VOID_T sda_output_high(VOID_T)
{
    tkl_gpio_write(SC7A20_SDA_PIN, TUYA_GPIO_LEVEL_HIGH);
}
STATIC VOID_T sda_output_low(VOID_T)
{
    tkl_gpio_write(SC7A20_SDA_PIN, TUYA_GPIO_LEVEL_LOW);
}
STATIC VOID_T scl_output_high(VOID_T)
{
    tkl_gpio_write(SC7A20_SCL_PIN, TUYA_GPIO_LEVEL_HIGH);
}
STATIC VOID_T scl_output_low(VOID_T)
{
    tkl_gpio_write(SC7A20_SCL_PIN, TUYA_GPIO_LEVEL_LOW);
}
STATIC VOID_T sda_set_input(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    /*GPIO input init*/
    TUYA_GPIO_BASE_CFG_T in_pin_cfg = {
        .mode = TUYA_GPIO_PULLUP,
        .direct = TUYA_GPIO_INPUT,
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SDA_PIN, &in_pin_cfg));
}
STATIC VOID_T sda_set_output(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    /*GPIO output init*/
    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PULLUP,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_HIGH
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SDA_PIN, &out_pin_cfg));
}
STATIC UINT8_T sda_read_level(VOID_T)
{
    TUYA_GPIO_LEVEL_E read_level = 0;
    OPERATE_RET rt = OPRT_OK;
    TUYA_CALL_ERR_LOG(tkl_gpio_read(SC7A20_SDA_PIN, &read_level));
    if(read_level == 1) {
        // TAL_PR_DEBUG("GPIO read high level");
        return 1u;
    } else {
        // TAL_PR_DEBUG("GPIO read low level");
        return 0u;
    }
}
STATIC VOID_T sw_i2c_delay(VOID_T)
{
    volatile UINT_T i = SW_I2C_DELAY_US * TICK_US;       
    while (i--);    
}

/**
 * @brief 软件I2C GPIO初始化
 */
STATIC VOID_T sc7a20_sw_i2c_gpio_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    /*GPIO output init*/
    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PULLUP,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_HIGH
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SDA_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SC7A20_SCL_PIN, &out_pin_cfg));
}
// I2C 起始信号 S
STATIC VOID_T sw_i2c_start(VOID_T)
{
    sda_output_high();
    scl_output_high();
    sw_i2c_delay();
    sda_output_low();
    sw_i2c_delay();
    scl_output_low();
}

// I2C 停止信号 P
STATIC VOID_T sw_i2c_stop(VOID_T)
{
    sda_output_low();
    scl_output_high();
    sw_i2c_delay();
    sda_output_high();
    sw_i2c_delay();
}


/**
 * @brief I2C发送1字节
 * @param dat 发送数据
 * @retval IGNORE_ACK_MODE=1 永远返回0；0=ACK应答，1=NACK无应答
 */
STATIC UINT8_T sw_i2c_send_byte(UINT8_T dat)
{
    UINT8_T i;
    // 发送8bit数据
    for(i = 0; i < 8; i++)
    {
        if(dat & 0x80)
            sda_output_high();
        else
            sda_output_low();
        sw_i2c_delay();
        scl_output_high();
        sw_i2c_delay();
        scl_output_low();
        dat <<= 1;
    }

    // 第9个时钟：ACK位处理
    sda_set_input();
    sw_i2c_delay();
    scl_output_high();
    sw_i2c_delay();

#if IGNORE_ACK_MODE
    // 【满足你的需求：不读取ACK电平，直接跳过，强制继续传输】
    UINT8_T ack_ret = 0;
#else
    // 标准模式：读取SDA判断ACK/NACK
    UINT8_T ack_ret = sda_read_level();
#endif

    scl_output_low();
    // SDA切回输出模式
    sda_set_output();
    return ack_ret;
}

/**
 * @brief I2C读取1字节
 * @param ack_flag 1=主机发ACK(继续读) 0=主机发NACK(停止读)
 */
STATIC UINT8_T sw_i2c_read_byte(UINT8_T ack_flag)
{
    UINT8_T i, dat = 0;
    sda_set_input();
    for(i = 0; i < 8; i++)
    {
        scl_output_high();
        sw_i2c_delay();
        dat <<= 1;
        if(sda_read_level())
            dat |= 0x01;
        scl_output_low();
        sw_i2c_delay();
    }
    // 第9位 主机输出ACK/NACK
    sda_set_output();
    if(ack_flag)
        scl_output_high();
    else
        sda_output_low();
    sw_i2c_delay();
    scl_output_high();
    sw_i2c_delay();
    scl_output_low();
    sda_output_high();
    return dat;
}

//==================== I2C寄存器读写封装 ====================
/**
 * @brief 写单个寄存器
 */
STATIC UINT8_T sc7a20_write_reg(UINT8_T reg, UINT8_T val)
{
    sw_i2c_start();
    // 发送7bit地址+写位
    if(sw_i2c_send_byte((SC7A20_DEV_ADDR << 1) | 0x00))
    {
        sw_i2c_stop();
        return 1;
    }
    // 寄存器地址
    if(sw_i2c_send_byte(reg))
    {
        sw_i2c_stop();
        return 1;
    }
    // 写入数值
    if(sw_i2c_send_byte(val))
    {
        sw_i2c_stop();
        return 1;
    }
    sw_i2c_stop();
    return 0;
}

/**
 * @brief 连续读取多个寄存器
 */
STATIC UINT8_T sc7a20_read_multi_reg(UINT8_T reg, UINT8_T *buf, UINT8_T len)
{
    UINT8_T i;
    sw_i2c_start();
    // 写地址+寄存器
    if(sw_i2c_send_byte((SC7A20_DEV_ADDR << 1) | 0x00))
    {
        sw_i2c_stop();
        return 1;
    }
    if(sw_i2c_send_byte(reg))
    {
        sw_i2c_stop();
        return 1;
    }
    // 重复起始Sr
    sw_i2c_start();
    // 读地址
    if(sw_i2c_send_byte((SC7A20_DEV_ADDR << 1) | 0x01))
    {
        sw_i2c_stop();
        return 1;
    }
    // 循环读取数据
    for(i = 0; i < len; i++)
    {
        if(i == len - 1)
            buf[i] = sw_i2c_read_byte(0); // 最后一字节NACK
        else
            buf[i] = sw_i2c_read_byte(1); // 非最后一字节ACK
    }
    sw_i2c_stop();
    return 0;
}

//==================== 传感器上层业务函数 ====================
STATIC UINT8_T sc7a20_check_id(VOID_T)
{
    UINT8_T id;
    if(sc7a20_read_multi_reg(SC7A20_WHO_AM_I, &id, 1))
        return 1;
    if(id == 0x11)
        return 0;
    return 1;
}

STATIC UINT8_T sc7a20_init(UINT8_T odr, UINT8_T fs)
{
    if(sc7a20_check_id())
        return 1;
    // CTRL1 开启XYZ三轴 + 设置采样率
    sc7a20_write_reg(SC7A20_CTRL1, odr | 0x07);
    // CTRL4 设置量程
    sc7a20_write_reg(SC7A20_CTRL4, fs);
    // 关闭滤波、中断
    sc7a20_write_reg(SC7A20_CTRL2, 0x00);
    return 0;
}

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

STATIC UINT8_T sc7a20_read_acc(SC7A20_DATA_T *acc_buf, UINT8_T fs)
{
    UINT8_T buf[6];
    int16_t raw_x, raw_y, raw_z;
    UINT8_T i = 0;
    float sens = get_sensitivity(fs);


    // if(sc7a20_read_multi_reg(SC7A20_OUT_X_L, buf, 6))
    //     return 1;
    for(i=0; i<6; i++)
    {
        if(sc7a20_read_multi_reg(SC7A20_OUT_X_L+i, &buf[i], 1))
            return 1;
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
    acc_buf->g_x = raw_x * sens / 1000.0f;
    acc_buf->g_y = raw_y * sens / 1000.0f;
    acc_buf->g_z = raw_z * sens / 1000.0f;

    float ax = raw_x / sens;
    float ay = raw_y / sens;
    float az = raw_z / sens;

    // 3. 计算倾斜角度
    float pitch_out = atan2f(ax, sqrtf(ay*ay + az*az)) * RAD_TO_DEG;    
    float roll_out  = atan2f(ay, sqrtf(ax*ax + az*az)) * RAD_TO_DEG;
    float direct_out  = atan2f(az, sqrtf(ax*ax + ay*ay)) * RAD_TO_DEG;
    // TAL_PR_NOTICE("pitch_out = %.2f     roll_out = %.2f     direct_out=%f",pitch_out,roll_out,direct_out);

    return 0;
}


VOID_T hugo_ai_position_process(VOID_T)
{
    SC7A20_DATA_T acc_data;
    sc7a20_sw_i2c_gpio_init();
    // 初始化传感器 100Hz ±2G
    if(0 == sc7a20_init(ODR_100HZ, FS_2G))
    {
        // 循环读取加速度
        while(1)
        {
            sc7a20_read_acc(&acc_data, FS_2G);
            // 打印数据
            // TAL_PR_NOTICE("ACC X=%.2f g Y=%.2f g Z=%.2f g", acc_data.g_x, acc_data.g_y, acc_data.g_z);
            tal_system_sleep(100);
        }
    }
    else
    {
        TAL_PR_ERR("SC7A20 init failed!");
        // tal_system_sleep(1000);
    }
    
}