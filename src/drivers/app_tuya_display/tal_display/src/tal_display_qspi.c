#include "tal_display_qspi.h"
#include "tal_semaphore.h"
#include "tal_system.h"
#include "tal_mutex.h"
#include "tal_thread.h"
#include "tal_queue.h"
#if defined(ENABLE_QSPI) && (ENABLE_QSPI==1)
#include "tkl_qspi.h"
#endif
#include "uni_log.h"
#include "tuya_app_config.h"
#ifndef TAL_DISPLAY_PRI
#define TAL_DISPLAY_PRI THREAD_PRIO_0
#endif

#if defined(ENABLE_QSPI) && (ENABLE_QSPI==1)
#define UNACTIVE_LEVEL(x) ((x == 0)? 1: 0)
typedef enum {
    QSPI_FRAME_REQUEST = 0,
    QSPI_FRAME_EXIT,
};

typedef struct {
	UINT32_T event;
	UINT32_T param;
} qspi_msg_t;

typedef struct {
	UINT8_T qspi_task_running;
	ty_frame_buffer_t *display_frame;
	SEM_HANDLE qspi_task_sem;
    MUTEX_HANDLE mutex;//for close&flush sync
	THREAD_HANDLE qspi_task;
    QUEUE_HANDLE queue;
    ty_display_qspi_device_t *device;
    SEM_HANDLE tx_sem;
} qspi_disp_manage_t;

qspi_disp_manage_t qspi_disp_manage = {0};

static OPERATE_RET display_qspi_device_gpio_init(ty_display_qspi_device_t *device)
{
    TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT};
    OPERATE_RET ret = OPRT_OK;

    if(device->display_cfg->reset.pin != TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
        ret = tkl_gpio_init(device->display_cfg->reset.pin, &cfg);
    }

    if(device->display_cfg->bl.pin != TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->bl.active_level);
        ret |= tkl_gpio_init(device->display_cfg->bl.pin, &cfg);
    }

    if(device->display_cfg->power_ctrl.pin != TUYA_GPIO_NUM_MAX) {
        cfg.level = device->display_cfg->power_ctrl.active_level;
        ret |= tkl_gpio_init(device->display_cfg->power_ctrl.pin, &cfg);
    }

    return ret;
}

static OPERATE_RET display_qspi_device_gpio_deinit(ty_display_qspi_device_t *device)
{
    OPERATE_RET ret = OPRT_OK;
    if(device->display_cfg->reset.pin != TUYA_GPIO_NUM_MAX) {
        ret = tkl_gpio_deinit(device->display_cfg->reset.pin);
    }

    if(device->display_cfg->bl.pin != TUYA_GPIO_NUM_MAX) {
        tkl_gpio_write(device->display_cfg->bl.pin, UNACTIVE_LEVEL(device->display_cfg->bl.active_level));
        ret |= tkl_gpio_deinit(device->display_cfg->bl.pin);
    }

    if(device->display_cfg->power_ctrl.pin != TUYA_GPIO_NUM_MAX) {
        tkl_gpio_write(device->display_cfg->power_ctrl.pin, UNACTIVE_LEVEL(device->display_cfg->power_ctrl.active_level));
        ret |= tkl_gpio_deinit(device->display_cfg->power_ctrl.pin);
    }

    return ret;
}

