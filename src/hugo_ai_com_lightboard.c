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
#include "hugo_ai_com_lightboard.h"
#include "hugo_ai_desktop.h"
#include "hugo_ai_face.h"

#include "tdd_sw_i2c.h"

#include <math.h>

// #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define LIGHTBOARD_SDA_PIN           TUYA_GPIO_NUM_7
#define LIGHTBOARD_SCL_PIN           TUYA_GPIO_NUM_23

/***********************************************************
***********************typedef define***********************
***********************************************************/
#define LIGHTBOARD_TICK_US             19

#define LIGHTBOARD_I2C_DELAY_US     20    // I2C时序延时，5us稳定
#define IGNORE_ACK_MODE     0    // 1=无视ACK强制发完所有字节  0=标准检测ACK

// #define RAD_TO_DEG 57.2957795f  // 弧度转角度系数 180/π

/***********************************************************
***********************variable define**********************
***********************************************************/
#define sda_output_high()   tkl_gpio_write(LIGHTBOARD_SDA_PIN, TUYA_GPIO_LEVEL_HIGH)
#define sda_output_low()    tkl_gpio_write(LIGHTBOARD_SDA_PIN, TUYA_GPIO_LEVEL_LOW)
#define scl_output_high()   tkl_gpio_write(LIGHTBOARD_SCL_PIN, TUYA_GPIO_LEVEL_HIGH)
#define scl_output_low()    tkl_gpio_write(LIGHTBOARD_SCL_PIN, TUYA_GPIO_LEVEL_LOW)


#define lightboard_i2c_delay() \
do { \
    volatile UINT_T i = LIGHTBOARD_I2C_DELAY_US * LIGHTBOARD_TICK_US; \
    while (i--); \
} while(0)

/***********************************************************
***********************function define**********************
***********************************************************/

//==================== I2C寄存器读写封装 ====================
/**
 * @brief 写单个寄存器
 */

UINT8_T time_valid_flag = 0;
UINT8_T time_dis_flag = 0;
POSIX_TM_S tm = {0};
STATIC UINT8_T buf[10] = {0x00};
STATIC UINT8_T buff[10] = {0x00};



extern UINT8_T flag_rgb_data;
extern UINT8_T flag_seg_data;
extern UINT8_T flag_turn_off_on_state;

STATIC UINT8_T lightboard_write_data(UINT8_T cmd,UINT8_T *pbuf, UINT8_T len)
{
    // UINT8_T         i = 0;
    // UCHAR_T         write_buff[20] = {0};
    // SW_I2C_MSG_T    i2c_msg = {0};

    // write_buff[0] = cmd;
    // for(i=0; i<len; i++)
    // {
    //     write_buff[i+1] = pbuf[i];
    // }

    // /*IIC write data*/
    // i2c_msg.addr  = LIGHTBOARD_DEV_ADDR;
    // i2c_msg.flags = SW_I2C_FLAG_WR | SW_I2C_FLAG_NO_READ_ACK;
    // i2c_msg.buff  = write_buff;
    // i2c_msg.len   = len+1;//I2C_WRITE_BUFLEN;
    // tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_2, &i2c_msg);
    // return 0;



    UINT8_T i = 0;
    UINT8_T j = 0;
    UINT8_T data;
    // UINT8_T buff[20];    
 
    buff[0] = 0xAA;
    buff[1] = cmd;
    buff[2] = len;
    for(i=0;i<len;i++)
    {
        buff[i+3] = *(pbuf+i);
    }
    buff[len+3]=GetCRC(buff,len+3);

    for(i=0;i<len+4;i++)
    // for(i=0;i<4;i++)
    {
        data = buff[i];
        for(j=0; j<8; j++)
        {
            if(data & 0x80)
            {
                sda_output_high();
            }
            else
            {
                sda_output_low();
            }
            scl_output_low();
            lightboard_i2c_delay();
            // tal_system_sleep(1);
            scl_output_high();
            lightboard_i2c_delay();
            // tal_system_sleep(1);
            data <<= 1;
        }
    }
    tal_system_sleep(400);
    return 0;

}

/**
//  * @brief 连续读取多个寄存器
//  */
// STATIC UINT8_T lightboard_read_multi_reg(UINT8_T reg, UINT8_T *buf, UINT8_T len)
// {
//     SW_I2C_MSG_T i2c_msg = {0};

//     /*IIC write data*/
//     i2c_msg.addr  = LIGHTBOARD_DEV_ADDR;
//     i2c_msg.flags = SW_I2C_FLAG_WR;
//     i2c_msg.buff  = &reg;
//     i2c_msg.len   = 1;//I2C_WRITE_BUFLEN;
//     tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_2, &i2c_msg);

