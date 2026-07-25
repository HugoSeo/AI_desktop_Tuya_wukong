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


// #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/
#define ADC_NUM       TUYA_ADC_NUM_0
#define ADC_CHANNEL   14

#define LED_CTRL_PIN            TUYA_GPIO_NUM_50
#define TOUCH_KEY_PIN           TUYA_GPIO_NUM_13

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


STATIC UINT8_T segdata[4]={16,16,16,16};
STATIC UINT8_T segdp_flg = 0;
STATIC UINT8_T time_valid_flag = 0;

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






// void app_led_send_msg(DEV_STATE new_msg)
// {
// 	bk_err_t ret;
// 	LED_MSG_T msg;

// 	if (led_msg_que) {
// 		msg.led_msg = new_msg;

// 		ret = rtos_push_to_queue(&led_msg_que, &msg, BEKEN_NO_WAIT);
// 		if (kNoErr != ret)
// 			BK_LOGD(NULL, "app_led_send_msg failed\r\n");
// 	}
// }

// static void app_led_timer_poll_handler(void)
// {
// 	bk_err_t err;

// 	bk_gpio_output_reverse(ledctr.gpio_idx);

// 	err = rtos_reload_timer(&ledctr.led_timer);
// 	BK_ASSERT(kNoErr == err);
// }

// static void app_led_timer_handler(void *data)
// {
// 	app_led_send_msg(TIMER_POLL);
// }

// static void app_led_poll_handler(DEV_STATE next_sta)
// {
// 	uint32_t intval = 0;
// 	bk_err_t err;

// 	//LED_PRT("state:%d, next_sta:%d\r\n", ledctr.state, next_sta);

// 	if (ledctr.state == next_sta)
// 		return;

// 	err = rtos_stop_timer(&ledctr.led_timer);
// 	BK_ASSERT(kNoErr == err);

// 	switch (next_sta) {
// 	case STA_NONE:
// 		break;

// 	case POWER_ON:
// 	case LED_DISCONNECT: {
// 		intval = LED_DISCONNECT_VAL;
// 		ledctr.state = LED_DISCONNECT;
// 		break;
// 	}

// 	case MONITOR_MODE: {
// 		intval = LED_MONITOR_VAL;
// 		ledctr.state = MONITOR_MODE;
// 		break;
// 	}

// 	case SOFTAP_MODE: {
// 		intval = LED_SOFTAP_VAL;
// 		ledctr.state = SOFTAP_MODE;
// 		break;
// 	}

// 	case LED_CONNECT: {
// 		intval = 0;
// 		ledctr.state = LED_CONNECT;
// 		bk_gpio_output(ledctr.gpio_idx, LED_ON_VAL);
// 		break;
// 	}

// 	default:
// 		break;
// 	}

// 	if (intval) {
// 		err = rtos_change_period(&ledctr.led_timer, intval);
// 		BK_ASSERT(kNoErr == err);
// 	}
// }

// static void app_led_main(beken_thread_arg_t data)
// {
// 	bk_err_t err;

// 	os_memset(&ledctr, 0, sizeof(LED_ST));
// 	ledctr.state = STA_NONE;

// 	ledctr.gpio_idx = LED_GPIO_INDEX;

// 	bk_gpio_config_output(ledctr.gpio_idx);
// 	bk_gpio_output(ledctr.gpio_idx, LED_INITIAL_VAL);

// 	err = rtos_init_timer(&ledctr.led_timer,
// 						  1 * 1000,
// 						  app_led_timer_handler,
// 						  (void *)0);
// 	BK_ASSERT(kNoErr == err);

// 	err = rtos_start_timer(&ledctr.led_timer);
// 	BK_ASSERT(kNoErr == err);

// 	while (1) {
// 		LED_MSG_T msg;
// 		err = rtos_pop_from_queue(&led_msg_que, &msg, BEKEN_WAIT_FOREVER);
// 		if (kNoErr == err) {
// 			switch (msg.led_msg) {
// 			case TIMER_POLL:
// 				app_led_timer_poll_handler();
// 				break;
// 			default:
// 				app_led_poll_handler(msg.led_msg);
// 				break;
// 			}
// 		}
// 	}

// app_led_exit:
// 	LED_PRT("app_led_main exit\r\n");

// 	rtos_deinit_queue(&led_msg_que);
// 	led_msg_que = NULL;

// 	led_thread_handle = NULL;
// 	rtos_delete_thread(NULL);

// }


// UINT32 app_led_init(void)
// {
// 	int ret;

// 	LED_PRT("app_led_init\r\n");
// 	if ((!led_thread_handle) && (!led_msg_que)) {
// 		ret = rtos_init_queue(&led_msg_que,
// 							  "led_queue",
// 							  sizeof(LED_MSG_T),
// 							  LED_QITEM_COUNT);
// 		if (kNoErr != ret) {
// 			LED_PRT("temp detect ceate queue failed\r\n");
// 			return kGeneralErr;
// 		}

// 		ret = rtos_create_thread(&led_thread_handle,
// 								 BEKEN_DEFAULT_WORKER_PRIORITY,
// 								 "app led",
// 								 (beken_thread_function_t)app_led_main,
// 								 1024,
// 								 NULL);
// 		if (ret != kNoErr) {
// 			rtos_deinit_queue(&led_msg_que);
// 			led_msg_que = NULL;
// 			LED_PRT("Error: Failed to create app_led_init: %d\r\n", ret);
// 			return kGeneralErr;
// 		}
// 	}

// 	return kNoErr;
// }








