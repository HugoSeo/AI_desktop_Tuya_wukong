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
// #include "wukong_ai_skills.h"
// #include "skill_cloudevent.h"
#include "wukong_audio_player.h"
#include "hugo_ai_desktop.h"
#include "hugo_ai_face.h"
#include "tuya_uf_db.h"
// #include <string.h>
// #include "hugo_ai_position_sensor.h"

// #include "wukong_ai_skills.h"

#include "wukong_ai_mode.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
// #define ADC_NUM       TUYA_ADC_NUM_0
// #define ADC_CHANNEL   14
#define ADC_NUM                 TUYA_ADC_NUM_0
#define ADC_CHANNEL_A           1
#define ADC_CHANNEL_B           6
#define ADC_CHANNEL_C           14
#define ADC_CHANNEL_D           15

// #define LED_CTRL_PIN            TUYA_GPIO_NUM_50
// #define TOUCH_KEY_PIN           TUYA_GPIO_NUM_15

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
#define Motor_B2_A_Pin          TUYA_GPIO_NUM_28
#define Motor_B2_B_Pin          TUYA_GPIO_NUM_8
#define Motor_B2_C_Pin          TUYA_GPIO_NUM_35
#define Motor_B2_D_Pin          TUYA_GPIO_NUM_34

#define Motor_B2_S_Pin          TUYA_GPIO_NUM_26
// uart define
#define Maxdatalen 300

// uf define
#define AI_POWER_STATUS_FLAG_PATH   "pw_flg"

STATIC UINT8_T uart_rxbuff[Maxdatalen] = {0x00};
STATIC UINT8_T uart_txbuff[Maxdatalen] = {0x00};
STATIC UINT16_T uart_Rxln = 0;

STATIC UINT8_T flag_moto_motion = 0;
STATIC UINT8_T flag_moto_motion_store = 0;
// STATIC UINT8_T flag_mcu_uart_ack = 0;
// STATIC UINT8_T flag_move_cmd = 0;
STATIC UINT8_T flag_turn_off_on_cmd = 0;
STATIC UINT8_T flag_shut_voice = 0;
UINT8_T flag_turn_off_on_state = POWER_STATUS_ON;
STATIC UINT8_T flag_config_voic = 0;
STATIC UINT8_T getdata[4]={0};
STATIC UINT8_T wakeflag=0;
STATIC UINT8_T emoji_num=0xFF;
STATIC UINT16_T emotiong_time = 0;
STATIC UINT8_T turnonkeyflag=0;
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
// STATIC UINT16_T hold_time = 0;
STATIC UINT16_T seg_dis_time = 0;
STATIC UINT_T rgb_dis_time = 0;

UINT8_T demo_test_state = 1;    //0:demo test first run     1:usually run
STATIC UINT16_T demo_test_time = 0;
STATIC UINT8_T demo_test_flag = 0;
STATIC UINT8_T demo_test_cmd = 0;

STATIC UINT8_T  radar_flag_valid = 0;
STATIC UINT16_T radar_check_time = 20000;

STATIC UINT8_T moto_step = 0;
// STATIC UINT8_T moto_


STATIC UINT8_T MOTOR2_UNMB = 0;
STATIC UINT16_T MOTOR2_Cycle = 0;
STATIC UINT8_T MOTO2_STOP_FLG = 0;
// STATIC UINT16_T M2angle = 0;
// STATIC UINT16_T M2SC7A20_Ydata = 0;
STATIC UINT_T nod_time = 0;

STATIC WF_STATION_STAT_E net_state={0};
STATIC BOOL_T online_state = FALSE;

INT32_T adc_value[16] = {0};
INT16_T adc_value_store[4][250] = {0};
INT16_T adc_value_poit = 0;
CHAR_T  adc_check_flag = FALSE;

CHAR_T  keep_quiet = 0;

//                            0    1    2    3    4    5    6    7    8    9    A    B    C    D    E    F   NOP
CONST UINT8_T seg_code[] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F,0x77,0x7C,0x39,0x5E,0x79,0x71,0x00};

INT8_T moto_angle_check(VOID);
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
extern QUEUE_HANDLE  s_queue_emotion;
extern QUEUE_HANDLE  s_queue_name_str;
extern QUEUE_HANDLE  s_queue_wake;
extern QUEUE_HANDLE  s_queue_onvoice;

extern float pitch_out;
extern float roll_out;
extern float direct_out;

extern UINT8_T time_dis_flag;


extern OPERATE_RET hugo_ai_set_free_idle(VOID);
extern OPERATE_RET hugo_ai_set_free_wakeup(VOID);

