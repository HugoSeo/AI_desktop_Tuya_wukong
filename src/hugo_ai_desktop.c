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
#include "hugo_ai_desktop.h"
#include "hugo_ai_face.h"
#include "tuya_uf_db.h"
// #include <string.h>
// #include "hugo_ai_position_sensor.h"

// #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
// #define ADC_NUM       TUYA_ADC_NUM_0
// #define ADC_CHANNEL   14
#define ADC_NUM                 TUYA_ADC_NUM_0
#define ADC_CHANNEL_A           1
#define ADC_CHANNEL_B           4
#define ADC_CHANNEL_C           6
#define ADC_CHANNEL_D           14

#define LED_CTRL_PIN            TUYA_GPIO_NUM_50
#define TOUCH_KEY_PIN           TUYA_GPIO_NUM_15

// SEG define
#define SEG_A_PIN               TUYA_GPIO_NUM_42
#define SEG_B_PIN               TUYA_GPIO_NUM_50
#define SEG_C_PIN               TUYA_GPIO_NUM_49
#define SEG_D_PIN               TUYA_GPIO_NUM_55
#define SEG_E_PIN               TUYA_GPIO_NUM_53
#define SEG_F_PIN               TUYA_GPIO_NUM_43
#define SEG_G_PIN               TUYA_GPIO_NUM_54
#define SEG_DP_PIN              TUYA_GPIO_NUM_6

#define SEG_1_PIN               TUYA_GPIO_NUM_24
#define SEG_2_PIN               TUYA_GPIO_NUM_22
#define SEG_3_PIN               TUYA_GPIO_NUM_23
#define SEG_4_PIN               TUYA_GPIO_NUM_7

// Moto define
#define MOTO_ADC_PIN            TUYA_GPIO_NUM_13
#define MOTO_ADC_CHANNEL        15
#define MOTO_A_PIN              TUYA_GPIO_NUM_18    //black wire
#define MOTO_B_PIN              TUYA_GPIO_NUM_19    //yellow wire
// #define MOTO_C_PIN              TUYA_GPIO_NUM_49    //brown wire
// #define MOTO_D_PIN              TUYA_GPIO_NUM_47    //blue wire
// #define MOTO_NSLEEP_PIN          TUYA_GPIO_NUM_17    //nSleep wire
// #define MOTO_NFAULT_PIN              TUYA_GPIO_NUM_17    //nSleep wire

// uart define
#define Maxdatalen 300

// uf define
#define AI_POWER_STATUS_FLAG_PATH   "pw_flg"

STATIC UINT8_T uart_rxbuff[Maxdatalen] = {0x00};
STATIC UINT8_T uart_txbuff[Maxdatalen] = {0x00};
STATIC UINT16_T uart_Rxln = 0;

STATIC UINT8_T flag_moto_motion = 0;
// STATIC UINT8_T flag_mcu_uart_ack = 0;
// STATIC UINT8_T flag_move_cmd = 0;
STATIC UINT8_T flag_turn_off_on_cmd = 0;
STATIC UINT8_T flag_shut_voice = 0;
UINT8_T flag_turn_off_on_state = 2;
STATIC UINT8_T flag_config_voic = 0;
STATIC UINT8_T getdata[4]={0};
STATIC UINT8_T wakeflag={0};
STATIC UINT8_T getnamestr[31]={0};

STATIC UINT16_T radar_distance = 0;


UINT8_T flag_rgb_data = RGB_OFF;
UINT8_T flag_seg_data = 0;
// STATIC UINT8_T segdata[4]={16,16,16,16};
// STATIC UINT8_T segdp_flg = 0;
// STATIC UINT8_T time_valid_flag = 0;

UINT8_T moto_flag = 0;
STATIC float  moto_angle = 0;
STATIC UINT8_T moto_nod_times = 0;
STATIC UINT8_T moto_state = 0;
STATIC UINT8_T moto_cur_state = 0;
STATIC UINT16_T moto_time = 0;
STATIC UINT16_T hold_time = 0;
STATIC UINT16_T adc_check_time = 0;
STATIC UINT16_T seg_dis_time = 0;
STATIC UINT16_T rgb_dis_time = 0;

UINT8_T demo_test_state = 0;
STATIC UINT16_T demo_test_time = 0;
STATIC UINT8_T demo_test_flag = 0;
STATIC UINT8_T demo_test_cmd = 0;

STATIC UINT8_T  radar_flag_valid = 0;

STATIC UINT8_T moto_step = 0;
// STATIC UINT8_T moto_

STATIC WF_STATION_STAT_E net_state={0};


//                            0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F   NOP
CONST UINT8_T seg_code[] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F,0x77,0x7C,0x39,0x5E,0x79,0x71,0x00};

// BYTE_T flag_gateway_state = 0;

// typedef struct led_st {
// beken_timer_t led_timer;
// DEV_STATE state;
// GPIO_INDEX gpio_idx;
// } LED_ST, LED_PTR;

// typedef struct led_message {
// DEV_STATE led_msg;
// } LED_MSG_T;

// static LED_ST ledctr;
// beken_queue_t led_msg_que = NULL;
// xTaskHandle led_thread_handle = NULL;

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/
// STATIC TUYA_ADC_BASE_CFG_T sg_adc_cfg = {
//     .ch_list.data = 1<<ADC_CHANNEL,
//     .ch_nums = 1,    //adc Number of channel lists
//     .width = 12,
//     .mode = TUYA_ADC_CONTINUOUS,
//     .type = TUYA_ADC_INNER_SAMPLE_VOL,
//     .conv_cnt = 1,
// };

extern QUEUE_HANDLE  s_queue_voice_cmd;
// extern QUEUE_HANDLE  s_queue_state;
extern QUEUE_HANDLE  s_queue_name_str;
extern QUEUE_HANDLE  s_queue_wake;
extern QUEUE_HANDLE  s_queue_test;

extern float pitch_out;
extern float roll_out;
extern float direct_out;

extern UINT8_T time_dis_flag;


extern OPERATE_RET hugo_ai_set_free_idle(VOID);
extern OPERATE_RET hugo_ai_set_free_wakeup(VOID);
// extern unsigned int  FACE_TIME;

/***********************************************************
***********************function define**********************
***********************************************************/

/**
* @brief a thread OPERATE_RET adc port and read the adc very 2 seconds
*
* @param[in] param:Task parameters
* @return none
*/
// VOID example_adc(INT_T argc, CHAR_T *argv[])
// {
//     OPERATE_RET rt = OPRT_OK;
//     INT32_T adc_value = 0;

//     /* ADC 0 channel 2 init */
//     TUYA_CALL_ERR_GOTO(tkl_adc_init(ADC_NUM, &sg_adc_cfg), __EXIT);

//     TUYA_CALL_ERR_LOG(tkl_adc_read_single_channel(ADC_NUM, ADC_CHANNEL, &adc_value));    
//     TAL_PR_DEBUG("ADC%d value = %d", ADC_NUM, adc_value);    
//     TUYA_CALL_ERR_LOG(tkl_adc_deinit(ADC_NUM));

// __EXIT:
//     return;
// }
// VOID adc_get(VOID)
// {
//     OPERATE_RET rt = OPRT_OK;
//     INT32_T adc_value = 0;

//     TUYA_CALL_ERR_LOG(tkl_adc_read_single_channel(ADC_NUM, ADC_CHANNEL, &adc_value));
//     // TAL_PR_DEBUG("ADC%d value = %d", ADC_NUM, adc_value&0x0FFF);
//     TAL_PR_DEBUG("ADC%d[%d] get Volt = %d", ADC_NUM, adc_value, adc_value/2*3300/4096);
//     tkl_adc_read_voltage(ADC_NUM, &adc_value, 1);
//     TAL_PR_DEBUG("ADC ch%d voltage = %d mV", ADC_CHANNEL, adc_value);
//     // TUYA_CALL_ERR_LOG(tkl_adc_deinit(ADC_NUM));
//     return;
// }

