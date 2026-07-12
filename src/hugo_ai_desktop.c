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

// #include "bk_gpio.h"
// #include "gpio_driver.h"
#include "wukong_ai_agent.h"
#include "wukong_ai_skills.h"
#include "skill_cloudevent.h"
#include "wukong_audio_player.h"
#include "hugo_ai_desktop.h"

// #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define ADC_NUM       TUYA_ADC_NUM_0
#define ADC_CHANNEL   14

#define LED_CTRL_PIN            TUYA_GPIO_NUM_50
#define TOUCH_KEY_PIN           TUYA_GPIO_NUM_13

//uart define
#define Maxdatalen 300


STATIC UINT8_T uart_rxbuff[Maxdatalen] = {0x00};
STATIC UINT8_T uart_txbuff[Maxdatalen] = {0x00};
STATIC UINT16_T uart_Rxln = 0;
STATIC UINT8_T flag_rgb_data = 0;
STATIC UINT8_T flag_moto_direction = 0;
STATIC UINT8_T flag_moto_time_H = 0;
STATIC UINT8_T flag_moto_time_L = 0;
STATIC UINT8_T flag_move_cmd = 0;
STATIC UINT8_T flag_turn_off_on_cmd = 0;
STATIC UINT8_T flag_shut_voice = 0;
STATIC UINT8_T flag_turn_off_on_state = 0;
STATIC UINT8_T getdata[4]={0};

// BYTE_T flag_gateway_state = 0;

/***********************************************************
***********************typedef define***********************
***********************************************************/


/***********************************************************
***********************variable define**********************
***********************************************************/
STATIC TUYA_ADC_BASE_CFG_T sg_adc_cfg = {
    .ch_list.data = 1<<ADC_CHANNEL,
    .ch_nums = 1,    //adc Number of channel lists
    .width = 12,
    .mode = TUYA_ADC_CONTINUOUS,
    .type = TUYA_ADC_INNER_SAMPLE_VOL,
    .conv_cnt = 1,
};