/***********************************************************
***********************function define**********************
***********************************************************/

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
    CONST CHAR_T *audio_data = NULL;
    UINT32_T audio_size = 0;
    OPERATE_RET rt = OPRT_OK;

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
                    if((uart_rxbuff[t+5]==1)&&(uart_rxbuff[t+6]<=3))
                    {                        
                        flag_turn_off_on_state = uart_rxbuff[t+6];
                        if(flag_turn_off_on_state == POWER_STATUS_DEMO)
                            flag_turn_off_on_state = POWER_STATUS_ON;
                        flag_shut_voice = flag_turn_off_on_state;
                        // power_flag_write();
                        // flag_mcu_uart_ack = 0x02;
                        
                        if(flag_turn_off_on_state == POWER_STATUS_ON)
                        {
                            // seg_dis_time = 30000;
                            flag_seg_data = 1;
                            time_dis_flag = 1;

                            rgb_dis_time = 10000;
                            flag_rgb_data = RGB_WAIT;
                            moto_state = STATE_MOTO_ON;
                            moto_angle = 0;
                            moto_angle_check();
                            // if(demo_test_state!=0)
                            demo_test_state = 0;
                            tal_queue_post(s_queue_onvoice, &demo_test_state, 0);
                            demo_test_state = 1;

                            // hugo_ai_set_free_wakeup();
                            turnonkeyflag = 1;
                            nod_time = 3000;
                        }
                        else if(flag_turn_off_on_state == POWER_STATUS_OFF)
                        {
                            seg_dis_time = 1000;
                            flag_seg_data = 3;
                            flag_rgb_data = RGB_OFF;
                            moto_state = STATE_MOTO_OFF;

                            audio_data = (CONST CHAR_T*)media_src_bingo2_msc;
                            audio_size = sizeof(media_src_bingo2_msc);
                            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                            tuya_ai_input_start(TRUE);
                            flag_shut_voice = 0;
                            for(i=0; i<20; i++)
                            {
                                moto_cur_state = STATE_MOTO_OFF;
                                moto_angle = 20;
                                moto_angle_check();
                                demo_test_flag = 0;
                                demo_test_state = 1;
                                tal_system_sleep(500);
                            }

                            
                            // hugo_ai_set_free_idle();
                        }
                        // else if(flag_turn_off_on_state == POWER_STATUS_DEMO)
                        // {
                        //     flag_seg_data = 1;
                        //     time_dis_flag = 1;

                        //     rgb_dis_time = 600000;//10000;
                        //     flag_rgb_data = RGB_BLUE;
                        //     moto_state = STATE_MOTO_ON;
                        //     demo_test_state = 0;    //demo test mode
                        //     tal_queue_post(s_queue_onvoice, &demo_test_state, 0);
                        // }
                        
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
                            if((radar_distance<300)&&(radar_distance>10))
                            {
                                // radar_check_time = 10000;   //10s后检查雷达
                                if(radar_flag_valid < 2)
                                {
                                    radar_flag_valid++;
                                
                                }
                                if((radar_flag_valid == 1)/*&&(radar_check_time == 0)*/)   // New get radar
                                {                               
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
                case 0xFF:
                // if(uart_rxbuff[t+6] == 0x01)
                {
                    moto_state = STATE_MOTO_NOD+uart_rxbuff[t+6]-1;
                }
                default:
                    break;
                }
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
        flag_moto_motion_store = flag_moto_motion;
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
        flag_moto_motion_store = 0;
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
        flag_moto_motion_store = demo_test_cmd + 0x20;
        send_mcu_data(0x05,&demo_test_cmd,1);
        demo_test_cmd = 0;
    }
}

VOID mcu_uart_task(VOID)
{    
    mcu_uart_rx_process();
    mcu_uart_tx_process();
}



// moto control
// STATIC UINT8_T hugo_ai_motoadc_init(VOID)
// {
//     OPERATE_RET rt = OPRT_OK;
//     STATIC TUYA_ADC_BASE_CFG_T sg_adc_cfg = {
//         .ch_list.data = (1<<MOTO_ADC_CHANNEL),
//         // .ch_list.data = (1<<ADC_CHANNEL_B) ,
//         .ch_nums = 1,    //adc Number of channel lists
//         .width = 12,
//         .mode = TUYA_ADC_CONTINUOUS,
//         .type = TUYA_ADC_INNER_SAMPLE_VOL,
//         .conv_cnt = 1,
//     };

//     TUYA_CALL_ERR_GOTO(tkl_adc_init(ADC_NUM, &sg_adc_cfg), __EXIT_ADC);
//     return 1;
// __EXIT_ADC:
//     return 0;
// }

VOID hugo_ai_moto_init(VOID)
{
    
    OPERATE_RET rt = OPRT_OK;
    /*GPIO output init*/
    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(Motor_B2_A_Pin, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(Motor_B2_B_Pin, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(Motor_B2_C_Pin, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(Motor_B2_D_Pin, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(Motor_B2_S_Pin, &out_pin_cfg));

    moto_power_en();    

}
// STATIC UINT8_T moto_adc_get(VOID)
// {
//     #if 1
//     OPERATE_RET rt = OPRT_OK;
//     INT32_T adc_value = 0;
//     STATIC INT32_T volt_cur = 0xFFFFFFFF;
//     // STATIC INT32_T volt_pre = 0;
//     // UINT8_T i = 0;
//     STATIC UINT16_T adc_cnt = 0;

//     TUYA_CALL_ERR_LOG(tkl_adc_read_single_channel(ADC_NUM, MOTO_ADC_CHANNEL, &adc_value));
//     // TUYA_CALL_ERR_LOG(tkl_adc_read_data(ADC_NUM,&adc_value,1));
//     // TAL_PR_DEBUG("ADC%d value = %d", ADC_NUM, adc_value&0x0FFF);

//     // if(volt_cur == 0xFFFFFFFF)
//     //     volt_cur = adc_value;
//     // volt_cur = volt_cur*0.6f+(adc_value/2*3300/4096*0.4f);
     
//     TAL_PR_DEBUG("Moto ADC = %d cur = %d", adc_value, adc_value/2*3300/4096,volt_cur);
    
//     if(adc_value>195)
//     {
//         adc_cnt++;
//         if(adc_cnt >= 10)
//         {
//             adc_cnt = 0;
//             return FALSE;
//         }
//     }
//     else
//     {
//         adc_cnt = 0;
//     }
//         #endif
//     return TRUE;
// }



//-----------------------------------------------------------------------------------------

//==================================================//
//                     -90                          //
//          up          ↑      -180                 //
//  face   (      0   ←   →                 board   //
//          down        ↓       180                 //
//                      90                          //
//==================================================//

VOID_T moto_stop(VOID)
{
    moto_flag = MOTO_IDLE;
    tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_LOW);
    tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_LOW);
    tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_LOW);
    tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(Motor_B2_S_Pin, TUYA_GPIO_LEVEL_LOW);
}
VOID_T moto_power_en(VOID)
{
    tkl_gpio_write(Motor_B2_S_Pin, TUYA_GPIO_LEVEL_HIGH);
}