// OPERATE_RET ret_gateway_state(VOID)
// {
//     OPERATE_RET rt = OPRT_OK;
//     // return flag_gateway_state;
//     return rt;
// }


//flag_turn_off_on_state

VOID power_flag_write(VOID)
{
    INT_T  rt = OPRT_OK;
    uFILE *fp = NULL;
    INT_T  cnt = 0;

    fp = ufopen(AI_POWER_STATUS_FLAG_PATH, "w+");
    if (NULL == fp) {
        TAL_PR_ERR("uf file %s can't open and read data!", AI_POWER_STATUS_FLAG_PATH);
        return OPRT_NOT_EXIST;
    }

    if (0 != ufseek(fp, 0, UF_SEEK_SET)) {
        ufclose(fp);
        TAL_PR_ERR("uf file %s Set file offset to 0 error!", AI_POWER_STATUS_FLAG_PATH);
        return OPRT_NOT_EXIST;
    }

    cnt = ufwrite(fp, &flag_turn_off_on_state, 1);
    if (cnt != 1) {
        TAL_PR_ERR("uf file %s write data error!", AI_POWER_STATUS_FLAG_PATH);
    }

    rt = ufclose(fp);

    if (rt != OPRT_OK) {
        TAL_PR_ERR("uf file %s close error!", AI_POWER_STATUS_FLAG_PATH);
        return rt;
    }
    return rt;


}
VOID power_flag_read(VOID)
{
    INT_T    rt = OPRT_OK;
    uFILE   *fp = NULL;
    INT_T    cnt = 0;
    UINT8_T  data = 0;

    fp = ufopen(AI_POWER_STATUS_FLAG_PATH, "r+");
    if (NULL == fp) {
        TAL_PR_ERR("uf file %s can't open and read data!", AI_POWER_STATUS_FLAG_PATH);
        return OPRT_NOT_EXIST;
    }

    TAL_PR_DEBUG("uf open OK");
    cnt = ufread(fp, &data, 1);
    if((data == 1)||(data == 2))
    {
        flag_turn_off_on_state = data;
    }
    TAL_PR_DEBUG("uf file %s  flag_turn_off_on_state=%.2X!", AI_POWER_STATUS_FLAG_PATH, flag_turn_off_on_state);

    rt = ufclose(fp);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("uf file %s close error!", AI_POWER_STATUS_FLAG_PATH);
        return rt;
    }
    return rt;

}






// //uart 
VOID mcu_uart_init(VOID)
{
    
    TAL_UART_CFG_T cfg;
    UINT_T baud = 115200;
    UINT32_T bufsz = 128;
    memset(&cfg, 0, sizeof(TAL_UART_CFG_T));
    cfg.base_cfg.baudrate = baud;
    cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
    cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg.rx_buffer_size = bufsz;

    tal_uart_init(TUYA_UART_NUM_0, &cfg);

}

VOID mcu_uart_rx(VOID)
{
    OPERATE_RET cnt = 0;
    UINT8_T data[Maxdatalen];
    cnt = tal_uart_read(TUYA_UART_NUM_0,data,Maxdatalen);
    if (cnt > 0)
    {
        
        if (uart_Rxln+cnt < Maxdatalen)
        {
            // uart_rxbuff[uart_Rxln++] = data;
            memcpy(uart_rxbuff+uart_Rxln,data,cnt);
        }
        uart_Rxln+=cnt;

        // TAL_PR_INFO("=============%d:%d",cnt,uart_Rxln);
    }
}

UINT8_T get_check_sum(UINT8_T *pack, UINT16_T pack_len)
{
    UINT16_T i;
    UINT8_T check_sum = 0;
    for(i = 0; i < pack_len; i ++)
    {
        check_sum += *pack ++;
    }
    return check_sum;
}

VOID mcu_uart_rx_process(VOID)
{
    UINT16_T i,t ;
    UINT8_T sum1=0,sum2=0;

    mcu_uart_rx();
    for(t=0; t<uart_Rxln; t++)
    {
        if(uart_rxbuff[t]==0xAA&&uart_rxbuff[t+1]==0x55)
        {
            sum1=1;
            sum2=0;
            sum1=uart_rxbuff[(uart_rxbuff[t+4]+uart_rxbuff[t+5])+6+t];
            for(i=0; i<((uart_rxbuff[t+4]+uart_rxbuff[t+5])+6); i++)
            {
                sum2+=uart_rxbuff[t+i];
            }
#ifdef Debug_ENABLE
            //   printf("sum1=%0.2X sum2=%0.2X t+4=%0.2X t+5=%0.2X,t=%0.2X\r\n",sum1,sum2,Lock_uart_rxbuff[t+4],Lock_uart_rxbuff[t+5],t);
#endif
            if(sum1==sum2)
            {                   
                switch(uart_rxbuff[t+3])        //cmd
                {
                case 0x01:  //动作控制应答
                    break;
                case 0x02:  //开关机设置
                    if((uart_rxbuff[t+5]==1)&&(uart_rxbuff[t+6]<=2))
                    {
                        flag_turn_off_on_state = uart_rxbuff[t+6];
                        flag_shut_voice = flag_turn_off_on_state;
                        // power_flag_write();
                        // flag_mcu_uart_ack = 0x02;
                        
                        if(flag_turn_off_on_state == POWER_STATUS_ON)
                        {
                            // seg_dis_time = 30000;
                            flag_seg_data = 1;
                            time_dis_flag = 1;

                            rgb_dis_time = 5000;
                            flag_rgb_data = RGB_WAIT;
                            moto_state = STATE_MOTO_ON;
                            if(demo_test_state!=0)
                                hugo_ai_set_free_wakeup();

                        }
                        else if(flag_turn_off_on_state == POWER_STATUS_OFF)
                        {
                            seg_dis_time = 1000;
                            flag_seg_data = 3;
                            flag_rgb_data = RGB_OFF;
                            moto_state = STATE_MOTO_OFF;

                            hugo_ai_set_free_idle();
                        }
                        
                    }
                    break;                
                case 0x03:
                    // AA 05 00 07 00 03 30 39 37 A9 
                    if(uart_rxbuff[t+5]==3)
                    {

                        if ((uart_rxbuff[t+6]>='0')&&(uart_rxbuff[t+6]<='9')&&(uart_rxbuff[t+7]>='0')&&(uart_rxbuff[t+7]<='9')&&(uart_rxbuff[t+8]>='0')&&(uart_rxbuff[t+8]<='9'))
                        {
                            radar_distance  = (uart_rxbuff[t+6]-'0')*100;                       
                            radar_distance += (uart_rxbuff[t+7]-'0')*10;
                            radar_distance += uart_rxbuff[t+8]-'0';
                            TAL_PR_INFO("radar_distance = %d",radar_distance);
                            if((radar_distance<200)&&(radar_distance>10))
                            {
                                if(radar_flag_valid < 2)
                                {
                                    radar_flag_valid++;
                                
                                }
                                if(radar_flag_valid == 1)   // New get radar
                                {
                                // if(FACE_TIME>5000)
                                // {
                                //     FACE_TIME = 200;
                                // }
                                    hugo_ai_face_intimer();
                                    
                                    
                                    // seg_dis_time = 10000;
                                    flag_seg_data = 1;
                                    time_dis_flag = 1;

                                    rgb_dis_time = 3000;
                                    flag_rgb_data = RGB_SUCEESS;
                                }
                            }
                            else
                            {
                                radar_flag_valid = 0;
                            }
                        }                        
                    }
                    break;
                case 0x04:  //config
                    // AA 55 00 05 00 01 01 06
                    if(uart_rxbuff[t+6] == 0x01)
                    {
                        TAL_PR_NOTICE("resetconfig");
                        // tuya_app_gui_request_dev_unbind();
                        // tuya_iot_wf_gw_reset();
                        // flag_mcu_uart_ack = 0x02;
                        flag_config_voic = 0x01;
                    }
                    break;
                default:
                    break;
                }
                // TAL_PR_INFO("=============Rx MCU[%d]:",uart_Rxln);
                // for(int k=0; k<uart_Rxln; k++)
                // {
                //     TAL_PR_INFO("%.2X",uart_rxbuff[k]);
                // }
                // TAL_PR_INFO("\r\n\r\n");
            }
        }
        // TAL_PR_INFO("=============Rx MCU[%d]:",uart_Rxln);
        //         for(int k=0; k<uart_Rxln; k++)
        //         {
        //             printf("%.2X ",uart_rxbuff[k]);
        //         }
        //         printf("\r\n\r\n");
    }
    for(i=0; i<uart_Rxln; i++) uart_rxbuff[i]=0;
    uart_Rxln = 0;
}
VOID send_mcu_data(UINT8_T command,UINT8_T *data,UINT16_T ln)
{
    UINT8_T t;
    if(ln<=(Maxdatalen-7))
    {
        uart_txbuff[0]=0xAA;
        uart_txbuff[1]=0x55;
        uart_txbuff[2]=0x00;
        uart_txbuff[3]=command;
        uart_txbuff[4]=(ln>>8);
        uart_txbuff[5]=ln;
        if(ln==0)
        {
            uart_txbuff[6]=get_check_sum(uart_txbuff,6);
        }
        else
        {
            for(t=0; t<ln; t++) uart_txbuff[6+t]=data[t];
            uart_txbuff[6+ln]=get_check_sum(uart_txbuff,6+ln);
        }
        // mcu_uart_tx(uart_txbuff,ln+7);
        tal_uart_write(TUYA_UART_NUM_0, uart_txbuff, ln+7);
        // delay_ms(10);
#ifdef Debug_ENABLE
        printf("lock_tx:");
        if(FP_Work_Mode != FP_UP_CHAR_Send&& Lock_Flag<Lock_OTA_Request_ACK)
        {
            for(t=0; t<ln+7; t++) {
                printf("%0.2x",Lock_uart_txbuff[t]);
            }
        }
        else
        {
            printf("%d",data[1]);
        }
        printf("\r\n");
#endif
    }
}
VOID mcu_uart_tx_process(VOID)
{
    UINT8_T data[10];

    
    if(flag_moto_motion!=0)
    {
        send_mcu_data(0x01,&flag_moto_motion,1);
        flag_moto_motion = 0;
    }
    // if(flag_move_cmd != 0)
    // {
    //     send_mcu_data(0x03,&flag_move_cmd,1);
    //     flag_move_cmd = 0;
    // }
    
    if(flag_turn_off_on_cmd != 0)
    {
        // if(flag_turn_off_on_cmd ==3)
        // {
        //     flag_turn_off_on_cmd = 2;
        // }
        send_mcu_data(0x02,&flag_turn_off_on_cmd,1);
        flag_turn_off_on_cmd = 0;
    }

    // if(flag_mcu_uart_ack != 0)
    // {
    //     send_mcu_data(flag_mcu_uart_ack,NULL,0);
    //     flag_mcu_uart_ack = 0;
    // }

    // if(wakeflag == 1)
    // {
    //     send_mcu_data(0x08,&wakeflag,1);
    //     wakeflag = 0;
    // }

    if(demo_test_cmd!=0)
    {
        send_mcu_data(0x05,&demo_test_cmd,1);
        demo_test_cmd = 0;
    }
}

