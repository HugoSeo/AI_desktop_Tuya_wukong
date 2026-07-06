#include "tal_tp_service.h"
#include "tal_tp_i2c.h"
#include "tal_mutex.h"
#include "tal_queue.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "tal_system.h"
#include "tal_memory.h"
#include "tkl_gpio.h"

#define UNACTIVE_LEVEL(x) ((x == 0)? 1: 0)

typedef struct
{
    ty_tp_read_point_t point_log;
    uint8_t flag;
} tp_log_t;


typedef struct
{
    QUEUE_HANDLE point_queue;
    uint8_t point_cnt;
    MUTEX_HANDLE mutex;
    SEM_HANDLE sem;
    THREAD_HANDLE thread_handle;
    uint16_t width;
    uint16_t height;
    ty_tp_mirror_type_t mirror_type;
    ty_tp_read_data_t read_data;
    tp_log_t *hist_log;
    tp_log_t *cur_log;
} tp_manage_t;

static tp_manage_t g_tp_manage = {0};
static ty_tp_read_data_t *tp_read_data = NULL;
static ty_tp_read_point_t *tp_point_arr = NULL;
static tp_log_t *tp_cur_log = NULL;
static tp_log_t *tp_hist_log = NULL;

static OPERATE_RET tal_tp_write(uint16_t x, uint16_t y, uint16_t state)
{
    OPERATE_RET ret = 0;
    ty_tp_point_info_t *point = NULL;

    do
    {
        point = Malloc(sizeof(ty_tp_point_info_t));
        if (!point)
        {
            PR_DEBUG("%s, malloc failed!\r\n", __func__);
            ret = OPRT_MALLOC_FAILED;
            break;
        }

        memset(point, 0, sizeof(ty_tp_point_info_t));

        /* Coordinate Axis Transform */
        if (g_tp_manage.mirror_type == TP_MIRROR_NONE)
        {
            point->m_x = x;
            point->m_y = y;
        }
        else if (g_tp_manage.mirror_type == TP_MIRROR_X_COORD)
        {
            point->m_x = g_tp_manage.width - x - 1;
            point->m_y = y;
        }
        else if (g_tp_manage.mirror_type == TP_MIRROR_Y_COORD)
        {
            point->m_x = x;
            point->m_y = g_tp_manage.height- y - 1;
        }
        else if (g_tp_manage.mirror_type == TP_MIRROR_X_Y_COORD)
        {
            point->m_x = g_tp_manage.width - x - 1;
            point->m_y = g_tp_manage.height- y - 1;
        }

        point->m_state = state;

        tal_mutex_lock(g_tp_manage.mutex);
        if (g_tp_manage.point_cnt < TY_TP_DATA_QUEUE_MAX_SIZE)
        {
            tal_queue_post(g_tp_manage.point_queue, &point, SEM_WAIT_FOREVER);
            g_tp_manage.point_cnt++;
        }
        else
        {
            PR_DEBUG("%s, tp data queue is full!\r\n", __func__);
            ret = OPRT_EXCEED_UPPER_LIMIT;
            Free(point);
        }
        tal_mutex_unlock(g_tp_manage.mutex);

    } while(0);

    return ret;
}

static void tuya_tp_read_info_callback(ty_tp_read_point_t *tp_point)
{
    uint8_t idx = tp_point->tp_point_id;

	// write tp data to queue
	if (tp_point->tp_point_event == TP_EVENT_PRESS_DOWN)
	{
		tal_tp_write(tp_point->tp_point_x, tp_point->tp_point_y, 1);
	}
	else if (tp_point->tp_point_event == TP_EVENT_CONTACT_MOVE)
	{
		if ( (tp_cur_log[idx].point_log.tp_timestamp - tp_hist_log[idx].point_log.tp_timestamp ) >= 5 )
		{
			tal_tp_write(tp_point->tp_point_x, tp_point->tp_point_y, 1);
		}
	}
	else if (tp_point->tp_point_event == TP_EVENT_RELEASE_UP)
	{
		tal_tp_write(tp_point->tp_point_x, tp_point->tp_point_y, 0);
	}
}

