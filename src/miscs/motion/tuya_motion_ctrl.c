#include "tuya_motion_ctrl.h"
#include "tal_sw_timer.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tal_sleep.h"
#include "tal_watchdog.h"
#include "tal_memory.h"
#include "tal_thread.h"
#include "tal_semaphore.h"
#include "tal_gpio.h"
#include "tal_uart.h"
#include "tuya_device_cfg.h"

#if defined(T5AI_BOARD_DESKTOP) && (T5AI_BOARD_DESKTOP == 1)

STATIC tuya_motion_ctrl_t s_ctrl_cfg = {0};
STATIC tuya_motion_heart_t s_heart_cfg = {0};

#define LEFT_CHECKSUM (0x03 ^ 0x01 ^ 0x00 ^ (BYTE_T)((angle >> 8) & 0xFF) ^ (BYTE_T)(angle & 0xFF))
#define RIGHT_CHECKSUM (0x03 ^ 0x01 ^ 0x01 ^ (BYTE_T)((angle >> 8) & 0xFF) ^ (BYTE_T)(angle & 0xFF))
#define ABSOLUTE_CHECKSUM (0x02 ^ 0x02 ^ (BYTE_T)((angle >> 8) & 0xFF) ^ (BYTE_T)(angle & 0xFF))

//左转
OPERATE_RET tuya_motion_left(UINT32_T angle)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_PR_DEBUG("[====motion] left angle: %d ", angle);

    BYTE_T data[8] = {0x55, 0xAA, 0x03, 0x01, 0x00, (BYTE_T)((angle >> 8) & 0xFF), (BYTE_T)(angle & 0xFF), LEFT_CHECKSUM};

    TAL_PR_DEBUG("[====motion] left tx data: %02X %02X %02X %02X %02X %02X %02X %02X ", 
        data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    TUYA_CALL_ERR_RETURN(tal_uart_write(TUYA_UART_NUM_2, data, SIZEOF(data)));
}

//右转
OPERATE_RET tuya_motion_right(UINT32_T angle)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_PR_DEBUG("[====motion] right angle: %d ", angle);

    BYTE_T data[8] = {0x55, 0xAA, 0x03, 0x01, 0x01, (BYTE_T)((angle >> 8) & 0xFF), (BYTE_T)(angle & 0xFF), RIGHT_CHECKSUM};

    TAL_PR_DEBUG("[====motion] right tx data: %02X %02X %02X %02X %02X %02X %02X %02X ", 
        data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    TUYA_CALL_ERR_RETURN(tal_uart_write(TUYA_UART_NUM_2, data, SIZEOF(data)));
}

//绝对角度(0~360)
OPERATE_RET tuya_motion_set_absolute_angle(UINT32_T angle)
{
    OPERATE_RET rt = OPRT_OK;
    TAL_PR_DEBUG("[====motion] absolute angle: %d ", angle);

    BYTE_T data[7] = {0x55, 0xAA, 0x02, 0x02, (BYTE_T)((angle >> 8) & 0xFF), (BYTE_T)(angle & 0xFF), ABSOLUTE_CHECKSUM};

    TAL_PR_DEBUG("[====motion] abs tx data: %02X %02X %02X %02X %02X %02X %02X ", 
        data[0], data[1], data[2], data[3], data[4], data[5], data[6]);

    TUYA_CALL_ERR_RETURN(tal_uart_write(TUYA_UART_NUM_2, data, SIZEOF(data)));
}


OPERATE_RET tuya_motion_send_msg(UINT_T mode, UINT_T rotate_value)
{ 
    if(s_ctrl_cfg.queue == NULL)
    {
        TAL_PR_ERR("motion queue is null");
        return OPRT_COM_ERROR;
    }
    tuya_motion_data_t st_motion_data = {0};
    st_motion_data.motion_mode = mode;
    st_motion_data.rotate_value = rotate_value;
    return tal_queue_post(s_ctrl_cfg.queue, &st_motion_data, 0);
}