VOID mcu_uart_task(VOID)
{    
    mcu_uart_rx_process();
    mcu_uart_tx_process();
}

STATIC VOID_T hugo_get_back_data(VOID_T *data)
{
    OPERATE_RET rt = OPRT_OK;
}

STATIC VOID_T touchkey_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    /*GPIO input init*/
    TUYA_GPIO_BASE_CFG_T in_pin_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_INPUT,
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(TOUCH_KEY_PIN, &in_pin_cfg));

}

STATIC UINT8_T get_touchkey(VOID_T)
{
    TUYA_GPIO_LEVEL_E read_level = 0;
    OPERATE_RET rt = OPRT_OK;
    TUYA_CALL_ERR_LOG(tkl_gpio_read(TOUCH_KEY_PIN, &read_level));
    if(read_level == 1) {
        // TAL_PR_DEBUG("GPIO read high level");
        return 0;
    } else {
        // TAL_PR_DEBUG("GPIO read low level");
        return 1;
    }
}



// moto control
STATIC UINT8_T hugo_ai_motoadc_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    STATIC TUYA_ADC_BASE_CFG_T sg_adc_cfg = {
        .ch_list.data = (1<<MOTO_ADC_CHANNEL),
        // .ch_list.data = (1<<ADC_CHANNEL_B) ,
        .ch_nums = 1,    //adc Number of channel lists
        .width = 12,
        .mode = TUYA_ADC_CONTINUOUS,
        .type = TUYA_ADC_INNER_SAMPLE_VOL,
        .conv_cnt = 1,
    };

    TUYA_CALL_ERR_GOTO(tkl_adc_init(ADC_NUM, &sg_adc_cfg), __EXIT_ADC);
    return 1;
__EXIT_ADC:
    return 0;
}