// ret: 1,run up;   -1,run down
INT8_T moto_angle_check(VOID)
{
    INT8_T ret = 0;
    float  delta_angle = 0;
    if(((direct_out > moto_angle)&&(direct_out>-150))||((direct_out < -150)&&direct_out < moto_angle))
     {
         moto_flag = MOTO_RUN_UP;
        //  flag_rgb_data=RGB_BLUE;
        if(direct_out < -150)
        {
            delta_angle = (180-moto_angle) + (180+direct_out);
        }
        else
        {
            delta_angle = direct_out-moto_angle;
        }
     }  
   
    else  if(((direct_out < moto_angle)&&(direct_out>-150))||((direct_out < -150)&&direct_out > moto_angle))
    {
        moto_flag = MOTO_RUN_DOWN;
        // flag_rgb_data=RGB_RED;
        if(direct_out < -150)   //没有目标角度在-150~-180的情况，忽略这些情况
        {
            delta_angle = moto_angle-direct_out;
        }
        else
        {
            delta_angle = moto_angle-direct_out;
        }
    }
    else
    {
        moto_flag = MOTO_STOP;
        // flag_rgb_data=RGB_GREEN;
    }    
    if(MOTOR2_Cycle==0)
    {
        // MOTOR2_Cycle = 5000;//3ms*700 = 2100ms
        MOTOR2_Cycle = delta_angle*18;
    }
    tkl_gpio_write(Motor_B2_S_Pin, TUYA_GPIO_LEVEL_HIGH);
    return ret;
}

VOID_T MOTO_B2_RUN(VOID)
{
    switch (moto_flag)
    {
       case MOTO_RUN_UP:   //上转
        // if((direct_out > moto_angle&&direct_out>=0)||(direct_out > moto_angle&&direct_out<=0))
        if(((direct_out > moto_angle)&&(direct_out>-150))||((direct_out < -150)&&direct_out < moto_angle))
        {
            MOTOR2_UNMB++;
            MOTO2_STOP_FLG=0;
            if(MOTOR2_UNMB>3)  MOTOR2_UNMB = 0;
            switch (MOTOR2_UNMB)
            {
            case 3:
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            case 2:
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            case 1:
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            case 0:
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            default:
                break;
            }
        }
        else
        {
           MOTO2_STOP_FLG++;       //if(MOTO2_STOP_FLG<200) 
           if(MOTO2_STOP_FLG>MOTO_MAX) 
            moto_flag=MOTO_STOP;
        }
        break;
    case MOTO_RUN_DOWN: //下转
        // if(direct_out < moto_angle) 
        if(((direct_out < moto_angle)&&(direct_out>-150))||((direct_out < -150)&&direct_out > moto_angle))
        {
            MOTOR2_UNMB++;
            MOTO2_STOP_FLG=0;
            if(MOTOR2_UNMB>3)  MOTOR2_UNMB = 0;
            switch (MOTOR2_UNMB)
            {
            case 0:
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            case 1:
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            case 2:
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            case 3:
                tkl_gpio_write(Motor_B2_B_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_C_Pin, TUYA_GPIO_LEVEL_LOW);
                tkl_gpio_write(Motor_B2_D_Pin, TUYA_GPIO_LEVEL_HIGH);
                tkl_gpio_write(Motor_B2_A_Pin, TUYA_GPIO_LEVEL_HIGH);
                break;
            default:
                break;
            }
        }
        else
        {
            MOTO2_STOP_FLG++;  //if(MOTO2_STOP_FLG<200) 
            if(MOTO2_STOP_FLG>MOTO_MAX)
            moto_flag=MOTO_STOP;
        }
        break;
    case MOTO_STOP:
        moto_stop();
        moto_flag = MOTO_IDLE;
        break;
    default:
        break;
    }
}
VOID hugo_ai_moto_timer(VOID)
{
    if(moto_time > 0)
    {
        moto_time --;
    }
        
    // if(hold_time>0)
    // {
    //     hold_time --;
    // }    

    if(seg_dis_time>0)
    {
        seg_dis_time --;
    }
    else
    {
        seg_dis_time = 0;
        flag_seg_data = 3;
    }

    if (rgb_dis_time > 1)
    {
        rgb_dis_time --;
    }
    else if(rgb_dis_time == 1)
    {
        rgb_dis_time = 0;
        flag_rgb_data=RGB_OFF;
    }

    // if(demo_test_state == 0)
    {
        if (demo_test_time>0)
        {
            demo_test_time --;
        }        
    }

    if(MOTOR2_Cycle > 1)
    {
        MOTOR2_Cycle --;
        if ((MOTOR2_Cycle%500)== 0)
        {
            /* code */
            // if((moto_flag == MOTO_IDLE)&&(moto_cur_state!=STATE_MOTO_OFF))
            if((moto_flag == MOTO_IDLE)&&(abs(direct_out-moto_angle)>5))
                moto_angle_check();
        }
    }
    else 
    {
        if(MOTOR2_Cycle == 1) 
        {
            MOTOR2_Cycle=0;
            moto_stop();
            moto_flag = MOTO_IDLE;
        } 
    }

    if(nod_time > 1)
    {
        nod_time--;        
    }
    else if(nod_time == 1)
    {
        nod_time = 0;
        moto_state = STATE_MOTO_NOD;
    }

    if(radar_check_time > 0)
    {
        radar_check_time--;
    }
    // else if(radar_check_time == 1)  //长时间没有收到雷达数据则无效
    // {
    //     radar_check_time = 0;
    //     radar_flag_valid = 0;
    // }
    
    if(emotiong_time>0) 
    {
        emotiong_time--;
    }
}