static void __tp_task(void *args)
{
    OPERATE_RET ret = 0;

    ty_tp_device_cfg_t *device = (ty_tp_device_cfg_t *)args;
    if (!device || !tp_point_arr || !tp_cur_log || !tp_hist_log)
        return;

    ty_tp_usr_cfg_t *usr_cfg = device->tp_cfg;
    if (!usr_cfg)
        return;

    while (1)
    {
        ret = tal_semaphore_wait_forever(g_tp_manage.sem);
        if (ret)
        {
            goto next_round;
        }

        tp_read_data->point_cnt = 0;
        memset(tp_point_arr, 0, device->max_support_tp_num * sizeof(ty_tp_read_point_t));
        memset(tp_cur_log, 0, device->max_support_tp_num * sizeof(tp_log_t));

        if (device->read_pont_info)
        {
            uint8_t idx = 0;

            device->read_pont_info(tp_read_data);

            for (uint8_t i = 0; i < tp_read_data->point_cnt; ++i)
            {
                tp_point_arr[i].tp_timestamp = rtos_get_time();

                idx = tp_point_arr[i].tp_point_id;
                if (idx < 0 || idx >= device->max_support_tp_num)
                {
                    continue;
                }

                memcpy(&(tp_cur_log[idx].point_log), &(tp_point_arr[i]), sizeof(ty_tp_read_point_t));
                if (tp_point_arr[i].tp_point_event != TP_EVENT_RELEASE_UP)
                    tp_cur_log[idx].flag = TRUE;

                if (tp_read_data->support_get_event)
                    tuya_tp_read_info_callback(&tp_point_arr[i]);
            }

            if (!tp_read_data->support_get_event)
            {
                for (uint8_t i = 0; i < device->max_support_tp_num; ++i)
                {
                    if (tp_cur_log[i].flag && !tp_hist_log[i].flag)
                    {
                        tp_cur_log[i].point_log.tp_point_event = TP_EVENT_PRESS_DOWN;
                        tuya_tp_read_info_callback(&(tp_cur_log[i].point_log));
                    }
                    else if (!tp_cur_log[i].flag && tp_hist_log[i].flag)
                    {
                        tp_hist_log[i].point_log.tp_point_event = TP_EVENT_RELEASE_UP;
                        tuya_tp_read_info_callback(&(tp_hist_log[i].point_log));
                    }
                    else if (tp_cur_log[i].flag && tp_hist_log[i].flag)
                    {
                        tp_cur_log[i].point_log.tp_point_event = TP_EVENT_CONTACT_MOVE;
                        tuya_tp_read_info_callback(&(tp_cur_log[i].point_log));
                    }
                }                
            }

            memcpy(tp_hist_log, tp_cur_log, device->max_support_tp_num * sizeof(tp_log_t));
        }
next_round:
        tkl_gpio_irq_enable(usr_cfg->tp_intr.pin);
    }
}

static void tuya_tp_isr_cb(void *args)
{
    ty_tp_device_cfg_t *device = (ty_tp_device_cfg_t *)args;
    if (!device)
        return;

    ty_tp_usr_cfg_t *usr_cfg = device->tp_cfg;
    if (!usr_cfg)
        return;

    tkl_gpio_irq_disable(usr_cfg->tp_intr.pin);

    if (g_tp_manage.sem)
        tal_semaphore_post(g_tp_manage.sem);
    return;
}

static OPERATE_RET tuya_tp_driver_init(ty_tp_device_cfg_t *device)
{
	OPERATE_RET ret = OPRT_OK;
    TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT};

	PR_DEBUG("%s, width=%d, height=%d int_type=%d, refresh_rate=%d, tp_num=%d.\r\n", __func__, 
        device->width, device->height, device->intr_type, device->refresh_rate, device->max_support_tp_num);

    // pwr ctrl引脚初始化
    if (device->tp_cfg->tp_pwr_ctrl.pin < TUYA_GPIO_NUM_MAX)
    {
        cfg.level = device->tp_cfg->tp_pwr_ctrl.active_level;
        ret = tkl_gpio_init(device->tp_cfg->tp_pwr_ctrl.pin, &cfg);
        // 时延等待取值参考原生code，可优化
        tal_system_sleep(20);
    }

    // rst引脚初始化，必须先拉低输出后再拉高输出，且中间要有一定间隔，tp才正常启动
    cfg.level = UNACTIVE_LEVEL(device->tp_cfg->tp_rst.active_level);
    ret = tkl_gpio_init(device->tp_cfg->tp_rst.pin, &cfg);
    tal_system_sleep(20);
    tkl_gpio_write(device->tp_cfg->tp_rst.pin, device->tp_cfg->tp_rst.active_level);

    // int引脚初始化
    cfg.direct = TUYA_GPIO_INPUT;
    if (device->intr_type == TUYA_GPIO_IRQ_RISE)
        cfg.mode = TUYA_GPIO_PULLDOWN;
    else if (device->intr_type == TUYA_GPIO_IRQ_FALL)
        cfg.mode = TUYA_GPIO_PULLUP;
    else
        cfg.mode = TUYA_GPIO_FLOATING;
    ret = tkl_gpio_init(device->tp_cfg->tp_intr.pin, &cfg);

    // int回调注册
    TUYA_GPIO_IRQ_T irq_cfg = {.mode = device->intr_type, .cb = tuya_tp_isr_cb, device};
    ret = tkl_gpio_irq_init(device->tp_cfg->tp_intr.pin, &irq_cfg);
    ret = tkl_gpio_irq_enable(device->tp_cfg->tp_intr.pin);

    // i2c初始化
    uint8_t tp_i2c_clk = device->tp_cfg->tp_i2c_clk.pin;
    uint8_t tp_i2c_sda = device->tp_cfg->tp_i2c_sda.pin;
    TUYA_I2C_NUM_E tp_i2c_idx = device->tp_cfg->tp_i2c_idx;
    ret = tal_tp_i2c_init(tp_i2c_clk, tp_i2c_sda, tp_i2c_idx);

    // hy4633说明rst拉高1000ms后可进行i2c操作，可将此操作移动到init接口下
	tal_system_sleep(1000);

	if (device->init)
        device->init(device);

	return ret;
}