//     /*IIC read data*/
//     i2c_msg.addr  = LIGHTBOARD_DEV_ADDR;
//     i2c_msg.flags = SW_I2C_FLAG_RD;
//     i2c_msg.buff  = buf;
//     i2c_msg.len   = len;//I2C_READ_BUFLEN;
//     tdd_sw_i2c_xfer(SW_I2C_PORT_NUM_2, &i2c_msg);
//     return 0;
// }





VOID_T hugo_ai_lightboard_init(VOID_T)
{



    // // lightboard_DATA_T acc_data;
    // // lightboard_lightboard_i2c_gpio_init();


    // OPERATE_RET  op_ret = OPRT_OK;
    // // USHORT_T     temper = 0, humi = 0;

    // //灯板通信
    // /*i2c init*/
    // SW_I2C_GPIO_T sw_i2c_gpio = {
    //     .scl = LIGHTBOARD_SCL_PIN,
    //     .sda = LIGHTBOARD_SDA_PIN,
    // };
    // op_ret = tdd_sw_i2c_init(SW_I2C_PORT_NUM_2, sw_i2c_gpio);
    // if(OPRT_OK != op_ret) {
    //     TAL_PR_ERR("err<%d>,i2c init fail!", op_ret);
    // }   



    OPERATE_RET rt = OPRT_OK;
    /*GPIO output init*/
    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PULLUP,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_HIGH
    };
    tkl_gpio_init(LIGHTBOARD_SDA_PIN, &out_pin_cfg);
    tkl_gpio_init(LIGHTBOARD_SCL_PIN, &out_pin_cfg);
    sda_output_high();
    scl_output_high();

}


STATIC VOID_T hugo_ai_seg_reflash_time(VOID_T)
{
    // POSIX_TM_S tm = {0};
    STATIC INT_T s_last_min  = -1, s_last_hour = -1;

    if (tal_time_check_time_sync() != OPRT_OK ||
    tal_time_check_time_zone_sync() != OPRT_OK) 
    {
        // return;
    }
    if (tal_time_get_local_time_custom(0, &tm) != OPRT_OK) 
    {
        // return;
    }
    if(tm.tm_sec % 5 != 0)
    {
        time_valid_flag = 0;
        return;
    }
    // if ((tm.tm_min == s_last_min)&&(tm.tm_hour == s_last_hour))
    // {
    //     time_valid_flag = 0;
    //     return;
    // }

    time_valid_flag = 1;
    s_last_min = tm.tm_min;
    s_last_hour = tm.tm_hour;
    // segdata[0] = tm.tm_hour/10;
    // // if (segdata[0] == 0)
    // // {
    // //     segdata[0] = 16;
    // // }
    // segdata[1] = tm.tm_hour%10;
    // segdata[2] = tm.tm_min/10;
    // segdata[3] = tm.tm_min%10;
}


VOID_T hugo_ai_lightboard_process(VOID_T)
{
    // lightboard_DATA_T acc_data;
    
    // UINT8_T buf[10] = {0x12,0x34,0x03,0x04,0x05,0x06};

    hugo_ai_seg_reflash_time();
    if ((time_valid_flag == 1)&&(flag_turn_off_on_state != 2))
    {
        // hugo_ai_seg_reflash_time();
        buf[0] = 0x01;
        buf[1] = tm.tm_hour;
        buf[2] = tm.tm_min;
        // buf[1] = 0x10;
        // buf[2] = 0x23;
        // TAL_PR_NOTICE("time: %.2d:%.2d",buf[1],buf[2]);
        lightboard_write_data(0x01,buf,3);
        tal_system_sleep(300);
        lightboard_write_data(0x01,buf,3);
        time_dis_flag = 0;
        // buf[0] = 6;
        // lightboard_write_data(0x02,buf,1);
    }
    else if((flag_seg_data == 3)&&(flag_turn_off_on_state == 2))
    {
        buf[0] = 0x03;
        buf[1] = 0x00;
        buf[2] = 0x00;
        lightboard_write_data(0x01,buf,3);
        tal_system_sleep(300);
        lightboard_write_data(0x01,buf,3);
        flag_seg_data = 0;
    }
    else if(flag_rgb_data!=0xFF)
    {
        // TAL_PR_INFO("=== flag_rgb_data=%d",flag_rgb_data);
        lightboard_write_data(0x02,&flag_rgb_data,1);
        tal_system_sleep(300);
        lightboard_write_data(0x02,&flag_rgb_data,1);
        flag_rgb_data = 0xFF;
    }

    
}