VOID_T tuya_motion_ctrl(TUYA_MOTION_CTRL_EN mode, UINT32_T angle)
{
    TAL_PR_DEBUG("[tuya_motion_ctrl] motion ctrl mode: %d, angle: %d");
    OPERATE_RET rt;
    switch(mode)
    {
        case MOTION_CTRL_LEFT:           //左转
        case MOTION_CTRL_CLOCKWISE:          //顺时针
        {
            tuya_motion_left(angle);
        }
        break;

        case MOTION_CTRL_RIGHT:              //右转
        case MOTION_CTRL_COUNTERCLOCKWISE:   //逆时针
        {
            tuya_motion_right(angle);
        }
        break;

        case MOTION_CTRL_APPOINT:            //指定角度
        {
            tuya_motion_set_absolute_angle(angle);
        }
        break;

        case MOTION_CTRL_POINT_MOVE:         //转一点
        {
            tuya_motion_left(angle);
        }
        break;

        case MOTION_CTRL_RESET:              //复位
        {
            tuya_motion_set_absolute_angle(0);
        }
        break;

        case MOTION_CTRL_STOP:              //停止
        {
            tuya_motion_left(0);
        }
        break;

        default:
        break;
    }
}

STATIC VOID __motion_heart_task(VOID *args)
{
    OPERATE_RET rt = 0;
    BYTE_T read_buf[6] = {0};
    memset(read_buf, 0, sizeof(read_buf));

    s_heart_cfg.current_time = tal_system_get_millisecond();

    while(s_heart_cfg.is_running) 
    {
        rt = tal_uart_read(TUYA_UART_NUM_2, read_buf, 6);
        if(rt > 0)
        {
            bk_printf("[__motion_heart_task] read uart2 data: ");
            for(int i = 0; i < 6; i++)
            {
                bk_printf("%02X ", read_buf[i]);
            }
            bk_printf("\r\n");
            
            memset(read_buf, 0, sizeof(read_buf)); 

            s_heart_cfg.is_alive = TRUE;

            s_heart_cfg.last_heartbeat_time = s_heart_cfg.current_time;

            if(s_heart_cfg.is_finsh_motion == FALSE)
            {
                TAL_PR_DEBUG("[__motion_heart_task] motion finish");
                tuya_motion_send_msg(MOTION_CTRL_FINSH, 0);
            }

        }

        UINT_T heart_alive = s_heart_cfg.current_time - s_heart_cfg.last_heartbeat_time;
        if(heart_alive >= 6000 && s_heart_cfg.is_finsh_motion)
        {
            s_heart_cfg.is_alive = FALSE;
        }

        tal_system_sleep(50);
    }
}