VOID hugo_ai_moto_init(VOID)
{
    
    // OPERATE_RET rt = OPRT_OK;
    // /*GPIO output init*/
    // TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
    //     .mode = TUYA_GPIO_PUSH_PULL,
    //     .direct = TUYA_GPIO_OUTPUT,
    //     .level = TUYA_GPIO_LEVEL_LOW
    // };
    // TUYA_CALL_ERR_LOG(tkl_gpio_init(MOTO_A_PIN, &out_pin_cfg));
    // TUYA_CALL_ERR_LOG(tkl_gpio_init(MOTO_B_PIN, &out_pin_cfg));


    OPERATE_RET rt = OPRT_OK;
    UINT_T pwm_duty = 9000;
    /*pwm init*/
    TUYA_PWM_BASE_CFG_T pwm_cfg = {
        .duty = pwm_duty, /* 1-10000 */
        .frequency = PWM_FREQUENCY,
        .polarity  = TUYA_PWM_NEGATIVE,
    };
    TUYA_CALL_ERR_GOTO(tkl_pwm_init(PWM_ID_UP, &pwm_cfg), __EXIT);
    TUYA_CALL_ERR_GOTO(tkl_pwm_init(PWM_ID_DOWN, &pwm_cfg), __EXIT);
    // TUYA_CALL_ERR_GOTO(tkl_pwm_start(PWM_ID_DOWN), __EXIT);

    hugo_ai_motoadc_init();

    __EXIT:
        ;
}
STATIC UINT8_T moto_adc_get(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    INT32_T adc_value = 0;
    STATIC INT32_T volt_cur = 0xFFFFFFFF;
    // STATIC INT32_T volt_pre = 0;
    // UINT8_T i = 0;
    STATIC UINT16_T adc_cnt = 0;

    TUYA_CALL_ERR_LOG(tkl_adc_read_single_channel(ADC_NUM, MOTO_ADC_CHANNEL, &adc_value));
    // TUYA_CALL_ERR_LOG(tkl_adc_read_data(ADC_NUM,&adc_value,1));
    // TAL_PR_DEBUG("ADC%d value = %d", ADC_NUM, adc_value&0x0FFF);

    // if(volt_cur == 0xFFFFFFFF)
    //     volt_cur = adc_value;
    // volt_cur = volt_cur*0.6f+(adc_value/2*3300/4096*0.4f);
     
    TAL_PR_DEBUG("Moto ADC = %d cur = %d", adc_value, adc_value/2*3300/4096,volt_cur);
    
    if(adc_value>195)
    {
        adc_cnt++;
        if(adc_cnt >= 5)
        {
            adc_cnt = 0;
            return FALSE;
        }
    }
    else
    {
        adc_cnt = 0;
    }
    
    // if (adc_value > 90)
    // {
    //     return FALSE;
    // }
    
    return TRUE;
}
STATIC VOID hugo_ai_moto_up(UINT_T pwm_duty)
{
    // TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
    //     .mode = TUYA_GPIO_PUSH_PULL,
    //     .direct = TUYA_GPIO_OUTPUT,
    //     .level = TUYA_GPIO_LEVEL_LOW
    // };
    // tkl_gpio_init(MOTO_B_PIN, &out_pin_cfg);
    // tkl_gpio_write(MOTO_A_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(MOTO_B_PIN, TUYA_GPIO_LEVEL_LOW);
    // TAL_PR_INFO("=====hugo_ai_moto_up");
    OPERATE_RET rt = OPRT_OK;
    // UINT_T pwm_duty = 2500;
    // /*pwm init*/
    TUYA_PWM_BASE_CFG_T pwm_cfg = {
        .duty = pwm_duty, /* 1-10000 */
        .frequency = PWM_FREQUENCY,
        .polarity  = TUYA_PWM_NEGATIVE,
    };
    // TUYA_CALL_ERR_GOTO(tkl_pwm_init(PWM_ID_UP, &pwm_cfg), __EXIT);

    TUYA_CALL_ERR_LOG(tkl_pwm_info_set(PWM_ID_UP, &pwm_cfg));
    TUYA_CALL_ERR_GOTO(tkl_pwm_start(PWM_ID_UP), __EXIT);

    TAL_PR_NOTICE("==================pwm_up");

    __EXIT:
        ;
}
STATIC VOID hugo_ai_moto_down(UINT_T pwm_duty)
{
    // TAL_PR_INFO("=====hugo_ai_moto_down");
    // TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
    //     .mode = TUYA_GPIO_PUSH_PULL,
    //     .direct = TUYA_GPIO_OUTPUT,
    //     .level = TUYA_GPIO_LEVEL_LOW
    // };
    // tkl_gpio_init(MOTO_A_PIN, &out_pin_cfg);

    // tkl_gpio_write(MOTO_NSLEEP_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(MOTO_A_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(MOTO_B_PIN, TUYA_GPIO_LEVEL_HIGH);


    OPERATE_RET rt = OPRT_OK;
    // UINT_T pwm_duty = 2500;
    /*pwm init*/
    TUYA_PWM_BASE_CFG_T pwm_cfg = {
        .duty = pwm_duty, /* 1-10000 */
        .frequency = PWM_FREQUENCY,
        .polarity  = TUYA_PWM_NEGATIVE,
    };
    // TUYA_CALL_ERR_GOTO(tkl_pwm_init(PWM_ID_DOWN, &pwm_cfg), __EXIT);
    TUYA_CALL_ERR_LOG(tkl_pwm_info_set(PWM_ID_DOWN, &pwm_cfg));
    TUYA_CALL_ERR_GOTO(tkl_pwm_start(PWM_ID_DOWN), __EXIT);
    TAL_PR_NOTICE("==================pwm_up");

    // pwm_cfg.duty = 0;
    // TUYA_CALL_ERR_LOG(tkl_pwm_info_set(PWM_ID_UP, &pwm_cfg));
    // TUYA_CALL_ERR_GOTO(tkl_pwm_start(PWM_ID_UP), __EXIT);


    __EXIT:
        ;
}
STATIC VOID hugo_ai_moto_stop(VOID)
{
    // tkl_gpio_write(MOTO_NSLEEP_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(MOTO_A_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(MOTO_B_PIN, TUYA_GPIO_LEVEL_LOW);
    OPERATE_RET rt = OPRT_OK;
    TUYA_CALL_ERR_LOG(tkl_pwm_stop(PWM_ID_UP));
    TUYA_CALL_ERR_LOG(tkl_pwm_stop(PWM_ID_DOWN));
}
VOID hugo_ai_moto_timer(VOID)
{
    if(moto_time > 50)
    {
        moto_time -= 50;
    }
    else
    {
        moto_time = 0;
    }
    if(adc_check_time>50)
    {
        adc_check_time -= 50;
    }
    else
    {
        adc_check_time = 0;
    }

    if(hold_time>50)
    {
        hold_time -= 50;
    }
    else
    {
        hold_time = 0;
    }

    if(seg_dis_time>50)
    {
        seg_dis_time -= 50;
    }
    else
    {
        seg_dis_time = 0;
        flag_seg_data = 3;
    }

    if (rgb_dis_time > 50)
    {
        rgb_dis_time -= 50;
    }
    else
    {
        rgb_dis_time = 0;
        flag_rgb_data=RGB_OFF;
    }

    if(demo_test_state == 0)
    {
        if (demo_test_time>50)
        {
            demo_test_time -= 50;
        }
        else
        {
            demo_test_time = 0;
        }
}
    
}
VOID hugo_ai_moto_process(VOID)
{
    // if((pitch_out > 15)||(pitch_out<-15))
    // {
    //     // moto_state = STATE_MOTO_IDLE;
    //     // moto_flag = MOTO_IDLE;
    //     // return;

    // }



    // STATIC UINT8_T wakeup_cnt = 0;
    // if(moto_state != 0)
    //     moto_cur_state = moto_state;
    // if(moto_state == STATE_MOTO_ON)
    // {
    //     moto_time = 20000;
    //     moto_state = STATE_MOTO_IDLE;
    //     moto_angle = 0;
    //     hold_time = 500;
    // }
    // else if(moto_state == STATE_MOTO_OFF)
    // {
    //     moto_time = 20000;
    //     moto_state = STATE_MOTO_IDLE;
    //     moto_angle = -90;
    //     hold_time = 0;
    // }
    // else if(moto_state == STATE_MOTO_QUIET)
    // {
    //     moto_time = 20000;
    //     moto_state = STATE_MOTO_IDLE;
    //     moto_angle = -90;
    //     hold_time = 0;
    // }
    // else if(moto_state == STATE_MOTO_NOD)
    // {
    //     moto_time = 20000;
    //     moto_state = STATE_MOTO_IDLE;
    //     // wakeup_cnt = moto_nod_times;
    //     hold_time = 0;
    //     moto_flag = MOTO_IDLE;
    // }

    // if((moto_nod_times>0)&&(hold_time==0)&&(moto_flag == MOTO_IDLE))
    // {
    //     if (moto_nod_times % 2 == 0)
    //     {
    //         moto_angle = 30;
    //         hold_time = 500;
    //     }
    //     else
    //     {
    //         moto_angle = -30;
    //         hold_time = 500;
    //     }        
    //     moto_nod_times--;
    //     if(moto_nod_times == 0)
    //     {
    //         moto_angle = 0;
    //         hold_time = 500;
    //     }
    // }
    
    // if(moto_time > 0)
    // {
    //     if(moto_flag == MOTO_IDLE)
    //     {
    //         moto_flag = MOTO_CHECK;
    //         // TAL_PR_INFO("=====set MOTO_CHECK");
    //     }
    // }

    if(moto_state != 0)
        moto_cur_state = moto_state;
    if(moto_state == STATE_MOTO_ON)
    {
        moto_state = STATE_MOTO_IDLE;
        moto_flag = MOTO_RUN_UP;
        // moto_step = 1;
        TAL_PR_INFO("=====set STATE_MOTO_ON");
    }
    else if(moto_state == STATE_MOTO_OFF)
    {

        moto_state = STATE_MOTO_IDLE;
        moto_flag = MOTO_RUN_UP;
        // moto_step = 0;
        TAL_PR_INFO("=====set STATE_MOTO_OFF");
    }
    else if(moto_state == STATE_MOTO_TEST1)
    {
        moto_state = STATE_MOTO_IDLE;
        moto_flag = MOTO_RUN_UP;
    }
    else if(moto_state == STATE_MOTO_TEST2)
    {
        moto_state = STATE_MOTO_IDLE;
        moto_flag = MOTO_RUN_DOWN;
    }
    // else if(moto_state == STATE_MOTO_NOD)
    // {
    //     moto_state = STATE_MOTO_IDLE;
    // }

    // else if(moto_state == STATE_MOTO_NOD)
    // {
    //     moto_time = 300;
    //     moto_state = STATE_MOTO_IDLE;
    //     moto_flag = MOTO_RUN_UP;
    //     moto_step = 1;
    // }



    // if(moto_cur_state == STATE_MOTO_ON)
    // {
    //     if(moto_step == 2)
    //     {
    //         moto_step = 0;
    //         moto_flag = MOTO_RUN_UP;
    //         moto_time = 3000;
    //         hold_time = 200;
    //     }
    // }
    // else if (moto_cur_state == STATE_MOTO_NOD)
    // {
    //     if(moto_step == 2)
    //     {
    //         moto_flag = MOTO_RUN_DOWN;
    //         moto_time = 600;
    //         moto_step = 3;
    //         hold_time = 200;
    //     }
    //     else if(moto_step == 4)
    //     {
    //         moto_flag = MOTO_RUN_UP;
    //         moto_time = 300;
    //         moto_step = 0;
    //         hold_time = 200;
    //     }
    // }
    


}
VOID hugo_ai_moto_task(VOID)
{
    // STATIC INT16_T hold_times = 0;
    STATIC INT8_T run_flag = 0;
    // STATIC float range_angle=180;
    STATIC INT8_T run_direction = 0;

    #if 0
    if((pitch_out > 15)||(pitch_out<-15))
    {
        moto_state = STATE_MOTO_IDLE;
        moto_flag = MOTO_IDLE;
        return;
    }

    if (hold_time != 0)
        return;

    if (moto_flag == MOTO_CHECK)
    {
//         float pitch_out = 0;
// float roll_out = 0;
// float direct_out = 0;
        // if(moto_cur_state == STATE_MOTO_ON)
        // {
        //     range_angle = 150;
        // }


        // if(direct_out>moto_angle+10.0)
        // {
        //     if((moto_cur_state == STATE_MOTO_ON)&&(direct_out>130))
        //     {
        //         run_direction = MOTO_RUN_DOWN;
        //     }
        //     else
        //     {
        //         run_direction = MOTO_RUN_UP;
        //     }            
        // }
        // else if(direct_out<moto_angle-10.0)
        // {
        //     run_direction = MOTO_RUN_DOWN;
        // }
        // else
        // {
        //     run_direction = MOTO_IDLE;
        // }

        if(direct_out>moto_angle+10.0)
        // if(run_direction == MOTO_RUN_UP)
        {
            hugo_ai_moto_up(5000);
            hugo_ai_moto_down(0);
            // TAL_PR_INFO("=====set hugo_ai_moto_up");
            run_flag = 1;
            moto_flag=MOTO_WAIT;
        }
        else if(direct_out<moto_angle-10.0)
        // else if(run_direction == MOTO_RUN_DOWN)
        {
            hugo_ai_moto_down(5000);
            hugo_ai_moto_up(0);
            // TAL_PR_INFO("=====set hugo_ai_moto_down");
            moto_flag=MOTO_WAIT;
            run_flag = -1;
        }
        else
        {
            moto_flag = MOTO_IDLE;
            // TAL_PR_INFO("=====set MOTO_IDLE");
        }
    }
    else if (moto_flag == MOTO_WAIT)
    {

        if((run_flag == 1)&&(direct_out<moto_angle+40)&&(direct_out>moto_angle+10.0))
        {
            hugo_ai_moto_up(1500);
            hugo_ai_moto_down(0);
            run_flag = 2;
        }
        if((run_flag == 1)&&(direct_out>moto_angle-40)&&(direct_out<moto_angle-10.0))
        {
            hugo_ai_moto_down(1500);
            hugo_ai_moto_up(0);
            run_flag = -2;
        }
        if((direct_out>moto_angle-10.0)&&(direct_out<moto_angle+10.0))
        {
            hugo_ai_moto_up(10000);
            hugo_ai_moto_down(10000);
            moto_flag = MOTO_HOLD;
            hold_time = 200;
            // TAL_PR_INFO("=====set MOTO_HOLD");
        }
    }
    else if (moto_flag == MOTO_STOP)
    {
        hugo_ai_moto_stop();
        moto_flag = MOTO_IDLE;
    }
    else if (moto_flag == MOTO_HOLD)
    {
        // hugo_ai_moto_up(10000);
        // hugo_ai_moto_down(10000);
        // if(hold_time>0)
        // {

        // }
        // else
        // {
            moto_flag = MOTO_STOP;
            // TAL_PR_INFO("=====set hold MOTO_STOP");
        // }
    }
    #endif


    switch(moto_flag)
    {

        case MOTO_RUN_UP:
            hugo_ai_moto_up(5500);
            hugo_ai_moto_down(0);
            TAL_PR_INFO("=====set hugo_ai_moto_up");     
            moto_flag=MOTO_RUN_UP_END;
            adc_check_time = 100;
            moto_time = 5000;
            if(moto_cur_state == STATE_MOTO_TEST1)
                moto_time = 600;
        break;
        case MOTO_RUN_UP_END:
            if((adc_check_time==0)&&(moto_adc_get()==FALSE)||(moto_time==0))
            {
                if(moto_cur_state == STATE_MOTO_ON)
                {
                    moto_flag = MOTO_RUN_DOWN;
                    moto_time=3000;
                }
                else
                    moto_flag = MOTO_STOP;
                hugo_ai_moto_stop();
                TAL_PR_INFO("=====ADC MOTO_STOP");
                moto_time = 0;
            }
        break;
        case  MOTO_RUN_DOWN:
            hugo_ai_moto_down(5500);
            hugo_ai_moto_up(0);
            TAL_PR_INFO("=====set hugo_ai_moto_down");
            moto_flag=MOTO_RUN_DOWN_END;
            adc_check_time = 50;
            moto_time = 2200;
            if(moto_cur_state == STATE_MOTO_TEST2)
            moto_time = 600;
        break;
        case MOTO_RUN_DOWN_END:
            if((adc_check_time==0)&&(moto_adc_get()==FALSE)||(moto_time==0))
            {
                // if(moto_cur_state == STATE_MOTO_ON)
                // {
                //     moto_flag = MOTO_RUN_UP;
                //     moto_time=3000;
                // }
                // else  
                moto_flag= MOTO_STOP;
                hugo_ai_moto_stop();
                TAL_PR_INFO("=====ADC MOTO_STOP");
                moto_time = 0;
            }
        break;
        case MOTO_STOP:

        break;
    }




    // if(hold_time!=0)
    //     return;
    // if(moto_flag == MOTO_RUN_UP)
    // {
    //     hugo_ai_moto_up(5000);
    //     hugo_ai_moto_down(0);
    //     TAL_PR_INFO("=====set hugo_ai_moto_up");     
    //     moto_flag=MOTO_WAIT;
    //     adc_check_time = 50;
    // }
    // else if(moto_flag == MOTO_RUN_DOWN)
    // {
    //     hugo_ai_moto_down(5000);
    //     hugo_ai_moto_up(0);
    //     TAL_PR_INFO("=====set hugo_ai_moto_down");
    //     moto_flag=MOTO_WAIT;
    //     adc_check_time = 50;      
    // }
    // else if (moto_flag == MOTO_WAIT)
    // {
    //     if(moto_time==0)
    //     {
    //         moto_time = 0;
    //         hugo_ai_moto_up(10000);
    //         hugo_ai_moto_down(10000);
    //         moto_flag = MOTO_STOP;
    //         hold_time = 200;
    //         // TAL_PR_INFO("=====set MOTO_HOLD");
    //         adc_check_time = 50;
    //     }
    // }
    // else if (moto_flag == MOTO_STOP)
    // {
    //     if(moto_step>0)
    //     {
    //         moto_time = 0;
    //         moto_step++;
    //     }
            
    //     hugo_ai_moto_stop();
    //     moto_flag = MOTO_IDLE;
    // }


    // if((adc_check_time==0)&&(moto_adc_get()==FALSE))
    // {
    //     // if(moto_step>0)
    //     // {
    //     //     moto_time = 0;
    //     //     moto_step++;
    //     //     return;
    //     // }
    //     moto_flag = MOTO_STOP;
    //     // moto_time = 0;
    //     TAL_PR_INFO("=====ADC MOTO_STOP");
    // }
}