static void release_tp_cache(ty_tp_device_cfg_t *device)
{
    if (g_tp_manage.mutex == NULL)
        return;

    tal_mutex_lock(g_tp_manage.mutex);

    if (g_tp_manage.thread_handle)
    {
        tal_thread_delete(g_tp_manage.thread_handle);
        g_tp_manage.thread_handle = NULL;
    }

    if (g_tp_manage.cur_log)
    {
        Free(g_tp_manage.cur_log);
        g_tp_manage.cur_log = NULL;
        tp_cur_log = NULL;
    }

    if (g_tp_manage.hist_log)
    {
        Free(g_tp_manage.hist_log);
        g_tp_manage.hist_log = NULL;
        tp_hist_log = NULL;
    }

    if (g_tp_manage.read_data.point_arr)
    {
        Free(g_tp_manage.read_data.point_arr);
        g_tp_manage.read_data.point_arr = NULL;
        tp_point_arr = NULL;
    }

    if (g_tp_manage.point_queue)
    {
        while (g_tp_manage.point_cnt)
        {
            ty_tp_point_info_t *tp_point = NULL;
            tal_queue_fetch(g_tp_manage.point_queue, &tp_point, 0);
            Free(tp_point);
            g_tp_manage.point_cnt--;
        }

        tal_queue_free(g_tp_manage.point_queue);
        g_tp_manage.point_queue = NULL;
    }

    if (g_tp_manage.sem)
    {
        tal_semaphore_release(g_tp_manage.sem);
        g_tp_manage.sem = NULL;
    }

    ty_tp_usr_cfg_t *display_cfg = NULL;
    if (device)
    {
        display_cfg = device->tp_cfg;
        Free(device);
    }

    if (display_cfg)
        Free(display_cfg);

    tal_mutex_unlock(g_tp_manage.mutex);

    tal_mutex_release(g_tp_manage.mutex);
}