static OPERATE_RET display_qspi_init_seq_send(ty_display_qspi_device_t *device)
{
    if (device->init_seq != NULL) {
        DISPLAY_INIT_SEQ_T *seq = device->init_seq;
        for (;;) {
            switch (seq->type) {
                case TY_INIT_RST: {
                    // reset
                    DISPLAY_RESET_DATA_T *reset = seq->reset;
                    int rst_gpio = device->display_cfg->reset.pin;
                    TUYA_GPIO_LEVEL_E active_level = device->display_cfg->reset.active_level;
                    TUYA_GPIO_LEVEL_E unactive_level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
                
                    tkl_gpio_write(rst_gpio, unactive_level);
                    tkl_system_sleep(reset[0].delay);

                    tkl_gpio_write(rst_gpio, active_level);
                    tkl_system_sleep(reset[1].delay);

                    tkl_gpio_write(rst_gpio, unactive_level);
                    tkl_system_sleep(reset[2].delay);

                    break;
                }

                case TY_INIT_REG: {
                    // set reg value
                    DISPLAY_REG_DATA_T *reg = &seq->reg;
                    TUYA_QSPI_CMD_T cmd = {0};
                    cmd.op = TUYA_QSPI_WRITE;
                    cmd.cmd[0] = device->reg_write_cmd;
                    cmd.cmd_lines = TUYA_QSPI_1WIRE;
                    cmd.cmd_size = 1;

                    cmd.addr[0] = 0x00;
                    cmd.addr[1] = reg->r;
                    cmd.addr[2] = 0x00;
                    cmd.addr_lines = TUYA_QSPI_1WIRE;
                    cmd.addr_size = 3;

                    cmd.data = reg->v;
                    cmd.data_lines = TUYA_QSPI_1WIRE;
                    cmd.data_size = reg->len;

                    cmd.dummy_cycle = 0;
                    tkl_qspi_comand(device->display_cfg->port, &cmd);
                    
                    break;
                }

                case TY_INIT_DELAY: {
                    if (seq->delay_time != 0) {
                        tkl_system_sleep(seq->delay_time);
                    }

                    break;
                }

                case TY_INIT_CONF_END: {
                    return 0;
                }

                default: {
                    return -1;
                }
            }
            seq++;
        }
    } else {
        PR_ERR("lcd qspi device init cmd is null");
    }

    return 0;
}

static VOID_T display_qspi_event_cb(TUYA_QSPI_NUM_E port, TUYA_QSPI_IRQ_EVT_E event)
{
    if(event == TUYA_QSPI_EVENT_TX) {      
        if(qspi_disp_manage.tx_sem) {
            tal_semaphore_post(qspi_disp_manage.tx_sem);
        }       
    }
}

static OPERATE_RET display_qspi_frame_data_send(ty_display_qspi_device_t *device, ty_frame_buffer_t *frame_buff)
{
    //cs拉低
    tkl_qspi_force_cs_pin(device->display_cfg->port, 0);
    
    //write 帧前data  非dma
    TUYA_QSPI_CMD_T cmd = {0};
    cmd.op = TUYA_QSPI_WRITE;

    cmd.cmd[0] = device->pixel_pre_cmd.cmd;
    cmd.cmd_size = 1;
    cmd.cmd_lines = device->pixel_pre_cmd.cmd_lines;
    
    for(UINT8_T i = 0; i < device->pixel_pre_cmd.addr_size; i++) {
        cmd.addr[i] = device->pixel_pre_cmd.addr[i];
    }
    cmd.addr_size = device->pixel_pre_cmd.addr_size;
    cmd.addr_lines = device->pixel_pre_cmd.addr_lines;

    cmd.data_size = 0;
    cmd.dummy_cycle = 0;
    tkl_qspi_comand(device->display_cfg->port, &cmd);


    //write pixel data
    tkl_qspi_send(device->display_cfg->port, frame_buff->frame, frame_buff->len);//dma
    OPERATE_RET ret = tal_semaphore_wait(qspi_disp_manage.tx_sem, SEM_WAIT_FOREVER);
    if(ret) {
       PR_ERR("tal_semaphore_wait err %d", ret);
    } 
    //cs拉高
    tkl_qspi_force_cs_pin(device->display_cfg->port, 1);

    return ret;
}

#define QSPI_DISPLAY_FLUSH_INTERVAL 15
void _display_qspi_task(void *args)
{
    qspi_msg_t msg = {0};
    qspi_disp_manage.qspi_task_running = 1;
    while(qspi_disp_manage.qspi_task_running) {
        OPERATE_RET ret = tal_queue_fetch(qspi_disp_manage.queue, &msg, QSPI_DISPLAY_FLUSH_INTERVAL);
        if(ret == OPRT_OK) {
            switch(msg.event) {
                case QSPI_FRAME_REQUEST:
                    display_qspi_frame_data_send(qspi_disp_manage.device, qspi_disp_manage.display_frame);
                    break;

                case QSPI_FRAME_EXIT:
                    qspi_disp_manage.qspi_task_running = 0;
                    do {
                        ret = tal_queue_fetch(qspi_disp_manage.queue, &msg, 0);//no wait
                    } while(ret == 0);
                    break;

                default:
                    break;

            }
        }else {//固定周期刷新
            display_qspi_frame_data_send(qspi_disp_manage.device, qspi_disp_manage.display_frame);
        }
    }

    THREAD_HANDLE qspi_task1 = qspi_disp_manage.qspi_task;
    tal_semaphore_post(qspi_disp_manage.qspi_task_sem);
    tal_thread_delete(qspi_task1);
}
#endif

