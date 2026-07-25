#include "tal_display_rgb.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "tal_queue.h"
#include "tal_mutex.h"
#include "ty_soft_spi.h"
#include "uni_log.h"
#if defined(ENABLE_RGB_DISPLAY) && (ENABLE_RGB_DISPLAY==1)
#include "tkl_rgb.h"
#endif
#include "tuya_app_config.h"

#ifndef TAL_DISPLAY_PRI
#define TAL_DISPLAY_PRI THREAD_PRIO_0
#endif

#if defined(ENABLE_RGB_DISPLAY) && (ENABLE_RGB_DISPLAY==1)
#define UNACTIVE_LEVEL(level)  ((level == 1) ? 0 : 1)

typedef struct {
	UINT8_T rgb_task_running;
	ty_frame_buffer_t *pingpong_frame;
	ty_frame_buffer_t *display_frame;
	SEM_HANDLE flush_sem;
	SEM_HANDLE rgb_task_sem;
    MUTEX_HANDLE mutex;//for close&flush sync
	THREAD_HANDLE rgb_task;
	QUEUE_HANDLE queue;
    soft_spi_gpio_cfg_t soft_spi_gpio;
} rgb_disp_manage_t;

typedef enum {
    RGB_FRAME_REQUEST = 0,
    RGB_FRAME_RELEASE,
    RGB_FRAME_EXIT,
};

typedef struct {
	UINT32_T event;
	UINT32_T param;
} rgb_msg_t;

rgb_disp_manage_t rgb_manage = {0};
rgb_disp_manage_t *prgb_manage = &rgb_manage;

static void mutex_init_if_not(MUTEX_HANDLE *mutex)
{
    if(*mutex == NULL) {
        tal_mutex_create_init(mutex);
    }
}

//__attribute__((section(".itcm_sec_code")))
static void display_rgb_isr(TUYA_RGB_EVENT_E event)
{
	//GLOBAL_INT_DECLARATION();
	if (prgb_manage->pingpong_frame != NULL) {
		if (prgb_manage->display_frame != NULL) {
			OPERATE_RET ret = 0;

			//GLOBAL_INT_DISABLE();

			if (prgb_manage->pingpong_frame != prgb_manage->display_frame) {
				if (prgb_manage->display_frame->width != prgb_manage->pingpong_frame->width ||
                    prgb_manage->display_frame->height != prgb_manage->pingpong_frame->height) {
                    tkl_rgb_ppi_set(prgb_manage->pingpong_frame->width, prgb_manage->pingpong_frame->height);
				}

				if (prgb_manage->display_frame->fmt != prgb_manage->pingpong_frame->fmt) {
					tkl_rgb_pixel_mode_set(prgb_manage->pingpong_frame->fmt);
				}

                /*lvgl静态frame buff，不需要释放，free_cb为NULL
                **camera的frame buff不是静态的，需要动态申请释放，在camera侧申请，在这里释放，free_cb不能为NULL
                */
               if (prgb_manage->display_frame->free_cb != NULL) {
                    rgb_msg_t rel_msg = {RGB_FRAME_RELEASE, (UINT32_T)prgb_manage->display_frame};
                    tal_queue_post(rgb_manage.queue, &rel_msg, 0);
                }
			}
			prgb_manage->display_frame = prgb_manage->pingpong_frame;
			prgb_manage->pingpong_frame = NULL;
			tkl_rgb_base_addr_set((UINT32_T)prgb_manage->display_frame->frame);

			//GLOBAL_INT_RESTORE();
			tal_semaphore_post(prgb_manage->flush_sem);
		}else {
			//GLOBAL_INT_DISABLE();
			prgb_manage->display_frame = prgb_manage->pingpong_frame;
			prgb_manage->pingpong_frame = NULL;
			//GLOBAL_INT_RESTORE();
			tal_semaphore_post(prgb_manage->flush_sem);
		}
	}

}

static OPERATE_RET rgb_display_frame(ty_frame_buffer_t *frame)
{
	OPERATE_RET ret = 0;
	if (prgb_manage->display_frame == NULL) {
		tkl_rgb_ppi_set(frame->width, frame->height);
		tkl_rgb_pixel_mode_set(frame->fmt);
		prgb_manage->pingpong_frame = frame;

		tkl_rgb_base_addr_set((uint32_t)frame->frame);
		tkl_rgb_display_transfer_start();
	}else {
		if (prgb_manage->pingpong_frame != NULL) {
			PR_ERR("pingpong_frame can't be !NULL");
		}

        //GLOBAL_INT_DECLARATION();
        //GLOBAL_INT_DISABLE();
		prgb_manage->pingpong_frame = frame;
        //GLOBAL_INT_RESTORE();
	}

	ret = tal_semaphore_wait(prgb_manage->flush_sem, SEM_WAIT_FOREVER);
	if (ret != 0){
		PR_DEBUG("%s semaphore get failed: %d", __func__, ret);
	}

	return ret;
}