//==================================================//
//                     -90                          //
//          up          ↑      -180                 //
//  face   (      0   ←   →                 board   //
//          down        ↓       180                 //
//                      90                          //
//==================================================//
VOID hugo_ai_moto_process(VOID)
{
    // if((pitch_out > 15)||(pitch_out<-15))
    // {
    //     // moto_state = STATE_MOTO_IDLE;
    //     // moto_flag = MOTO_IDLE;
    //     // return;

    // }

    UINT8_T ret = 0;
      // STATIC UINT8_T wakeup_cnt = 0;
    if(moto_state != 0)
    {
        moto_cur_state = moto_state;
        // moto_power_en();
        MOTO2_STOP_FLG = 0;
        MOTOR2_Cycle = 0;
        //moto_flag = MOTO_STOP;
        moto_stop();
        // moto_time = 10000;
         moto_flag = MOTO_CHECK;
        tal_system_sleep(100);
    }

    // if((MOTOR2_Cycle == 0)&&(hold_time == 0)&&(moto_time > 0))
    // {        
    //     if(moto_flag == STATE_MOTO_IDLE)
    //     {
    //         moto_state = moto_cur_state;
    //     }
    // }


    switch (moto_state)
    {
    case STATE_MOTO_ON:
        moto_state = STATE_MOTO_IDLE;
        moto_angle = -40;
        TAL_PR_NOTICE("=============== on A[%d]= %f  %f",ret,direct_out,moto_angle);
        moto_angle_check();        
        break;
    case STATE_MOTO_OFF:
        moto_state = STATE_MOTO_IDLE;
        moto_angle = 80;//80;//150;//75
        moto_angle_check();
        TAL_PR_NOTICE("=============== off A[%d]= %f  %f",ret,direct_out,moto_angle);        
        break;
    case STATE_MOTO_NOD:
        moto_state = STATE_MOTO_IDLE;
        moto_angle = -50;
        moto_angle_check();
        moto_step = 1;
        break;
    case STATE_MOTO_UP:
        moto_state = STATE_MOTO_IDLE;    
        moto_angle = -50;
        moto_angle_check();
        break;
    case STATE_MOTO_DOWN:
        moto_state = STATE_MOTO_IDLE;       
        moto_angle = 20;
        moto_angle_check();
        break;
    default:
        break;
    }  


    if(moto_cur_state == STATE_MOTO_NOD)
    {        
        if(MOTOR2_Cycle == 0)
        {
            /*if((moto_time > 0)&&(moto_step==0))
            {
                moto_flag = MOTO_RUN_UP;        
                moto_angle = -35;
                MOTOR2_Cycle = 1200;
                moto_step++;
                
            }*/
            MOTO2_STOP_FLG = 0;
            // moto_power_en();
            switch (moto_step)
            {
            // case 0:
            case 1:
            case 3:
            case 5:     
                moto_angle = 0;
                moto_angle_check();
                moto_step++;
                break;
            
            case 2:
            case 4:
                moto_angle = -60;
                moto_angle_check();

                moto_step++;
                break;
            case 6:
                // moto_cur_state = STATE_MOTO_IDLE;     
                moto_angle = -35;
                moto_angle_check();
                moto_step = 0;
            default:
                break;
            }
        }
    }
}
// ADC
// P13      P12     P21     P25
// ADC15    ADC14   ADC6    ADC1
STATIC UINT8_T hugo_ai_adc_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    STATIC TUYA_ADC_BASE_CFG_T sg_adc_cfg = {
        .ch_list.data = (1<<ADC_CHANNEL_A) | (1<<ADC_CHANNEL_B) | (1<<ADC_CHANNEL_C) | (1<<ADC_CHANNEL_D),
        // .ch_list.data = (1<<ADC_CHANNEL_B) ,
        .ch_nums = 4,    //adc Number of channel lists
        .width = 12,
        .mode = TUYA_ADC_CONTINUOUS,
        .type = TUYA_ADC_INNER_SAMPLE_VOL,
        .conv_cnt = 1,
    };

    TUYA_CALL_ERR_GOTO(tkl_adc_init(ADC_NUM, &sg_adc_cfg), __EXIT_ADC);
    return 1;
__EXIT_ADC:
    TUYA_CALL_ERR_LOG(tkl_adc_deinit(ADC_NUM));
    return 0;
}

VOID hugo_ai_adc_get(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    // INT32_T adc_value[16] = {0};
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

    // TAL_PR_INFO("ADC get= [%d:%d %d] [%d:%d %d] [%d:%d %d] [%d:%d %d]",\
    //     ADC_CHANNEL_A, adc_value[0], adc_value[0]/2*3300/4096,\
    //     ADC_CHANNEL_B, adc_value[1], adc_value[1]/2*3300/4096,\
    //     ADC_CHANNEL_C, adc_value[2], adc_value[2]/2*3300/4096,\
    //     ADC_CHANNEL_D, adc_value[3], adc_value[3]/2*3300/4096\
    // );

    // tkl_adc_read_voltage(ADC_NUM, &adc_value, 4);
    // for(i=0;i<4;i++)
    //     TAL_PR_DEBUG("ADC ch%d voltage = %d mV", i, adc_value[i]);
    // TUYA_CALL_ERR_LOG(tkl_adc_deinit(ADC_NUM));
    return;
}