/**
* @brief tal_display_qspi_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_qspi_open(ty_display_qspi_device_t *device)
{
#if defined(ENABLE_QSPI) && (ENABLE_QSPI==1)
    OPERATE_RET ret = display_qspi_device_gpio_init(device);
    if(ret) return ret;

    ret = tkl_qspi_init(device->display_cfg->port, &(device->qspi_cfg));
    if(ret) return ret;

    ret = tkl_qspi_irq_init(device->display_cfg->port, display_qspi_event_cb);
    if(ret) return ret;

    ret = tkl_qspi_irq_enable(device->display_cfg->port);
    if(ret) return ret;

    ret = display_qspi_init_seq_send(device);
    if(ret) return ret;

    if(qspi_disp_manage.mutex == NULL) {
        ret = tal_mutex_create_init(&(qspi_disp_manage.mutex));
        if(ret) return ret;
    }

    ret = tal_semaphore_create_init(&(qspi_disp_manage.tx_sem), 0, 1);
    if(ret) return ret;

    qspi_disp_manage.device = device;
    if(device->has_pixel_memory == 0) {
        qspi_disp_manage.display_frame = NULL;
        ret = tal_queue_create_init(&(qspi_disp_manage.queue), sizeof(qspi_msg_t), 32);
        if(ret) return ret;

        ret = tal_semaphore_create_init(&(qspi_disp_manage.qspi_task_sem), 0, 1);
        if(ret) return ret;

        THREAD_CFG_T thread_cfg = {4096, TAL_DISPLAY_PRI, "display_qspi_task"};
        ret |= tal_thread_create_and_start(&(qspi_disp_manage.qspi_task), NULL, NULL, _display_qspi_task, NULL, &thread_cfg);
        if(ret) return ret;
   }
    
    return 0;
#else
    return -1;
#endif
}

/**
* @brief tal_display_qspi_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_qspi_close(ty_display_qspi_device_t *device)
{
#if defined(ENABLE_QSPI) && (ENABLE_QSPI==1)
    tal_mutex_lock(qspi_disp_manage.mutex);
    if(device->has_pixel_memory == 0) {
        qspi_msg_t msg = {QSPI_FRAME_EXIT, 0};
        tal_queue_post(qspi_disp_manage.queue, &msg , SEM_WAIT_FOREVER);
        tal_semaphore_wait(qspi_disp_manage.qspi_task_sem, SEM_WAIT_FOREVER);
    }

    display_qspi_device_gpio_deinit(device);

    //disable qspi irq
    tkl_qspi_irq_disable(device->display_cfg->port);

    //qspi deint
    tkl_qspi_deinit(device->display_cfg->port);

    tal_semaphore_release(qspi_disp_manage.tx_sem);
   
    if(device->has_pixel_memory == 0) {
        tal_semaphore_release(qspi_disp_manage.qspi_task_sem);
        tal_queue_free(qspi_disp_manage.queue);
        qspi_disp_manage.queue = NULL;
        qspi_disp_manage.display_frame = NULL;
        qspi_disp_manage.qspi_task = NULL;
    }

    qspi_disp_manage.device = NULL;
    tal_mutex_unlock(qspi_disp_manage.mutex);

    return 0;
#else
    return -1;
#endif
}

/**
* @brief tal_display_qspi_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_qspi_flush(ty_display_qspi_device_t *device, ty_frame_buffer_t *frame_buff)
{
#if defined(ENABLE_QSPI) && (ENABLE_QSPI==1)
    //lock
    tal_mutex_lock(qspi_disp_manage.mutex);

    if(device->has_pixel_memory) {//有帧缓存的情况，直接刷屏
        if(qspi_disp_manage.device) {
            display_qspi_frame_data_send(qspi_disp_manage.device, frame_buff);
        }     
    }else {//没有帧缓存的情况，通过另一个线程固定周期刷屏
        if(qspi_disp_manage.qspi_task_running) {
            qspi_disp_manage.display_frame = frame_buff;
            qspi_msg_t msg = {QSPI_FRAME_REQUEST, frame_buff};
            tal_queue_post(qspi_disp_manage.queue, &msg , SEM_WAIT_FOREVER);
        }
    }

    tal_mutex_unlock(qspi_disp_manage.mutex);
    //unlock
    return 0;
#else
    return -1;
#endif
}