TY_TP_HANDLE tal_tp_open(ty_tp_device_cfg_t *device, ty_tp_usr_cfg_t *cfg)
{
    OPERATE_RET ret = OPRT_OK;
    ty_tp_device_cfg_t *handle = NULL;
    ty_tp_usr_cfg_t *display_cfg = NULL;
    tp_read_data = &(g_tp_manage.read_data);

    if (device == NULL || cfg == NULL)
        goto failed;

    ret = tal_mutex_create_init(&(g_tp_manage.mutex));
    if (ret)
        goto failed;

    ret = tal_queue_create_init(&(g_tp_manage.point_queue), 4, TY_TP_DATA_QUEUE_MAX_SIZE);
    if (ret)
        goto failed;

    ret = tal_semaphore_create_init(&(g_tp_manage.sem), 0, 1);
    if (ret)
        goto failed;

    handle = Malloc(sizeof(ty_tp_device_cfg_t));
    if (handle == NULL)
        goto failed;
    
    memcpy(handle, device, sizeof(ty_tp_device_cfg_t));

    display_cfg = Malloc(sizeof(ty_tp_usr_cfg_t));
    if (display_cfg == NULL)
        goto failed;

    memcpy(display_cfg, cfg, sizeof(ty_tp_usr_cfg_t));
    handle->tp_cfg = display_cfg;

    tp_point_arr = Malloc(device->max_support_tp_num * sizeof(ty_tp_read_point_t));
    if (tp_point_arr == NULL)
        goto failed;

    memset(tp_point_arr, 0, device->max_support_tp_num * sizeof(ty_tp_read_point_t));
    g_tp_manage.read_data.point_arr = tp_point_arr;

    tp_cur_log = Malloc(device->max_support_tp_num * sizeof(tp_log_t));
    if (tp_cur_log == NULL)
        goto failed;

    memset(tp_cur_log, 0, device->max_support_tp_num * sizeof(tp_log_t));
    g_tp_manage.cur_log = tp_cur_log;

    tp_hist_log = Malloc(device->max_support_tp_num * sizeof(tp_log_t));
    if (tp_hist_log == NULL)
        goto failed;

    memset(tp_hist_log, 0, device->max_support_tp_num * sizeof(tp_log_t));
    g_tp_manage.hist_log = tp_hist_log;

    g_tp_manage.width = device->width;
    g_tp_manage.height = device->height;
    g_tp_manage.mirror_type = device->mirror_type;

	ret = tuya_tp_driver_init(handle);
    if (ret)
        goto failed;

    if (g_tp_manage.thread_handle == NULL)
    {
        THREAD_CFG_T thread_cfg = {4096, THREAD_PRIO_1, "tp_task"};
        ret = tal_thread_create_and_start(&(g_tp_manage.thread_handle), NULL, NULL, __tp_task, handle, &thread_cfg);
    }
    if (ret)
        goto failed;

    return handle;

failed:
    release_tp_cache(handle);

    return NULL;
}

static OPERATE_RET tuya_tp_driver_deinit(ty_tp_device_cfg_t *device)
{
    ty_tp_usr_cfg_t *display_cfg = device->tp_cfg;
    if (!display_cfg)
    {
        PR_DEBUG("%s, input args is null!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

    // pwr ctrl引脚断电
    if (display_cfg->tp_pwr_ctrl.pin < TUYA_GPIO_NUM_MAX)
    {
        tkl_gpio_write(display_cfg->tp_pwr_ctrl.pin, UNACTIVE_LEVEL(display_cfg->tp_pwr_ctrl.active_level));
        tkl_gpio_deinit(display_cfg->tp_pwr_ctrl.pin); //是否需要deinit，调低功耗时再确认
    }

    tkl_gpio_deinit(device->tp_cfg->tp_rst.pin);

    tkl_gpio_irq_disable(device->tp_cfg->tp_intr.pin);
    tkl_gpio_deinit(device->tp_cfg->tp_intr.pin);

    tal_tp_i2c_deinit();

    return OPRT_OK;
}

OPERATE_RET tal_tp_close(TY_TP_HANDLE handle)
{
    if (!handle)
    {
        PR_DEBUG("%s, input args is null!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

    OPERATE_RET ret = OPRT_OK;
    ret = tuya_tp_driver_deinit(handle);

    release_tp_cache(handle);

    return ret;
}

OPERATE_RET tal_tp_read(ty_tp_point_info_t *point)
{
    OPERATE_RET ret = OPRT_OK;
    ty_tp_point_info_t *tp_point = NULL;

    do
    {
        if (!point)
        {
            PR_DEBUG("%s, input args is null!\r\n", __func__);
            ret = OPRT_INVALID_PARM;
            break;
        }

        tal_mutex_lock(g_tp_manage.mutex);
        if (g_tp_manage.point_cnt == 0)
        {
            ret = OPRT_EXCEED_UPPER_LIMIT;
            tal_mutex_unlock(g_tp_manage.mutex);
            break;
        }

        tal_queue_fetch(g_tp_manage.point_queue, &tp_point, SEM_WAIT_FOREVER);
        if (tp_point)
        {
            g_tp_manage.point_cnt--;
            memcpy(point, tp_point, sizeof(ty_tp_point_info_t));
            if (g_tp_manage.point_cnt)
                point->m_need_continue = 1;
            else
                point->m_need_continue = 0;

            Free(tp_point);
            tp_point = NULL;
        }
        else
        {
            ret = OPRT_COM_ERROR;
        }
        tal_mutex_unlock(g_tp_manage.mutex);
    } while (0);
    
    return ret;
}