STATIC UINT8_T hugo_ai_adc_task(VOID)
{
    unsigned char mic_station_flag = 0;
    unsigned char cnt = 0, i = 0;
    unsigned char end = 0;
    // hugo_ai_adc_init();
    // tal_system_sleep(100);
    INT32_T adc_value_sum[4] = {0};
    INT32_T adc_value_div[4] = {0};
    hugo_ai_adc_get();
    // tal_system_sleep(100);
    // if(adc_value_poit >= 250)
    //     adc_value_poit = 0;
    for(cnt = 0; cnt < 4; cnt++)
    {
        adc_value_store[cnt][adc_value_poit] = adc_value[cnt];
    }
    adc_value_poit++;
    if(adc_value_poit >= 250)
    {
        adc_value_poit = 0;
        for(cnt = 0; cnt < 4; cnt++)
        {
            for(i=0;i++;i<250)
            {
                if(adc_value_div[cnt]<adc_value_store[cnt][i])
                {
                    adc_value_div[cnt]=adc_value_store[cnt][i];
                }
            }
        }
    }


    // // if(adc_check_flag == TRUE)
    // {
    //     adc_check_flag = FALSE;
    //     if(adc_value_poit<50)
    //     {
    //         end = adc_value_poit+200;
    //         for(cnt = 0; cnt < 4; cnt++)
    //         {            
    //             for(i = adc_value_poit+1; i <= end; i++)
    //             {
    //                 // adc_value_sum[cnt] += adc_value_store[cnt][i];
    //                 if(adc_value_div[cnt]<adc_value_store[cnt][i])
    //                 {
    //                     adc_value_div[cnt]=adc_value_store[cnt][i];
    //                 }
    //             }

    //             // for(i = 0; i <= adc_value_poit; i++)
    //             // {
    //             //     adc_value_div[cnt] += adc_value_store[cnt][i];
    //             // }
    //             // for(i = adc_value_poit+201; i < 250; i++)
    //             // {
    //             //     adc_value_div[cnt] += adc_value_store[cnt][i];
    //             // }
    //         }
    //     }
    //     else
    //     {
    //         end = adc_value_poit-50;
    //         for(cnt = 0; cnt < 4; cnt++)
    //         {
    //             for(i = 0; i <= end; i++)
    //             {
    //                 // adc_value_sum[cnt] += adc_value_store[cnt][i];
    //                 if(adc_value_div[cnt]<adc_value_store[cnt][i])
    //                 {
    //                     adc_value_div[cnt]=adc_value_store[cnt][i];
    //                 }
    //             }
    //             for(i = adc_value_poit+1; i < 250; i++)
    //             {
    //                 // adc_value_sum[cnt] += adc_value_store[cnt][i];
    //                 if(adc_value_div[cnt]<adc_value_store[cnt][i])
    //                 {
    //                     adc_value_div[cnt]=adc_value_store[cnt][i];
    //                 }
    //             }
    //             // for(i = adc_value_poit-49; i <= adc_value_poit; i++)
    //             // {
    //             //     adc_value_div[cnt] += adc_value_store[cnt][i];
    //             // }
    //         }
    //     }

        // TAL_PR_INFO("=============adc =0[%d %d %d]  1[%d %d %d]  2[%d %d %d]  3[%d %d %d]",
        //     adc_value_sum[0],adc_value_div[0]*4,adc_value_div[0]*4-adc_value_sum[0],adc_value_sum[1],adc_value_div[1]*4,adc_value_div[1]*4-adc_value_sum[1],
        //     adc_value_sum[2],adc_value_div[2]*4,adc_value_div[2]*4-adc_value_sum[2],adc_value_sum[3],adc_value_div[3]*4,adc_value_div[3]*4-adc_value_sum[3]);

        //test to average
        // TAL_PR_INFO("=============adc =0[%d %d %d]  1[%d %d %d]  2[%d %d %d]  3[%d %d %d]",
        //     adc_value_sum[0],adc_value_sum[0]/200,(adc_value_div[0]+adc_value_sum[0])/250,  adc_value_sum[1],adc_value_sum[1]/200,(adc_value_div[1]+adc_value_sum[1])/250,
        //     adc_value_sum[2],adc_value_sum[2]/200,(adc_value_div[2]+adc_value_sum[2])/250,  adc_value_sum[3],adc_value_sum[3]/200,(adc_value_div[3]+adc_value_sum[3])/250);

        // adc_value_div[0] = adc_value_sum[0]/200 - ADC_REFERENCE_0;
        // adc_value_div[1] = adc_value_sum[0]/200 - ADC_REFERENCE_1;
        // adc_value_div[2] = adc_value_sum[0]/200 - ADC_REFERENCE_2;
        // adc_value_div[3] = adc_value_sum[0]/200 - ADC_REFERENCE_3;


        TAL_PR_INFO("=============adc div = [%d  %d  %d  %d]",adc_value_div[0],adc_value_div[1],adc_value_div[2],adc_value_div[3]);
    // }

    // adc_value_poit++;
}

UINT8_T hugo_radar_valid(VOID)
{
    return radar_flag_valid;
}