// ADC
// P28      P12     P21     P25
// ADC4     ADC14   ADC6    ADC1
STATIC UINT8_T hugo_ai_adc_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    STATIC TUYA_ADC_BASE_CFG_T sg_adc_cfg = {
        .ch_list.data = (1<<ADC_CHANNEL_A) | (1<<ADC_CHANNEL_B) | (1<<ADC_CHANNEL_C) | (1<<ADC_CHANNEL_D),
        // .ch_list.data = (1<<ADC_CHANNEL_A) ,
        .ch_nums = 4,    //adc Number of channel lists
        .width = 12,
        .mode = TUYA_ADC_CONTINUOUS,
        .type = TUYA_ADC_INNER_SAMPLE_VOL,
        .conv_cnt = 1,
    };

    TUYA_CALL_ERR_GOTO(tkl_adc_init(ADC_NUM, &sg_adc_cfg), __EXIT_ADC);
    return 1;
__EXIT_ADC:
    return 0;
}

VOID adc_get(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    INT32_T adc_value[16] = {0};
    UINT8_T i = 0;

    // TUYA_CALL_ERR_LOG(tkl_adc_read_single_channel(ADC_NUM, ADC_CHANNEL, &adc_value));
    TUYA_CALL_ERR_LOG(tkl_adc_read_data(ADC_NUM,adc_value,4));
    // tkl_adc_read_single_channel(ADC_NUM,ADC_CHANNEL_A,&adc_value[0]);
    // tkl_adc_read_single_channel(ADC_NUM,ADC_CHANNEL_B,&adc_value[1]);
    // tkl_adc_read_single_channel(ADC_NUM,ADC_CHANNEL_C,&adc_value[2]);
    // tkl_adc_read_single_channel(ADC_NUM,ADC_CHANNEL_D,&adc_value[3]);
    // TAL_PR_DEBUG("ADC%d value = %d", ADC_NUM, adc_value&0x0FFF);
    // for(i=0;i<4;i++)
    // {        
    //     TAL_PR_INFO("ADC%d[%d] get Volt = %d", i, adc_value[i], adc_value[i]/2*3300/4096);
    // }

    // TAL_PR_INFO("ADC get Volt = [%d:%d %d] [%d:%d %d] [%d:%d %d] [%d:%d %d]",\
        ADC_CHANNEL_A, adc_value[0], adc_value[0]/2*3300/4096,\
        ADC_CHANNEL_B, adc_value[1], adc_value[1]/2*3300/4096,\
        ADC_CHANNEL_C, adc_value[2], adc_value[2]/2*3300/4096,\
        ADC_CHANNEL_D, adc_value[3], adc_value[3]/2*3300/4096\
    );

    // tkl_adc_read_voltage(ADC_NUM, &adc_value, 4);
    // for(i=0;i<4;i++)
    //     TAL_PR_DEBUG("ADC ch%d voltage = %d mV", i, adc_value[i]);
    // TUYA_CALL_ERR_LOG(tkl_adc_deinit(ADC_NUM));
    return;
}