STATIC VOID __motion_ctrl_task(VOID *args)
{
    OPERATE_RET rt = OPRT_OK;
    rt = tal_queue_create_init(&s_ctrl_cfg.queue, SIZEOF(tuya_motion_data_t), 3);
    if(rt != OPRT_OK)
    {
        TAL_PR_ERR("[__motion_ctrl_task] motion ctrl queue create failed\r\n");
        s_ctrl_cfg.is_running = FALSE;
    }

    UINT_T timeout = 100;
    tuya_motion_data_t st_motion_data = {0};
    TUYA_MOTION_CTRL_EN ctrl_mode = MOTION_CTRL_IDLE;
    UINT_T ctrl_angle = 0;

    while (s_ctrl_cfg.is_running)
    {
        rt = tal_queue_fetch(s_ctrl_cfg.queue, &st_motion_data, timeout);
        if(rt == OPRT_OK)
        {
            TAL_PR_DEBUG("[__motion_ctrl_task] motion mode: %d, rotate: %d, motion state: %d", 
                st_motion_data.motion_mode, st_motion_data.rotate_value, s_ctrl_cfg.motion_state);

            if(st_motion_data.motion_mode >= MOTION_CTRL_LEFT && st_motion_data.motion_mode <= MOTION_CTRL_RESET)    
            {
                if(s_ctrl_cfg.motion_state == CTRL_STATE_IDLE)
                {
                    ctrl_mode = st_motion_data.motion_mode;
                    ctrl_angle = st_motion_data.rotate_value;
                    s_ctrl_cfg.motion_state = CTRL_STATE_START;
                }  
            }
            else if (st_motion_data.motion_mode == MOTION_CTRL_STOP)
            {
                s_ctrl_cfg.motion_state = CTRL_STATE_STOP;
            }
            else if (st_motion_data.motion_mode == MOTION_CTRL_FINSH)
            {
                if(s_ctrl_cfg.motion_state == CTRL_STATE_RUNNING || s_ctrl_cfg.motion_state == CTRL_STATE_START)
                {
                    s_ctrl_cfg.motion_state = CTRL_STATE_FINSH;
                }
            }
        }

        switch(s_ctrl_cfg.motion_state)
        {
            case CTRL_STATE_IDLE:
            {
                timeout+=10;
            }
            break;

            case CTRL_STATE_START:
            {
                TAL_PR_DEBUG("[__motion_ctrl_task] CTRL_STATE_START");
                s_heart_cfg.is_finsh_motion = FALSE;
                // tuya_motion_right(30);
                tuya_motion_ctrl(ctrl_mode, ctrl_angle);
                s_ctrl_cfg.motion_state = CTRL_STATE_RUNNING;
            }
            break;

            case CTRL_STATE_RUNNING:
            {
                //等待执行完成
            }
            break;

            case CTRL_STATE_FINSH:
            {
                TAL_PR_DEBUG("[====motion] CTRL_STATE_FINSH");
                s_ctrl_cfg.motion_state = CTRL_STATE_IDLE;
                s_heart_cfg.is_finsh_motion = TRUE;
            }
            break;

            case CTRL_STATE_STOP:
            {
                TAL_PR_DEBUG("[====motion] CTRL_STATE_STOP");
                tuya_motion_ctrl(MOTION_CTRL_STOP, 0);
                s_ctrl_cfg.motion_state = CTRL_STATE_IDLE;
                
            }
            break;
        }

    }
    
    if (s_ctrl_cfg.ctrl_task) {
        tal_thread_delete(s_ctrl_cfg.ctrl_task);
        s_ctrl_cfg.ctrl_task = NULL;
        TAL_PR_ERR("motion ctrl task exit\r\n");
    }
}

OPERATE_RET tuya_motion_ctrl_init(VOID_T)
{
    TAL_PR_DEBUG("tuya motion ctrl init\r\n");
    OPERATE_RET rt = OPRT_OK;

    //rx init
    TAL_UART_CFG_T cfg_rx = {0};
    cfg_rx.base_cfg.baudrate = 115200;
    cfg_rx.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg_rx.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg_rx.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
    cfg_rx.base_cfg.flowctrl = TUYA_UART_FLOWCTRL_NONE;
    cfg_rx.rx_buffer_size = 256;
    cfg_rx.open_mode = 0;//O_BLOCK;
    TUYA_CALL_ERR_RETURN(tal_uart_init(TUYA_UART_NUM_2, &cfg_rx));

    memset(&s_heart_cfg, 0, sizeof(s_heart_cfg));

    s_heart_cfg.heart_task = NULL;
    s_heart_cfg.is_running = TRUE;
    s_heart_cfg.is_alive = FALSE;
    s_heart_cfg.is_finsh_motion = TRUE;
    //heart task init
    THREAD_CFG_T heart_thrd_cfg = {
        .priority = THREAD_PRIO_5,
        .stackDepth = 3*1024,
        .thrdname = "motion_heart",
        #ifdef ENABLE_EXT_RAM
        .psram_mode = 1,
        #endif            
    };
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_heart_cfg.heart_task, NULL, NULL, __motion_heart_task, NULL, &heart_thrd_cfg));


    memset(&s_ctrl_cfg, 0, sizeof(s_ctrl_cfg));
    s_ctrl_cfg.is_running = TRUE;
    s_ctrl_cfg.ctrl_task = NULL;
    s_ctrl_cfg.queue = NULL;
    s_ctrl_cfg.motion_state = CTRL_STATE_IDLE;
    //ctrl task init
    THREAD_CFG_T ctrl_thrd_cfg = {
        .priority = THREAD_PRIO_5,
        .stackDepth = 3*1024,
        .thrdname = "motion_ctrl",
        #ifdef ENABLE_EXT_RAM
        .psram_mode = 1,
        #endif            
    };
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_ctrl_cfg.ctrl_task, NULL, NULL, __motion_ctrl_task, NULL, &ctrl_thrd_cfg));

    return rt;
}

#endif