OPERATE_RET hugo_ai_desktop_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    CONST CHAR_T *audio_data = NULL;
    UINT32_T audio_size = 0;

    // hugo_ai_adc_init();

    // uart test
    mcu_uart_init();
    Face_Init();

    // power_flag_read();

    hugo_ai_moto_init();
    
    TAL_PR_NOTICE("------------------Hugo run------------------");

    tal_queue_create_init(&s_queue_voice_cmd, 4*SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_emotion, SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_name_str, 31*SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_wake, 1*SIZEOF(UINT8_T), 1);
    tal_queue_create_init(&s_queue_onvoice, 1*SIZEOF(UINT8_T), 1);
    

    while (1)
    { 
        // tal_wifi_station_get_status(&net_state);            //tuya_ai_toy_is_cloud_connected
        online_state = tuya_ai_toy_is_cloud_connected();
        
        mcu_uart_task();
        // hugo_ai_adc_task();
        
        if((flag_turn_off_on_state == POWER_STATUS_ON)&&(online_state == TRUE))
        {
            hugo_Face_uart_task();
        }

        hugo_ai_moto_process();

        if (tal_queue_fetch(s_queue_voice_cmd, &getdata, 1) == OPRT_OK)
        {
            TAL_PR_NOTICE("------------------getdata=%d------------------",getdata[0]);
            // wukong_ai_agent_output_stop(TRUE);
            // wukong_audio_player_stop(AI_PLAYER_ALL);
            // wukong_audio_input_reset();
            // wukong_ai_agent_chat_break(NULL);
            
            switch (getdata[0])
            {
            
            case 1:
                // TAL_PR_NOTICE("------------------get data 2------------------");
                if(getdata[1]<=20)
                {
                    emotiong_time = 30000;
                    flag_moto_motion = getdata[1];
                    if (flag_moto_motion == 8)  //闭嘴
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
                        wukong_ai_agent_output_stop(TRUE);
                        wukong_audio_player_stop(AI_PLAYER_ALL);
                        wukong_audio_input_reset();
                        wukong_ai_agent_chat_break(NULL);
                        // hugo_ai_set_free_idle();
                        // f();
                        keep_quiet = 1;
                        moto_state = STATE_MOTO_ON;
                        nod_time = 4000;
                        break;
                    }
                    if (flag_moto_motion == 9)  //摇头
                    {
                        flag_moto_motion = 0;   //不需要MCU处理
                        moto_state = STATE_MOTO_NOD;
                    }
                    else if((flag_moto_motion >= 6)&&(flag_moto_motion <= 10))
                    {
                        nod_time = 4000;
                    }

                }
                break;
            case 2:
                // tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
                // tuya_ai_input_stop();
                if(getdata[1]<=3)
                {
                    flag_turn_off_on_cmd = getdata[1];
                    flag_turn_off_on_state = flag_turn_off_on_cmd;
                    TAL_PR_NOTICE("------------------flag_turn_off_on_state=%d------------------",flag_turn_off_on_state);
                    
                    flag_shut_voice = flag_turn_off_on_cmd;
                    moto_state = flag_turn_off_on_cmd;

                    // if(flag_turn_off_on_cmd==POWER_STATUS_OFF)
                    // {
                        // wukong_ai_mode_dispatch(AI_MODE_OP_NOTIFY_IDLE, NULL, 0);
                        // // wukong_ai_mode_init();
                        // // wukong_ai_device_mode_switch(AI_DEVICE_MODE_CHAT);

                        // //close idle timer
                        // tuya_ai_toy_idle_timer_ctrl(FALSE);
                        // //open low power timer
                        // tuya_ai_toy_lowpower_timer_ctrl(TRUE);
                        // //disable wakeup
                        // wukong_audio_input_wakeup_set(FALSE);

                        // hugo_ai_set_free_idle();
                    // }
                    // if(flag_turn_off_on_cmd == POWER_STATUS_ON)
                    // {
                    //     demo_test_state = 0; 
                    //     tal_queue_post(s_queue_onvoice, &demo_test_state, 0);
                    //     demo_test_state = 1;
                    // }
                    if(flag_turn_off_on_cmd == POWER_STATUS_DEMO)
                    {
                        flag_seg_data = 1;
                        time_dis_flag = 1;

                        rgb_dis_time = 600000;//10000;
                        flag_rgb_data = RGB_BLUE;
                        moto_state = STATE_MOTO_ON;
                        // hugo_ai_set_free_idle();
                        demo_test_state = 0;    //demo test mode
                        tal_queue_post(s_queue_onvoice, &demo_test_state, 0);   //0 no voice    1 have voice
                        flag_turn_off_on_cmd = 0;
                        keep_quiet = 1;
                        wukong_ai_agent_output_stop(TRUE);
                        wukong_audio_player_stop(AI_PLAYER_ALL);
                        wukong_audio_input_reset();
                        wukong_ai_agent_chat_break(NULL);
                        tal_system_sleep(500);
                    }
                    
                    continue;
                }                
                
                break;
            case 10:
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
            if((flag_shut_voice == 1)||(flag_shut_voice == 2))
                continue;
            audio_data = (CONST CHAR_T*)media_src_bingo2_msc;
            audio_size = sizeof(media_src_bingo2_msc);
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            tuya_ai_input_start(TRUE);
            // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("你收到了一条控制指令，用下面的话术进行应答，不要加任何其他的词包括“好的”和“正在处理”等词，注意只说一遍，直接说：“好嘞，马上安排。”。"));
            // tuya_ai_input_stop();
        }
        if (tal_queue_fetch(s_queue_emotion, &emoji_num, 1) == OPRT_OK)
        {            
            TAL_PR_NOTICE("-------------emo: %d",getdata[1]);

            if(emotiong_time == 0)
            {                
                moto_state = STATE_MOTO_NOD;
                nod_time = 2000;
                emotiong_time = 30000;

                //高兴系列，点头,左右旋转
                if((emoji_num == 0)||(emoji_num == 1)||(emoji_num == 2)||(emoji_num == 3)||(emoji_num == 8)||(emoji_num == 11)||
                (emoji_num == 12)||(emoji_num == 15)||(emoji_num == 16)||(emoji_num == 17)||(emoji_num == 19))
                {               
                    if(flag_moto_motion_store >= 6)
                    {
                        flag_turn_off_on_cmd = POWER_STATUS_ON;
                    }
                    else
                    {
                        flag_moto_motion = 9;
                    }                
                }
                //惊喜系列,左右旋转
                else if((emoji_num == 7)||(emoji_num == 9)||(emoji_num == 10))
                {
                    flag_moto_motion = 9;
                }
                //生气系列
                else if((emoji_num == 5))
                {
                    // flag_moto_motion = 8;
                    demo_test_cmd = 4;
                }
                //伤心系列
                else if((emoji_num == 4)||(emoji_num == 6))
                {
                    flag_moto_motion = 8;//6;
                }
                //躺平系列
                else if((emoji_num == 13)||(emoji_num == 14)||(emoji_num == 18)||(emoji_num == 20))
                {
                    flag_moto_motion = 7;
                }
            }
            
        }

        if (tal_queue_fetch(s_queue_wake, &wakeflag, 1) == OPRT_OK)
        {            
            // moto_state = STATE_MOTO_NOD;            
            // moto_nod_times = 5;
            keep_quiet = 0;
            adc_check_flag = TRUE;
            // if(turnonkeyflag == 1)
            // {
            //     turnonkeyflag = 0;                
            // }
            // else if(flag_moto_motion_store >= 6)
            // {
            //     flag_turn_off_on_cmd = POWER_STATUS_ON;
            //     emotiong_time = 30000;
            //     nod_time = 2000;
            // }
            // else
            // {
            //     if(emotiong_time==0)
            //     {
            //         emotiong_time = 30000;
            //         flag_moto_motion = 9;
            //         nod_time = 2000;
            //     }
            // }

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
            // tuya_iot_wf_gw_reset();
            tuya_iot_wf_gw_fast_unactive(GWCM_OLD, WF_START_SMART_AP_CONCURRENT);
            flag_config_voic = 0;
        }
        
        if(flag_shut_voice == 1)
        {
        //     flag_shut_voice = 0;

            audio_data = (CONST CHAR_T*)media_src_introduction_zh;
            audio_size = sizeof(media_src_introduction_zh);
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            tuya_ai_input_start(TRUE);
            demo_test_time = 3300;
            flag_shut_voice = 4;
            keep_quiet = 1;
            
        }
        else if(flag_shut_voice == 4)
        {
            if(demo_test_time == 0)
            {
                flag_shut_voice = 5;
                demo_test_time = 5500;
                
                if(online_state == TRUE)
                {
                    audio_data = (CONST CHAR_T*)media_src_connected_zh;
                    audio_size = sizeof(media_src_connected_zh);
                    TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                    tuya_ai_input_start(TRUE);
                }
                else
                {
                    audio_data = (CONST CHAR_T*)media_src_disconnected_zh;
                    audio_size = sizeof(media_src_disconnected_zh);
                    TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                    tuya_ai_input_start(TRUE);
                }
            }
        }
        else if(flag_shut_voice == 5)
        {
            if(demo_test_time == 0)
            {
                flag_shut_voice = 0;
                demo_test_state = 1;
                tal_queue_post(s_queue_onvoice, &demo_test_state, 0);
                keep_quiet = 0;
                // wukong_audio_player_alert(AI_TOY_ALERT_TYPE_POWER_ON, FALSE);   //updat voice flag
            }
        }
        else if(flag_shut_voice == 2)
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
        else if(flag_shut_voice == 3)
        // else if((flag_shut_voice == 3)||(flag_shut_voice == 1))
        {
            // demo_test_state = 0;    //test

            flag_shut_voice = 0;
            // if(demo_test_state == 0)
            {
                demo_test_flag = 1;//1;
                demo_test_time = 8300;            
                audio_data = (CONST CHAR_T*)media_src_introduction_1_zh;
                audio_size = sizeof(media_src_introduction_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);

                // continue;
            }
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

        // while(MOTOR2_Cycle)
        // {
        //     MOTO_B2_RUN();
        //     tal_system_sleep(2);
        // }
        if((demo_test_state == 0)&&(demo_test_time == 0)&&(demo_test_flag!=0))
        {           
            rgb_dis_time = 600000;
            flag_rgb_data = RGB_BLUE;
            if(demo_test_flag == 1)
            {
                audio_data = (CONST CHAR_T*)media_src_introduction_2_zh;
                audio_size = sizeof(media_src_introduction_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 7500;//5500;
                flag_moto_motion = 9;
                moto_state = STATE_MOTO_NOD;
                
            }
            else if(demo_test_flag == 2)
            {
                audio_data = (CONST CHAR_T*)media_src_introduction_3_zh;
                audio_size = sizeof(media_src_introduction_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 9300;//5300;
                demo_test_cmd = 1;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 3)
            {
                audio_data = (CONST CHAR_T*)media_src_greeting_1_zh;
                audio_size = sizeof(media_src_greeting_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5300;  //7900
                demo_test_cmd = 2;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 4)
            {
                audio_data = (CONST CHAR_T*)media_src_greeting_2_zh;
                audio_size = sizeof(media_src_greeting_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 8700;//6700;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 5)
            {
                audio_data = (CONST CHAR_T*)media_src_breakfast_1_zh;
                audio_size = sizeof(media_src_breakfast_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 6000;
                // demo_test_cmd = 2;
                flag_moto_motion = 9;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 6)
            {
                audio_data = (CONST CHAR_T*)media_src_breakfast_2_zh;
                audio_size = sizeof(media_src_breakfast_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 10000;//6000;
                moto_state = STATE_MOTO_NOD;
            }            
            else if(demo_test_flag == 7)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_1_zh;
                audio_size = sizeof(media_src_get_moving_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5800;//2900;
                demo_test_cmd = 3;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 8)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_2_zh;
                audio_size = sizeof(media_src_get_moving_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5800;//6100;//3100;
                moto_state = STATE_MOTO_NOD;
                
            }
#if 1
            else if(demo_test_flag == 9)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_3_5_zh;
                audio_size = sizeof(media_src_get_moving_3_5_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag = 12;
                demo_test_time = 5800;//1500;//5800;//3100;
                moto_state = STATE_MOTO_NOD;
                
            }
#else
            
            else if(demo_test_flag == 9)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_3_zh;
                audio_size = sizeof(media_src_get_moving_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5800;//2900;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 10)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_4_zh;
                audio_size = sizeof(media_src_get_moving_4_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5800;//2900;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 11)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_5_zh;
                audio_size = sizeof(media_src_get_moving_5_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 4200;//2600;
                moto_state = STATE_MOTO_NOD;
            }
#endif
            else if(demo_test_flag == 12)
            {
                audio_data = (CONST CHAR_T*)media_src_get_moving_6_zh;
                audio_size = sizeof(media_src_get_moving_6_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 9700;//5700;
                moto_state = STATE_MOTO_NOD;
            }            
            else if(demo_test_flag == 13)
            {
                audio_data = (CONST CHAR_T*)media_src_to_get_testing_1_zh;
                audio_size = sizeof(media_src_to_get_testing_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);
                demo_test_flag++;
                demo_test_time = 8000;//6000;
                flag_moto_motion = 8;
                moto_state = STATE_MOTO_ON;
            }
           
            else if(demo_test_flag == 14)
            {
                audio_data = (CONST CHAR_T*)media_src_out_of_range_1_zh;
                audio_size = sizeof(media_src_out_of_range_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);
                demo_test_flag++;
                demo_test_time = 5600;
                moto_state = STATE_MOTO_NOD;
                flag_moto_motion = 9; 
            }
            else if(demo_test_flag == 15)
            {
                audio_data = (CONST CHAR_T*)media_src_out_of_range_2_zh;
                audio_size = sizeof(media_src_out_of_range_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5600;//5600;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 16)
            {
                audio_data = (CONST CHAR_T*)media_src_out_of_range_3_zh;
                audio_size = sizeof(media_src_out_of_range_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 9600;//5600;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 17)
            {
                audio_data = (CONST CHAR_T*)media_src_sedentary_remind_1_zh;
                audio_size = sizeof(media_src_sedentary_remind_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 7400;//5400;
                demo_test_cmd = 4;
                moto_state = STATE_MOTO_NOD;

            }
            else if(demo_test_flag == 18)
            {
                audio_data = (CONST CHAR_T*)media_src_sedentary_remind_2_zh;
                audio_size = sizeof(media_src_sedentary_remind_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 8300;//4300;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 19)
            {
                audio_data = (CONST CHAR_T*)media_src_turnoff_remind_1_zh;
                audio_size = sizeof(media_src_turnoff_remind_1_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                demo_test_time = 5700;
                moto_state = STATE_MOTO_NOD;
                flag_moto_motion = 9;
            }
            else if(demo_test_flag == 20)
            {
                audio_data = (CONST CHAR_T*)media_src_turnoff_remind_2_zh;
                audio_size = sizeof(media_src_turnoff_remind_2_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                // flag_turn_off_on_cmd = 2;
                demo_test_time = 4600;//2600;
                moto_state = STATE_MOTO_NOD;
                
            }
            else if(demo_test_flag == 21)
            {
                audio_data = (CONST CHAR_T*)media_src_turnoff_remind_3_zh;
                audio_size = sizeof(media_src_turnoff_remind_3_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);                
                demo_test_flag++;
                flag_turn_off_on_cmd = POWER_STATUS_ON;
                demo_test_time = 5500;//4200;
                moto_state = STATE_MOTO_NOD;
            }
            else if(demo_test_flag == 22)
            {
                demo_test_flag = 0;
                // demo_test_flag++;
                
                flag_turn_off_on_cmd = POWER_STATUS_ON;     
                flag_turn_off_on_state = POWER_STATUS_ON;
                moto_state = STATE_MOTO_ON;
                // demo_test_time = 6000;
                demo_test_state = 1;
                nod_time = 2000;
                tal_queue_post(s_queue_onvoice, &demo_test_state, 0);   //
            }
            continue;
            
        }
        if((demo_test_flag == 1)||(demo_test_flag == 7)||(demo_test_flag == 14)||(demo_test_flag == 23))
        {
            if((demo_test_time>6900&&demo_test_time<7000)||(demo_test_time>4900&&demo_test_time<5000)||(demo_test_time>2900&&demo_test_time<3000))
            moto_state = STATE_MOTO_ON;
        }

        // if(net_state == WSS_GOT_IP)
        if(online_state == TRUE)
        {
            if(face_voice_flag == 1)
            {
                face_voice_flag = 0;
                // tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("这个是一个陌生人，打一下招呼，问一下对方怎么称呼"));
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("这个是一个陌生人，用下面这个话术进行打招呼，不要加任何其他的词包括“好的”和“正在处理”等词，直接说：“你好呀，我是小康，请问您怎么称呼呀?”。"));
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("跟陌生人打个招呼，问一下他叫什么名字。"));
                // tuya_ai_input_stop();
                audio_data = (CONST CHAR_T*)media_src_get_username_zh;
                audio_size = sizeof(media_src_get_username_zh);
                TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
                tuya_ai_input_start(TRUE);
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

        tal_system_sleep(10);
    }
    tal_queue_free(s_queue_voice_cmd);
    tal_queue_free(s_queue_emotion);
    tal_queue_free(s_queue_name_str);
    tal_queue_free(s_queue_wake);
    tal_queue_free(s_queue_onvoice);
    
    return rt;
}