STATIC UINT8_T hugo_ai_adc_task(VOID)
{
    adc_get();
    // tal_system_sleep(300);
}

OPERATE_RET hugo_ai_desktop_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    CONST CHAR_T *audio_data = NULL;
    UINT32_T audio_size = 0;

    // CHAR_T *text;

//     //ADC
//     INT32_T adc_value = 0;

//     /* ADC 0 channel 2 init */
//     TUYA_CALL_ERR_GOTO(tkl_adc_init(ADC_NUM, &sg_adc_cfg), __EXIT);

//     while (1)
//     {
//         adc_get();
//         tal_system_sleep(500);
//     }
// __EXIT:
//     return 1;


    // hugo_ai_adc_init();

    // uart test
    mcu_uart_init();

    //touch key
    // touchkey_init();
    // seg_init();
    // app_led_init();
    Face_Init();

    // power_flag_read();

    // hugo_ai_moto_init();
    // hugo_ai_moto_up();
    // tal_system_sleep(2000);
    // hugo_ai_moto_stop();
    // tal_system_sleep(800);
    // hugo_ai_moto_down();
    // tal_system_sleep(2000);
    // hugo_ai_moto_stop();


    // UINT8_T data[64] = {0x55,0xAA,0x05,0x01,0xAB,0xBC};
    // OPERATE_RET cnt = 0;
    // TAL_PR_NOTICE("hugo mf_user_callback");
    // while (1)
    // {
    //     // tal_uart_write(TUYA_UART_NUM_0, data, 10);
    //     // tal_system_sleep(500);
    //     cnt = tal_uart_read(TUYA_UART_NUM_0,data,1);
    //     if(cnt > 0)
    //     TAL_PR_NOTICE("===%X===",data[0]);
    //     // tal_system_sleep(500);
    // }


    // OPERATE_RET rt = OPRT_OK;
    // CONST CHAR_T *audio_data = NULL;
    // UINT32_T audio_size = 0;

    // audio_data = (CONST CHAR_T*)media_src_dingdong_zh;
    // audio_size = sizeof(media_src_dingdong_zh); 
    // while(1)
    // {
    //     TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
    //     tal_system_sleep(1000);
    // }

    // LED test LED_CTRL_PIN
    // LED_init();
    // LED_reset();

    // UINT32_T data = 0;
    
    TAL_PR_NOTICE("------------------Hugo run------------------");

    // rt = tal_workq_schedule(WORKQ_SYSTEM, hugo_get_back_data, NULL);
    // if (OPRT_OK != rt) {
    //     TAL_PR_ERR("ai toy -> schedule default session creation failed, rt: %d", rt);
    // }
    tal_queue_create_init(&s_queue_voice_cmd, 4*SIZEOF(UINT8_T), 1);
    // tal_queue_create_init(&s_queue_state, SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_name_str, 31*SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_wake, 1*SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_test, 1*SIZEOF(UINT8_T), 1);
    
    // WUKONG_AI_PLAYTTS_T tts_param = {
    //     .text = "网络开小差了，机器人暂时无法联网，仅支持本地按键操作"
    // };
    // wukong_audio_play_tts_url(tts_param,FALSE);

    // moto_flag = STATE_MOTO_ON;
    // moto_state = STATE_MOTO_ON;

    tal_system_sleep(1000);
    tal_queue_post(s_queue_test, &demo_test_state, 0); 

    while (1)
    { 
        tal_wifi_station_get_status(&net_state);
        
        mcu_uart_task();
        // hugo_ai_adc_task();
        
        if((flag_turn_off_on_state == POWER_STATUS_ON)&&(net_state == WSS_GOT_IP))
        {
            hugo_Face_uart_task();
        }

        hugo_ai_moto_process();

        if (tal_queue_fetch(s_queue_voice_cmd, &getdata, 1) == OPRT_OK)
        {
            TAL_PR_NOTICE("------------------get OK------------------");
            // tal_system_sleep(1000);
            TAL_PR_NOTICE("------------------getdata=%d------------------",getdata[0]);
            // tuya_ai_input_start(TRUE);
            // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
            // tuya_ai_input_stop();

            wukong_ai_agent_output_stop(TRUE);
            wukong_audio_player_stop(AI_PLAYER_ALL);
            wukong_audio_input_reset();
            wukong_ai_agent_chat_break(NULL);
            
            switch (getdata[0])
            {
            
            case 1:
                // TAL_PR_NOTICE("------------------get data 2------------------");
                if(getdata[1]<=20)
                {
                    flag_moto_motion = getdata[1];
                    if (flag_moto_motion == 8)
                    {
                        // moto_state = STATE_MOTO_QUIET;
                        TAL_PR_NOTICE("------------Hugo set AI_MODE_OP_NOTIFY_IDLE");
                        // wukong_ai_mode_dispatch(AI_MODE_OP_NOTIFY_IDLE, NULL, 0);
                        // wukong_ai_mode_init();
                        // wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);

                        // //close idle timer
                        // tuya_ai_toy_idle_timer_ctrl(FALSE);
                        // //open low power timer
                        // tuya_ai_toy_lowpower_timer_ctrl(TRUE);
                        // //disable wakeup
                        // wukong_audio_input_wakeup_set(FALSE);
                        hugo_ai_set_free_idle();
                        // wukong_ai_chat_sub_mode_switch();
                        break;
                    }

                }
                break;
            // case 3:
            //     // tuya_ai_input_start(TRUE);
            //     // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
            //     // tuya_ai_input_stop();
            //     if(getdata[1]<=5)
            //     {
            //         if(flag_turn_off_on_state == 2)
            //         {
            //             flag_rgb_data=RGB_OFF;
            //             continue;
            //         }
            //         flag_move_cmd = getdata[1];
            //     }
            //     break;
            case 2:
                // tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
                // tuya_ai_input_stop();
                if(getdata[1]<=2)
                {
                    flag_turn_off_on_cmd = getdata[1];
                    flag_turn_off_on_state = flag_turn_off_on_cmd;
                    TAL_PR_NOTICE("------------------flag_turn_off_on_state=%d------------------",flag_turn_off_on_state);
                    // tal_queue_post(s_queue_state, &flag_turn_off_on_state, 0);
                    
                    flag_shut_voice = flag_turn_off_on_cmd;
                    moto_state = flag_turn_off_on_cmd;

                    // power_flag_write();
                    if(flag_turn_off_on_cmd==2)
                    {
                        // wukong_ai_mode_dispatch(AI_MODE_OP_NOTIFY_IDLE, NULL, 0);
                        // // wukong_ai_mode_init();
                        // // wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);

                        // //close idle timer
                        // tuya_ai_toy_idle_timer_ctrl(FALSE);
                        // //open low power timer
                        // tuya_ai_toy_lowpower_timer_ctrl(TRUE);
                        // //disable wakeup
                        // wukong_audio_input_wakeup_set(FALSE);

                        hugo_ai_set_free_idle();
                    }
                    continue;
                }                
                
                break;
            case 10:
                // tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
                // tuya_ai_input_stop();

                if(flag_turn_off_on_state ==2 )
                {
                    continue;
                }
                if(getdata[1]==1)
                {                    
                    flag_rgb_data=RGB_ON;
                }
                else if(getdata[1]==2)
                {
                    flag_rgb_data=RGB_OFF;
                }
                break;
            case 11:
                if(getdata[1]==1)
                {
                    TAL_PR_NOTICE("------------- getname");
                    if (tal_queue_fetch(s_queue_name_str, &getnamestr, 100) == OPRT_OK)
                    {
                        // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("这个是一个认识的人，打一下招呼")); 

                        // face_voice_flag = 2;
                        TAL_PR_NOTICE("------------- %s",getnamestr);
                        if(face_name_store(getnamestr)==TRUE)
                        {
                            face_voice_flag = 2;
                        }
                        else
                        {

                        }
                    }
                }
                // break;
                continue;
            // case 0xFF:
            default:
                // if (flag_turn_off_on_state == 2)
                // {
                //     tuya_ai_input_start(TRUE);
                //     TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
                //     tuya_ai_input_stop();                    
                // }
                continue;
            }
            if(flag_shut_voice == 2)
                continue;
            audio_data = (CONST CHAR_T*)media_src_bingo2_msc;
            audio_size = sizeof(media_src_bingo2_msc);
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            tuya_ai_input_start(TRUE);
            // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("你收到了一条控制指令，用下面的话术进行应答，不要加任何其他的词包括“好的”和“正在处理”等词，注意只说一遍，直接说：“好嘞，马上安排。”。"));
            // tuya_ai_input_stop();
        }
        if (tal_queue_fetch(s_queue_wake, &wakeflag, 1) == OPRT_OK)
        {
            
            // moto_state = STATE_MOTO_NOD;
            moto_nod_times = 5;
            flag_moto_motion = 9;

            // seg_dis_time = 30000;
            // flag_seg_data = 1;
            // time_dis_flag = 1;

            // rgb_dis_time = 30000;
            // flag_rgb_data = RGB_WAIT;
            TAL_PR_NOTICE("------------------wake up = %d--flag_seg_data=%d----------------",wakeflag,flag_seg_data);

        }
        
        if(flag_config_voic == 1)
        {
            audio_data = (CONST CHAR_T*)media_src_connecting_zh;
            audio_size = sizeof(media_src_connecting_zh);
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            tuya_ai_input_start(TRUE);
            flag_config_voic = 2;
            
        }
        else if (flag_config_voic == 2)
        {
            tal_system_sleep(750);
            flag_config_voic = 3;
        }
        else if(flag_config_voic == 3)
        {
            tuya_iot_wf_gw_reset();
            // tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT);
            flag_config_voic = 0;
        }
        
        if(flag_shut_voice == 1)
        {
            flag_shut_voice = 0;

            if(demo_test_state == 0)
            {
                demo_test_flag = 1;
                demo_test_time = 7200;            
                audio_data = (CONST CHAR_T*)media_src_introduction_1_zh;
                audio_size = sizeof(media_src_introduction_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);

                continue;
            }
            tuya_ai_input_start(TRUE);
            TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("你好，自我介绍一下吧。"));
            tuya_ai_input_stop();

        }
        if(flag_shut_voice == 2)
        {
            flag_shut_voice = 0;
            // audio_data = (CONST CHAR_T*)media_src_dingdong_zh;
            // audio_size = sizeof(media_src_dingdong_zh); 
            // TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            // flag_turn_off_on_cmd = 3;

            // audio_data = (CONST CHAR_T*)media_src_turnoff_remind_2_zh;
            // audio_size = sizeof(media_src_turnoff_remind_2_zh);
            // TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            // tuya_ai_input_start(TRUE);
            if(demo_test_state != 0)
            {
                audio_data = (CONST CHAR_T*)media_src_bingo2_msc;
                audio_size = sizeof(media_src_bingo2_msc);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);
            }
            // moto_state = STATE_MOTO_ON;
            // if(net_state == WSS_GOT_IP)
            // {
            //     tuya_ai_input_start(TRUE);
            //     TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("你收到了一条控制指令，用下面的话术进行应答，不要加任何其他的词包括“好的”和“正在处理”等词，注意只说一遍，直接说：“正在开机，请稍候。”。"));
            //     tuya_ai_input_stop();
            // }
            // else
            // {
            //     audio_data = (CONST CHAR_T*)media_src_waiting_zh;
            //     audio_size = sizeof(media_src_waiting_zh); 
            //     TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            // }
        }
        // if(flag_shut_voice == 2)
        // {
        //     flag_shut_voice = 0;
        //     moto_state = STATE_MOTO_OFF;
        //     // audio_data = (CONST CHAR_T*)media_src_turnoff_zh;
        //     // audio_size = sizeof(media_src_turnoff_zh); 
        //     // TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
        //     if(net_state == WSS_GOT_IP)
        //     {
        //         tuya_ai_input_start(TRUE);
        //         TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("你收到了一条控制指令，用下面的话术进行应答，不要加任何其他的词包括“好的”和“正在处理”等词，直接说：“有需要请叫小康小康唤醒我。”。"));
        //         tuya_ai_input_stop();
        //         // flag_turn_off_on_cmd = 3;
        //     }
        //     else
        //     {
        //         audio_data = (CONST CHAR_T*)media_src_waiting_zh;
        //         audio_size = sizeof(media_src_waiting_zh); 
        //         TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
        //     }
        // }


        if((demo_test_state == 0)&&(demo_test_time == 0)&&(demo_test_flag!=0))
        {            
            if(demo_test_flag == 1)
            {
                audio_data = (CONST CHAR_T*)media_src_introduction_2_zh;
                audio_size = sizeof(media_src_introduction_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5000;//7100
            }
            else if(demo_test_flag == 2)
            {
                audio_data = (CONST CHAR_T*)media_src_introduction_3_zh;
                audio_size = sizeof(media_src_introduction_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 8800;
            }
            else if(demo_test_flag == 3)
            {
                audio_data = (CONST CHAR_T*)media_src_greeting_1_zh;
                audio_size = sizeof(media_src_greeting_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5400;  //7900
                demo_test_cmd = 1;
            }
            else if(demo_test_flag == 4)
            {
                audio_data = (CONST CHAR_T*)media_src_greeting_2_zh;
                audio_size = sizeof(media_src_greeting_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 7100;//9700
            }
            else if(demo_test_flag == 5)
            {
                audio_data = (CONST CHAR_T*)media_src_greeting_3_zh;
                audio_size = sizeof(media_src_greeting_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 12700;
            }
            else if(demo_test_flag == 6)
            {
                audio_data = (CONST CHAR_T*)media_src_breakfast_1_zh;
                audio_size = sizeof(media_src_breakfast_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5000;
                demo_test_cmd = 2;
                moto_state = STATE_MOTO_TEST1;
            }
            else if(demo_test_flag == 7)
            {
                audio_data = (CONST CHAR_T*)media_src_breakfast_2_zh;
                audio_size = sizeof(media_src_breakfast_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5500;
            }
            else if(demo_test_flag == 8)
            {
                audio_data = (CONST CHAR_T*)media_src_breakfast_3_zh;
                audio_size = sizeof(media_src_breakfast_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5500;
            }
            else if(demo_test_flag == 9)
            {
                audio_data = (CONST CHAR_T*)media_src_breakfast_4_zh;
                audio_size = sizeof(media_src_breakfast_4_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 8200;//
            }
            else if(demo_test_flag == 10)
            {
                audio_data = (CONST CHAR_T*)media_src_order_breakfast_1_zh;
                audio_size = sizeof(media_src_order_breakfast_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5800;  //7100
            }
            else if(demo_test_flag == 11)
            {
                audio_data = (CONST CHAR_T*)media_src_order_breakfast_2_zh;
                audio_size = sizeof(media_src_order_breakfast_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 7700;
            }
            else if(demo_test_flag == 12)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_1_zh;
                audio_size = sizeof(media_src_get_moving_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5700;  //7200
                demo_test_cmd = 3;             
                
            }
            else if(demo_test_flag == 13)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_2_zh;
                audio_size = sizeof(media_src_get_moving_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 9000;
            }
            else if(demo_test_flag == 14)
            {
                audio_data = (CONST CHAR_T*)media_src_meeting_targets_zh;
                audio_size = sizeof(media_src_meeting_targets_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 11500;
            }
            else if(demo_test_flag == 15)
            {
                audio_data = (CONST CHAR_T*)media_src_to_get_testing_1_zh;
                audio_size = sizeof(media_src_to_get_testing_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5000;
                flag_moto_motion = 8;
            }
            else if(demo_test_flag == 16)
            {
                audio_data = (CONST CHAR_T*)media_src_to_get_testing_2_zh;
                audio_size = sizeof(media_src_to_get_testing_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 12000;
            }
            else if(demo_test_flag == 17)
            {
                audio_data = (CONST CHAR_T*)media_src_out_of_range_1_zh;
                audio_size = sizeof(media_src_out_of_range_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5000;
            }
            else if(demo_test_flag == 18)
            {
                audio_data = (CONST CHAR_T*)media_src_out_of_range_2_zh;
                audio_size = sizeof(media_src_out_of_range_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5300;
            }
            else if(demo_test_flag == 19)
            {
                audio_data = (CONST CHAR_T*)media_src_out_of_range_3_zh;
                audio_size = sizeof(media_src_out_of_range_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 9600;
            }
            else if(demo_test_flag == 20)
            {
                audio_data = (CONST CHAR_T*)media_src_sedentary_remind_1_zh;
                audio_size = sizeof(media_src_sedentary_remind_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5000;  //5700
                demo_test_cmd = 4;
                moto_state = STATE_MOTO_TEST2;

            }
            else if(demo_test_flag == 21)
            {
                audio_data = (CONST CHAR_T*)media_src_sedentary_remind_2_zh;
                audio_size = sizeof(media_src_sedentary_remind_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 15600;
            }
            else if(demo_test_flag == 22)
            {
                audio_data = (CONST CHAR_T*)media_src_turnoff_remind_1_zh;
                audio_size = sizeof(media_src_turnoff_remind_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 3100;
                flag_turn_off_on_cmd = 2;
                flag_turn_off_on_state = 2;
                moto_state = 2;
            }
            else if(demo_test_flag == 23)
            {
                audio_data = (CONST CHAR_T*)media_src_turnoff_remind_2_zh;
                audio_size = sizeof(media_src_turnoff_remind_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                flag_shut_voice = 2;
                // flag_turn_off_on_cmd = 2;
                demo_test_time = 6000;
            }
            else if(demo_test_flag == 24)
            {
                demo_test_flag = 0;
                demo_test_state = 1;
                tal_queue_post(s_queue_test, &demo_test_state, 0);
            }
            continue;
            
        }


        if(net_state == WSS_GOT_IP)
        {
            if(face_voice_flag == 1)
            {
                face_voice_flag = 0;
                tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("这个是一个陌生人，打一下招呼，问一下对方怎么称呼"));
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("这个是一个陌生人，用下面这个话术进行打招呼，不要加任何其他的词包括“好的”和“正在处理”等词，直接说：“你好呀，我是小康，请问您怎么称呼呀?”。"));
                TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("跟陌生人打个招呼，问一下他叫什么名字。"));

                tuya_ai_input_stop();            
            }
            if(face_voice_flag == 2)
            {
                
                face_voice_flag = 0;
                tuya_ai_input_start(TRUE);
                CHAR_T text[128] = "你好，最近怎么样，我是";
                // CHAR_T text1[16] = ",";
                UINT8_T text_cnt,text_start;  
                text_start = strlen(text);              
                for (text_cnt = 0; text_cnt < 31; text_cnt++)
                {
                    text[text_start+text_cnt] = getnamestr[text_cnt];
                }
                
                // for (text_cnt = 0; text_cnt < 13; text_cnt++)
                // {
                //     text[text_start+text_cnt] = text1[text_cnt];
                // }
                
                // sprintf(text,"这个是一个认识的人，对方是%s，打一下招呼。",getnamestr);
                TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(text));
                TAL_PR_NOTICE("====================face_voice_flag = 2  %s",text);
                tuya_ai_input_stop();
            }
            if(face_voice_flag == 3)
            {
                TAL_PR_NOTICE("====================face_voice_flag = 3");
                if(face_name_get(getnamestr)==TRUE)
                {
                    face_voice_flag = 2;
                }
                else
                {
                    face_voice_flag = 0;
                }
            }
        }

        tal_system_sleep(100);
    }
    tal_queue_free(s_queue_voice_cmd);
    // tal_queue_free(s_queue_state);
    tal_queue_free(s_queue_name_str);
    tal_queue_free(s_queue_wake);
    tal_queue_free(s_queue_test);
    
    return rt;
}