void __rgb_task(void *args)
{
    prgb_manage->rgb_task_running = 1;
    rgb_msg_t msg = {0};
    OPERATE_RET ret = 0;
    while(prgb_manage->rgb_task_running) {
        ret = tal_queue_fetch(rgb_manage.queue, &msg, SEM_WAIT_FOREVER);
        if(ret == OPRT_OK) {
            switch(msg.event) {
                case RGB_FRAME_REQUEST:
                    rgb_display_frame((ty_frame_buffer_t *)msg.param);
                    break;

                case RGB_FRAME_RELEASE: {
                    ty_frame_buffer_t *frame = (ty_frame_buffer_t *)msg.param;
                    if (frame && frame->free_cb) {
                        frame->free_cb(frame);
                    }
                    break;
                }

                case RGB_FRAME_EXIT:
                    prgb_manage->rgb_task_running = false;
                    do {
                        ret = tal_queue_fetch(rgb_manage.queue, &msg, 0);//no wait
                        if (msg.event == RGB_FRAME_REQUEST || msg.event == RGB_FRAME_RELEASE) {
                            ty_frame_buffer_t *frame = (ty_frame_buffer_t *)msg.param;
                            if(frame && frame->free_cb) {
                                frame->free_cb(frame);
                            }
                        }
                    } while(ret == 0);
                    break;

                default:
                    break;

            }
        }
    }

    tal_semaphore_post(prgb_manage->rgb_task_sem);
    tal_thread_delete(prgb_manage->rgb_task);

    return;
}

static OPERATE_RET __init_with_soft_spi(ty_display_rgb_device_t *device)
{
    if(NULL == device || NULL == device->display_cfg) {
        return OPRT_INVALID_PARM;
    }

    prgb_manage->soft_spi_gpio.spi_clk = device->display_cfg->spi_clk;
    prgb_manage->soft_spi_gpio.spi_csx= device->display_cfg->spi_csx;
    prgb_manage->soft_spi_gpio.spi_sda = device->display_cfg->spi_sda;
    OPERATE_RET ret = ty_soft_spi_init(&prgb_manage->soft_spi_gpio);
    if(ret) return ret;

    if(NULL == device->init_seq) {
        PR_WARN("rgb device has no init_seq..");
        return 0;
    }

    DISPLAY_INIT_SEQ_T *seq = device->init_seq;
    for (;;) {
        switch (seq->type) {
            case TY_INIT_RST: {
                // reset
                DISPLAY_RESET_DATA_T *reset = seq->reset;
                int rst_gpio = device->display_cfg->reset.pin;
                TUYA_GPIO_LEVEL_E active_level = device->display_cfg->reset.active_level;
                TUYA_GPIO_LEVEL_E unactive_level = (active_level == TUYA_GPIO_LEVEL_LOW) ? TUYA_GPIO_LEVEL_HIGH : TUYA_GPIO_LEVEL_LOW;

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
                ty_soft_spi_write_cmd(&prgb_manage->soft_spi_gpio, reg->r);
                for(UINT32_T i = 0; i < reg->len; i++) {
                    ty_soft_spi_write_data(&prgb_manage->soft_spi_gpio, reg->v[i]);
                }

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

    return OPRT_OK;
}
#endif

/**
* @brief tal_display_rgb_open
*
* @param[in] device: device info
*
* @note This API is used to open display device.
*
* @return id >= 0 on success. Others on error
*/
OPERATE_RET tal_display_rgb_open(ty_display_rgb_device_t *device)
{
#if defined(ENABLE_RGB_DISPLAY) && (ENABLE_RGB_DISPLAY==1)
    OPERATE_RET ret = 0;
    TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT};
    //bl gpio设置
    if(device->display_cfg->bl.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->bl.active_level);
        ret |= tkl_gpio_init(device->display_cfg->bl.pin, &cfg);
    }

    //reset引脚设置
    if(device->display_cfg->reset.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = UNACTIVE_LEVEL(device->display_cfg->reset.active_level);
        ret |= tkl_gpio_init(device->display_cfg->reset.pin, &cfg);
    }

    //power ctrl引脚初始化，且给显示屏供电
    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        cfg.level = device->display_cfg->power_ctrl.active_level;
        ret |= tkl_gpio_init(device->display_cfg->power_ctrl.pin, &cfg);
    }

    ret |= tal_semaphore_create_init(&(rgb_manage.flush_sem), 0, 1);
    ret |= tal_semaphore_create_init(&(rgb_manage.rgb_task_sem), 0, 1);
    ret |= tal_queue_create_init(&(rgb_manage.queue), sizeof(rgb_msg_t), 32);

    THREAD_CFG_T thread_cfg = {4096, TAL_DISPLAY_PRI, "rgb_task"};
    ret |= tal_thread_create_and_start(&(rgb_manage.rgb_task), NULL, NULL, __rgb_task, NULL, &thread_cfg);
    if(ret) return ret;

    //用io模拟的spi给屏做初始化
    ret = __init_with_soft_spi(device);

    ret |= tkl_rgb_init(&(device->cfg));
    if(ret) return ret;

    return tkl_rgb_irq_cb_register(display_rgb_isr);
#else
    return -1;
#endif
}