// STATIC 
VOID_T seg_init(VOID_T)
{
    OPERATE_RET rt = OPRT_OK;
    /*GPIO output init*/
    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PULLUP,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_A_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_B_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_C_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_D_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_E_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_F_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_G_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_DP_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_1_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_2_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_3_PIN, &out_pin_cfg));
    TUYA_CALL_ERR_LOG(tkl_gpio_init(SEG_4_PIN, &out_pin_cfg));
}

STATIC VOID_T seg_alloff(VOID_T)
{
    tkl_gpio_write(SEG_1_PIN, TUYA_GPIO_LEVEL_LOW);
    tkl_gpio_write(SEG_2_PIN, TUYA_GPIO_LEVEL_LOW);
    tkl_gpio_write(SEG_3_PIN, TUYA_GPIO_LEVEL_LOW);
    tkl_gpio_write(SEG_4_PIN, TUYA_GPIO_LEVEL_LOW);

    // tkl_gpio_write(SEG_A_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(SEG_B_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(SEG_C_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(SEG_D_PIN, TUYA_GPIO_LEVEL_LOW);

    // tkl_gpio_write(SEG_E_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(SEG_F_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(SEG_G_PIN, TUYA_GPIO_LEVEL_LOW);
    // tkl_gpio_write(SEG_DP_PIN, TUYA_GPIO_LEVEL_LOW);

}
STATIC VOID_T seg_task(VOID_T)
{
    STATIC UINT8_T seg_cnt = 0;
    STATIC UINT8_T seg_dis = 0;
    STATIC UINT32_T seg_halfsec_cnt = 0;

    if(time_valid_flag == 0)
    {
        return;
    }

    seg_alloff();
    if (seg_cnt < 4)
    {
        seg_dis = seg_code[segdata[seg_cnt]];

        if (seg_dis&0x01)
        {
            tkl_gpio_write(SEG_A_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_A_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        if (seg_dis&0x02)
        {
            tkl_gpio_write(SEG_B_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_B_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        if (seg_dis&0x04)
        {
            tkl_gpio_write(SEG_C_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_C_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        if (seg_dis&0x08)
        {
            tkl_gpio_write(SEG_D_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_D_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        if (seg_dis&0x10)
        {
            tkl_gpio_write(SEG_E_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_E_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        if (seg_dis&0x20)
        {
            tkl_gpio_write(SEG_F_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_F_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        if (seg_dis&0x40)
        {
            tkl_gpio_write(SEG_G_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_G_PIN, TUYA_GPIO_LEVEL_LOW);
        }
        

        if(segdp_flg == 0)
        {
            tkl_gpio_write(SEG_DP_PIN, TUYA_GPIO_LEVEL_HIGH);
        }
        else
        {
            tkl_gpio_write(SEG_DP_PIN, TUYA_GPIO_LEVEL_LOW);
        }

        switch (seg_cnt)
        {
        case 0:
            tkl_gpio_write(SEG_4_PIN, TUYA_GPIO_LEVEL_HIGH);            
            break;
        case 1:
            tkl_gpio_write(SEG_3_PIN, TUYA_GPIO_LEVEL_HIGH);
            break;            
        case 2:
            tkl_gpio_write(SEG_2_PIN, TUYA_GPIO_LEVEL_HIGH);
            break;
        case 3:
            tkl_gpio_write(SEG_1_PIN, TUYA_GPIO_LEVEL_HIGH);
            break;
        default:
            break;
        }
    }

    seg_cnt++;
    if(seg_cnt >= 4)
        seg_cnt = 0;

    seg_halfsec_cnt++;
    if (seg_halfsec_cnt >= 500)
    {
        seg_halfsec_cnt = 0;
        segdp_flg = ~segdp_flg;
    }
    

    // tkl_gpio_write(SEG_A_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_B_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_C_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_D_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_E_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_F_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_G_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_DP_PIN, TUYA_GPIO_LEVEL_HIGH);

    // tkl_gpio_write(SEG_1_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_2_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_3_PIN, TUYA_GPIO_LEVEL_HIGH);
    // tkl_gpio_write(SEG_4_PIN, TUYA_GPIO_LEVEL_HIGH);

}

VOID_T hugo_ai_seg_process(VOID_T)
{
    // seg_init();
    // while(1)
    // {
        seg_task();
    //     tal_system_sleep(3);
    // }

}

STATIC VOID_T hugo_ai_seg_reflash_time(VOID_T)
{
    POSIX_TM_S tm = {0};
    STATIC INT_T s_last_min  = -1;

    if (tal_time_check_time_sync() != OPRT_OK ||
    tal_time_check_time_zone_sync() != OPRT_OK) 
    {
        return;
    }
    if (tal_time_get_local_time_custom(0, &tm) != OPRT_OK) 
    {
        return;
    }
    if (tm.tm_min== s_last_min)
    {
        return;
    }

    time_valid_flag = 1;
    segdata[0] = tm.tm_hour/10;
    // if (segdata[0] == 0)
    // {
    //     segdata[0] = 16;
    // }
    segdata[1] = tm.tm_hour%10;
    segdata[2] = tm.tm_min/10;
    segdata[3] = tm.tm_min%10;
}

STATIC VOID hugo_ai_moto_init(VOID)
{

}
STATIC VOID hugo_ai_moto_task(VOID)
{
    
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
    seg_init();
    // app_led_init();

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

        hugo_ai_seg_reflash_time();
        
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
        if (tal_queue_fetch(s_queue_voice_cmd, &getdata, 1) == OPRT_OK)
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