extern QUEUE_HANDLE  s_queue_voice_cmd;
extern QUEUE_HANDLE  s_queue_state;

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
                case 0x01:  //灯控                    
                    break;
                case 0x02:  //底座电机方向控制
                    break;
                case 0x03:  //执行预设动作
                
                    break;
                case 0x04:
                    if(uart_rxbuff[t+6]<=2)
                    {
                        flag_turn_off_on_state = uart_rxbuff[t+6];
                        flag_shut_voice = flag_turn_off_on_state;

                        tal_queue_post(s_queue_state, &flag_turn_off_on_state, 0);
                    }
                    break;
                default:
                    break;
                }
            }
        }
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

    
    //LED control
    if(flag_rgb_data==RGB_ON)
    {
        flag_rgb_data=0;
        data[0] = 1;
        send_mcu_data(0x01,data,1);
    }
    if(flag_rgb_data==RGB_OFF)
    {
        flag_rgb_data=0;
        data[0] = 2;
        send_mcu_data(0x01,data,1);
    }
    

    if(flag_moto_direction==MOTO_RIGHT)
    {
        flag_moto_direction=0;
        data[0] = 1;
        data[1] = flag_moto_time_H;
        data[2] = flag_moto_time_L;
        send_mcu_data(0x02,data,3);
    }
    if(flag_moto_direction==MOTO_LEFT)
    {
        flag_moto_direction=0;
        data[0] = 2;
        data[1] = flag_moto_time_H;
        data[2] = flag_moto_time_L;
        send_mcu_data(0x02,data,3);
    }

    if(flag_move_cmd != 0)
    {
        send_mcu_data(0x03,&flag_move_cmd,1);
        flag_move_cmd = 0;
    }
    
    if(flag_turn_off_on_cmd != 0)
    {
        if(flag_turn_off_on_cmd ==3)
        {
            flag_turn_off_on_cmd = 0;
        }            
        send_mcu_data(0x04,&flag_turn_off_on_cmd,1);   
        flag_turn_off_on_cmd = 0;
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

OPERATE_RET hugo_ai_desktop_init(VOID)
{
    OPERATE_RET rt = OPRT_OK;

    CONST CHAR_T *audio_data = NULL;
    UINT32_T audio_size = 0;

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




    // uart test
    mcu_uart_init();

    //touch key
    touchkey_init();

    

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
    tal_queue_create_init(&s_queue_state, SIZEOF(UINT8_T), 1);
    // WUKONG_AI_PLAYTTS_T tts_param = {
    //     .text = "网络开小差了，机器人暂时无法联网，仅支持本地按键操作"
    // };
    // wukong_audio_play_tts_url(tts_param,FALSE);
    while (1)
    { 
        // TUYA_CALL_ERR_LOG(hugo_cmd_getback(buf));
        
        mcu_uart_task();
        if(get_touchkey()==1)
        {
            TAL_PR_NOTICE("------------------get touch key------------------");
            // tuya_ai_input_start(TRUE);
            // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text("跟我读，碰到我啦"));
            // tuya_ai_input_stop();

            // audio_data = (CONST CHAR_T*)media_src_haha_zh;
            // audio_size = sizeof(media_src_haha_zh); 
            // TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            // flag_move_cmd = 3;
            // tal_system_sleep(100);
        }
        // hugo_cmd_getback1(buf);
        if (tal_queue_fetch(s_queue_voice_cmd, &getdata, 1000) == OPRT_OK)
        {
            TAL_PR_NOTICE("------------------get OK------------------");
            // tal_system_sleep(1000);
            TAL_PR_NOTICE("------------------getdata=%d------------------",getdata[0]);
            tuya_ai_input_start(TRUE);
            TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
            tuya_ai_input_stop();

            // audio_data = (CONST CHAR_T*)media_src_haolei_zh;
            // audio_size = sizeof(media_src_haolei_zh); 
            // TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            
            switch (getdata[0])
            {
            case 1:
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
            // case 2:
            //     // TAL_PR_NOTICE("------------------get data 2------------------");
            //     break;
            case 3:
                // tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
                // tuya_ai_input_stop();
                if(getdata[1]<=5)
                {
                    if(flag_turn_off_on_state == 2)
                    {
                        continue;
                    }
                    flag_move_cmd = getdata[1];
                }
                break;
            case 4:
                // tuya_ai_input_start(TRUE);
                // TUYA_CALL_ERR_LOG(wukong_ai_agent_send_text(""));
                // tuya_ai_input_stop();
                if(getdata[1]<=2)
                {
                    flag_turn_off_on_cmd = getdata[1];
                    flag_turn_off_on_state = flag_turn_off_on_cmd;
                    TAL_PR_NOTICE("------------------flag_turn_off_on_state=%d------------------",flag_turn_off_on_state);
                    tal_queue_post(s_queue_state, &flag_turn_off_on_state, 0);
                    
                    if(flag_turn_off_on_cmd==2)
                    {
                        flag_shut_voice = 2;
                        continue;
                    }                    
                }
                break;

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
            audio_data = (CONST CHAR_T*)media_src_haolei_zh;
            audio_size = sizeof(media_src_haolei_zh);
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
        }

        if(flag_shut_voice == 1)
        {
            flag_shut_voice = 0;
            audio_data = (CONST CHAR_T*)media_src_dingdong_zh;
            audio_size = sizeof(media_src_dingdong_zh); 
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            flag_turn_off_on_cmd = 3;
        }
        if(flag_shut_voice == 2)
        {
            flag_shut_voice = 0;
            audio_data = (CONST CHAR_T*)media_src_turnoff_zh;
            audio_size = sizeof(media_src_turnoff_zh); 
            TUYA_CALL_ERR_LOG(wukong_audio_play_data(AI_AUDIO_CODEC_MP3, audio_data, audio_size));
            flag_turn_off_on_cmd = 3;
        }
        // if(*buf!=0)
        // {
        //     TAL_PR_NOTICE("------------------buf=%d------------------",*buf);
        //     *buf=0;
        // }

        // tuya_ai_input_start(TRUE);
        // wukong_ai_agent_send_text("重复下面这个词，嘿嘿我在");
        // tuya_ai_input_stop();

        // LED_reset();
        // for (size_t i = 0; i < 16; i++)
        // {
            
        //     // LED_sign(data);
        //     // data+=100;
        //     LED_sign(0x5A5A5A);
        // }
        // LED_reset();
        // LED_end();

        // tal_system_sleep(200);
    }
    tal_queue_free(s_queue_voice_cmd);
    tal_queue_free(s_queue_state);
    return rt;

}