/**
* @brief tal_display_rgb_close
*
* @param[in] device: device info
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_rgb_close(ty_display_rgb_device_t *device)
{
#if defined(ENABLE_RGB_DISPLAY) && (ENABLE_RGB_DISPLAY==1)
    mutex_init_if_not(&prgb_manage->mutex);
    //lock
    tal_mutex_lock(prgb_manage->mutex);
    rgb_msg_t msg = {RGB_FRAME_EXIT, 0};
    tal_queue_post(rgb_manage.queue, &msg , SEM_WAIT_FOREVER);
    tal_semaphore_wait(rgb_manage.rgb_task_sem, 5000);
    rgb_manage.rgb_task = NULL;

    tal_semaphore_release(rgb_manage.flush_sem);
    rgb_manage.flush_sem = NULL;

    tal_semaphore_release(rgb_manage.rgb_task_sem);
    rgb_manage.rgb_task_sem = NULL;

    tal_queue_free(rgb_manage.queue);
    rgb_manage.queue = NULL;

    tkl_rgb_display_transfer_stop();
    tkl_rgb_deinit();

    //power ctrl引脚给屏断电
    if(device->display_cfg->power_ctrl.pin < TUYA_GPIO_NUM_MAX) {
        tkl_gpio_write(device->display_cfg->power_ctrl.pin, UNACTIVE_LEVEL(device->display_cfg->power_ctrl.active_level));
        tkl_gpio_deinit(device->display_cfg->reset.pin);//是否需要deinit，调低功耗时再确认
    }

    if(device->display_cfg->reset.pin < TUYA_GPIO_NUM_MAX) {
        tkl_gpio_deinit(device->display_cfg->reset.pin);
    }

    if(device->display_cfg->bl.pin < TUYA_GPIO_NUM_MAX) {
        tkl_gpio_deinit(device->display_cfg->bl.pin);
    }
    
    tkl_gpio_deinit(device->display_cfg->spi_clk);
    tkl_gpio_deinit(device->display_cfg->spi_csx);
    tkl_gpio_deinit(device->display_cfg->spi_sda);

    if(rgb_manage.display_frame) {
        if(rgb_manage.display_frame->free_cb)
            rgb_manage.display_frame->free_cb(rgb_manage.display_frame);
        rgb_manage.display_frame = NULL;
    }

    if(rgb_manage.pingpong_frame) {
        if(rgb_manage.pingpong_frame->free_cb)
            rgb_manage.pingpong_frame->free_cb(rgb_manage.pingpong_frame);
        rgb_manage.pingpong_frame = NULL;
    }


    tal_mutex_unlock(prgb_manage->mutex);
    //unlock
    return 0;
#else
    return -1;
#endif

}

/**
* @brief tal_display_rgb_flush
*
* @param[in] device: device info
* @param[in] frame_buff: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_rgb_flush(ty_display_rgb_device_t *device, ty_frame_buffer_t *frame_buff)
{
#if defined(ENABLE_RGB_DISPLAY) && (ENABLE_RGB_DISPLAY==1)
    mutex_init_if_not(&prgb_manage->mutex);
    //lock
    tal_mutex_lock(prgb_manage->mutex);
    if(prgb_manage->rgb_task_running) {
        rgb_msg_t msg = {RGB_FRAME_REQUEST, frame_buff};
        tal_queue_post(rgb_manage.queue, &msg , SEM_WAIT_FOREVER);
    }
    tal_mutex_unlock(prgb_manage->mutex);
    //unlock
    return 0;
#else
    return -1;
#endif
}
