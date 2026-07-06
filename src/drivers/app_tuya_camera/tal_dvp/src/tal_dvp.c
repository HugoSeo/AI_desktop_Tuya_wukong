#include "tuya_app_config.h"
#include "tal_dvp.h"
#include "tal_semaphore.h"
#include "tal_thread.h"
#include "tkl_thread.h"
#include "tal_queue.h"
#include "tal_mutex.h"
#include "tal_memory.h"
#include "tkl_system.h"
#include "tkl_gpio.h"
#include "tkl_i2c.h"
#include "tkl_dvp.h"
#include "tkl_pinmux.h"
#include "tuya_list.h"
#include "tal_system.h"
#include "tal_sw_timer.h"
#include "uni_log.h"

#define UNACTIVE_LEVEL(x) 	((x == 0)? 1: 0)
#define DVP_I2C_SCL 		(idx << 1)
#define DVP_I2C_SDA 		((idx << 1) + 1)

#define DVP_PER_MAX_PIXEL_SIZE			(3)
#define DVP_MIN_COMPRESSION_PERCENT		(20) // uint: %
#define DVP_SLICE_SIZE					(1500)
#define DVP_ENCODED_NODE_POOL_SIZE		(DVP_NODE_POOL_SIZE << 2)
#define DVP_LOG_TIMER_INTERVAL			(4) // uint: second

#define TWO_BYTE						(2)
#define THREE_BYTE						(3)
#define FOUR_BYTE						(4)

#define MAX_DETECT_RETRY_TIMES			(3)

typedef enum
{
    BASE_FRAME_FLOW = 0,
    ENCODED_FRAME_FLOW,
} DVP_WORK_FLOW_T;

typedef enum
{
	DVP_STATUS_TURN_OFF,
	DVP_STATUS_TURNING_OFF,
	DVP_STATUS_TURNING_ON,
	DVP_STATUS_TURN_ON,
} DVP_STATUS;

typedef struct DVP_NODE_MANAGE_S
{
    TUYA_DVP_FRAME_MANAGE_T dvp_frame_manage;
    struct DVP_NODE_MANAGE_S *next;
} DVP_NODE_MANAGE_T;

typedef struct
{
	UINT32_T base_frame_id;
	UINT32_T prev_base_frame_id;
	UINT32_T encoded_frame_id;
	UINT32_T prev_encoded_frame_id;
	UINT32_T total_base_byte;
	UINT32_T prev_base_byte;
	UINT32_T total_encoded_byte;
	UINT32_T prev_encoded_byte;
	UINT32_T compress_ratio;
	UINT32_T base_len;
	TIMER_ID dvp_log_timer_idx;
	UINT32_T fps;
	UINT32_T kbps;
} DVP_LOG_T;

typedef struct
{
	QUEUE_HANDLE base_frame_queue;
	QUEUE_HANDLE encoded_frame_queue;
    DVP_NODE_MANAGE_T *free_base_node_list;
    DVP_NODE_MANAGE_T base_node_pool[DVP_NODE_POOL_SIZE];
    DVP_NODE_MANAGE_T *free_encoded_node_list;
    DVP_NODE_MANAGE_T encoded_node_pool[DVP_ENCODED_NODE_POOL_SIZE];
	VOID *base_buf_ptr;
	VOID *encoded_buf_ptr;
	TUYA_I2C_NUM_E dvp_i2c_idx;
	UINT8_T base_flow_flag;
	UINT8_T encoded_flow_flag;
	TKL_THREAD_HANDLE base_flow_thread;
	TKL_THREAD_HANDLE encoded_flow_thread;
	UINT8_T base_flow_running;
	UINT8_T encoded_flow_running;
	DVP_FRAME_HANDLE dvp_frame_handle;
	VOID *frame_user_data;   /* passed as-is to dvp_frame_handle */
	SEM_HANDLE base_flow_closed_sem;
	SEM_HANDLE encoded_flow_closed_sem;
	DVP_STATUS dvp_status;
	TUYA_DVP_DEVICE_T *dvp_device;
	DVP_LOG_T dvp_log;
} DVP_MANAGE_T;

typedef enum {
    DVP_FRAME_REQUEST = 0,
    DVP_FRAME_EXIT,
};

typedef struct {
	UINT32_T event;
	UINT32_T param;
} TAL_DVP_MSG_T;

static DVP_MANAGE_T g_dvp_manage =
{
	.dvp_i2c_idx = TUYA_I2C_NUM_MAX,
	.base_flow_flag = false,
	.base_flow_running = false,
	.encoded_flow_flag = false,
	.encoded_flow_running = false,
	.dvp_status = DVP_STATUS_TURN_OFF,
	.dvp_log = 
	{
		.base_frame_id = 0,
		.prev_base_frame_id = 0,
		.encoded_frame_id = 0,
		.prev_encoded_frame_id = 0,
		.total_base_byte = 0,
		.prev_base_byte = 0,
		.total_encoded_byte = 0,
		.prev_encoded_byte = 0,
		.compress_ratio = 0,
		.base_len = 0,
		.dvp_log_timer_idx = NULL,
		.fps = 0,
		.kbps = 0,
	},
};

static UINT8_T g_write_buff[DVP_I2C_MSG_MAX_LEN] = {0};

static OPERATE_RET __sensor_detect_and_init(VOID_T *args);

static void *__dvp_data_buf_malloc(UINT32_T size)
{
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM==1)
	return tal_psram_malloc(size);
#else
	return tal_malloc(size);
#endif
}

static void __dvp_data_buf_free(void *ptr)
{
#if defined(ENABLE_EXT_RAM) && (ENABLE_EXT_RAM==1)
	return tal_psram_free(ptr);
#else
	return tal_free(ptr);
#endif
}

static UINT8_T __get_base_frame_pixel_size(TUYA_FRAME_FMT_E fmt)
{
	switch (fmt)
	{
	case TUYA_FRAME_FMT_YUV422:
    case TUYA_FRAME_FMT_RGB565:
		return TWO_BYTE;
    case TUYA_FRAME_FMT_RGB888:
		return THREE_BYTE;
	default:
		return 0;
	}
}

static OPERATE_RET __dvp_node_init(DVP_WORK_FLOW_T work_flow, UINT32_T buf_len, DVP_MANAGE_T *dvp_manage)
{
	VOID **buf_ptr = NULL;
    DVP_NODE_MANAGE_T **free_node_head = NULL;
	DVP_NODE_MANAGE_T *dvp_node_pool = NULL;
	UINT8_T node_cnt = 0;

    if (work_flow == BASE_FRAME_FLOW)
    {
		buf_ptr = &(dvp_manage->base_buf_ptr);
        free_node_head = &(dvp_manage->free_base_node_list);
		dvp_node_pool = dvp_manage->base_node_pool;
		node_cnt = DVP_NODE_POOL_SIZE;
    }
    else
    {
		buf_ptr = &(dvp_manage->encoded_buf_ptr);
        free_node_head = &(dvp_manage->free_encoded_node_list);
		dvp_node_pool = dvp_manage->encoded_node_pool;
		node_cnt = DVP_ENCODED_NODE_POOL_SIZE;
    }

	(*buf_ptr) = __dvp_data_buf_malloc(buf_len * node_cnt);
	if (*buf_ptr == NULL)
	{
		PR_ERR("%s malloc buf for flow-%d failed\r\n", __func__, work_flow);
		return OPRT_MALLOC_FAILED;
	}

    memset(dvp_node_pool, 0, sizeof(DVP_NODE_MANAGE_T) * node_cnt);

    for (UINT16_T idx = 0; idx < node_cnt; ++idx)
    {
		void *data_buf_ptr = NULL;

		if (idx < node_cnt - 1)
        	dvp_node_pool[idx].next = &dvp_node_pool[idx + 1]; // 组成free链路
		else
			dvp_node_pool[idx].next = NULL;

        memset(&(dvp_node_pool[idx].dvp_frame_manage), 0, sizeof(TUYA_DVP_FRAME_MANAGE_T));
		dvp_node_pool[idx].dvp_frame_manage.data =  (UINT8_T *)(*buf_ptr) + buf_len * idx;
    }
    (*free_node_head) = dvp_node_pool;

	return OPRT_OK;
}

static OPERATE_RET __dvp_node_deinit(DVP_WORK_FLOW_T work_flow, DVP_MANAGE_T *dvp_manage)
{
	VOID **buf_ptr = NULL;
    DVP_NODE_MANAGE_T **free_node_head = NULL;
	DVP_NODE_MANAGE_T *dvp_node_pool = NULL;
	UINT8_T node_cnt = 0;

    if (work_flow == BASE_FRAME_FLOW)
    {
		buf_ptr = &(dvp_manage->base_buf_ptr);
        free_node_head = &(dvp_manage->free_base_node_list);
		dvp_node_pool = dvp_manage->base_node_pool;
		node_cnt = DVP_NODE_POOL_SIZE;
    }
    else
    {
		buf_ptr = &(dvp_manage->encoded_buf_ptr);
        free_node_head = &(dvp_manage->free_encoded_node_list);
		dvp_node_pool = dvp_manage->encoded_node_pool;
		node_cnt = DVP_ENCODED_NODE_POOL_SIZE;
    }

	if (*buf_ptr != NULL)
	{
		__dvp_data_buf_free(*buf_ptr);
		(*buf_ptr) = NULL;
	}

    (*free_node_head) = NULL;

	memset(dvp_node_pool, 0, sizeof(DVP_NODE_MANAGE_T) * node_cnt);

	return OPRT_OK;
}

static OPERATE_RET __dvp_work_flow_get(TUYA_FRAME_FMT_E fmt, DVP_WORK_FLOW_T *work_flow)
{
	switch (fmt)
	{
	case TUYA_FRAME_FMT_YUV422:
	case TUYA_FRAME_FMT_YUV420:
    case TUYA_FRAME_FMT_RGB565:
    case TUYA_FRAME_FMT_RGB888:
		/* code */
		(*work_flow) = BASE_FRAME_FLOW;
		break;
    case TUYA_FRAME_FMT_JPEG:
    case TUYA_FRAME_FMT_H264:
		(*work_flow) = ENCODED_FRAME_FLOW;
		break;
	default:
		return OPRT_INVALID_PARM;
	}
	return OPRT_OK;
}

static TUYA_DVP_FRAME_MANAGE_T *__dvp_frame_assign_handler(TUYA_FRAME_FMT_E fmt, VOID_T *args)
{
	if (!args)
		return NULL;

	DVP_MANAGE_T *dvp_manage = (DVP_MANAGE_T *)args;
	DVP_WORK_FLOW_T work_flow;
	OPERATE_RET ret = __dvp_work_flow_get(fmt, &work_flow);
	if (ret)
		return NULL;

    DVP_NODE_MANAGE_T *tmp_dvp_node = NULL;
    DVP_NODE_MANAGE_T **free_node_head = NULL;

	TKL_ENTER_CRITICAL();

    if (work_flow == BASE_FRAME_FLOW)
    {
        tmp_dvp_node = dvp_manage->free_base_node_list;
        free_node_head = &(dvp_manage->free_base_node_list);
    }
    else
    {
        tmp_dvp_node = dvp_manage->free_encoded_node_list;
        free_node_head = &(dvp_manage->free_encoded_node_list);
    }

    if (tmp_dvp_node != NULL)
    {
        (*free_node_head) = tmp_dvp_node->next;
        tmp_dvp_node->next = NULL;
    }

    TKL_EXIT_CRITICAL();

    return ((tmp_dvp_node == NULL) ? NULL : &(tmp_dvp_node->dvp_frame_manage));
}

static void __dvp_frame_manage_unassign(TUYA_DVP_FRAME_MANAGE_T *dvp_frame, DVP_MANAGE_T *dvp_manage)
{
    if (!dvp_frame)
        return;

	DVP_WORK_FLOW_T work_flow;
	OPERATE_RET ret = __dvp_work_flow_get(dvp_frame->frame_fmt, &work_flow);
	if (ret)
		return;

    DVP_NODE_MANAGE_T *dvp_node = tuya_list_entry(dvp_frame, DVP_NODE_MANAGE_T, dvp_frame_manage);

    TKL_ENTER_CRITICAL();

    DVP_NODE_MANAGE_T **free_head = (work_flow == BASE_FRAME_FLOW)
                                    ? &dvp_manage->free_base_node_list
                                    : &dvp_manage->free_encoded_node_list;

    dvp_node->next = *free_head;
    (*free_head) = dvp_node;

    dvp_frame->frame_id = 0;
	dvp_frame->is_frame_complete = false;
	dvp_frame->data_len = 0;
	dvp_frame->width = 0;
	dvp_frame->height = 0;
	dvp_frame->total_frame_len = 0;

    TKL_EXIT_CRITICAL();
}

static void __base_flow_task(void *args)
{
	if (!args)
		return;

	TAL_DVP_MSG_T msg = {0};
	DVP_MANAGE_T *dvp_manage = (DVP_MANAGE_T *)args;
	dvp_manage->base_flow_running = true;

	OPERATE_RET ret = tal_queue_create_init(&(dvp_manage->base_frame_queue), sizeof(TAL_DVP_MSG_T), DVP_NODE_POOL_SIZE + 1);

	while (dvp_manage->base_flow_running)
	{
		ret = tal_queue_fetch(dvp_manage->base_frame_queue, &msg, SEM_WAIT_FOREVER);
        if(ret == OPRT_OK)
		{
            switch(msg.event)
			{
                case DVP_FRAME_REQUEST:
				{
					TUYA_DVP_FRAME_MANAGE_T *dvp_frame = (TUYA_DVP_FRAME_MANAGE_T *)msg.param;
					if (dvp_manage->dvp_frame_handle)
						dvp_manage->dvp_frame_handle(dvp_frame, dvp_manage->frame_user_data);

					__dvp_frame_manage_unassign(dvp_frame, dvp_manage);
					break;
				}
                case DVP_FRAME_EXIT:
				{
                    do {
                        ret = tal_queue_fetch(dvp_manage->base_frame_queue, &msg, 0);//no wait
                        if(msg.event == DVP_FRAME_REQUEST)
						{
                            TUYA_DVP_FRAME_MANAGE_T *dvp_frame = (TUYA_DVP_FRAME_MANAGE_T *)msg.param;
							__dvp_frame_manage_unassign(dvp_frame, dvp_manage);
                        }
                    } while(ret == 0);
					dvp_manage->base_flow_running = false;
                    break;
				}
                default:
                    break;
            }
        }
	}

	tal_queue_free(dvp_manage->base_frame_queue);
	dvp_manage->base_frame_queue = NULL;
	__dvp_node_deinit(BASE_FRAME_FLOW, dvp_manage);

	PR_NOTICE("Base flow task ready to release.....\r\n");
	if (dvp_manage->base_flow_closed_sem)
		tal_semaphore_post(dvp_manage->base_flow_closed_sem);

	tkl_thread_release(dvp_manage->base_flow_thread);
	dvp_manage->base_flow_thread = NULL;
}

static void __encoded_flow_task(void *args)
{
	if (!args)
		return;

	TAL_DVP_MSG_T msg = {0};
	DVP_MANAGE_T *dvp_manage = (DVP_MANAGE_T *)args;
	dvp_manage->encoded_flow_running = true;

	OPERATE_RET ret = tal_queue_create_init(&(dvp_manage->encoded_frame_queue), sizeof(TAL_DVP_MSG_T), DVP_ENCODED_NODE_POOL_SIZE + 1);

	while (dvp_manage->encoded_flow_running)
	{
		ret = tal_queue_fetch(dvp_manage->encoded_frame_queue, &msg, SEM_WAIT_FOREVER);
        if(ret == OPRT_OK)
		{
            switch(msg.event)
			{
                case DVP_FRAME_REQUEST:
				{
					TUYA_DVP_FRAME_MANAGE_T *dvp_frame = (TUYA_DVP_FRAME_MANAGE_T *)msg.param;
					if (dvp_manage->dvp_frame_handle)
						dvp_manage->dvp_frame_handle(dvp_frame, dvp_manage->frame_user_data);

					__dvp_frame_manage_unassign(dvp_frame, dvp_manage);
					break;
				}
                case DVP_FRAME_EXIT:
				{
                    do {
                        ret = tal_queue_fetch(dvp_manage->encoded_frame_queue, &msg, 0);//no wait
                        if(msg.event == DVP_FRAME_REQUEST)
						{
                            TUYA_DVP_FRAME_MANAGE_T *dvp_frame = (TUYA_DVP_FRAME_MANAGE_T *)msg.param;
							__dvp_frame_manage_unassign(dvp_frame, dvp_manage);
                        }
                    } while(ret == 0);
					dvp_manage->encoded_flow_running = false;
                    break;
				}
                default:
                    break;
            }
        }
	}

	tal_queue_free(dvp_manage->encoded_frame_queue);
	dvp_manage->encoded_frame_queue = NULL;
	__dvp_node_deinit(ENCODED_FRAME_FLOW, dvp_manage);

	PR_NOTICE("Encoded flow task ready to release.....\r\n");
	if (dvp_manage->encoded_flow_closed_sem)
		tal_semaphore_post(dvp_manage->encoded_flow_closed_sem);

	tkl_thread_release(dvp_manage->encoded_flow_thread);
	dvp_manage->encoded_flow_thread = NULL;
}

static OPERATE_RET __dvp_work_flow_init(DVP_MANAGE_T *dvp_manage)
{
	if (dvp_manage == NULL || dvp_manage->dvp_device == NULL)
		return OPRT_INVALID_PARM;

	OPERATE_RET ret = 0;

	TUYA_DVP_DEVICE_T *dvp_device = dvp_manage->dvp_device;
	TUYA_CAMERA_OUTPUT_MODE output_mode = dvp_device->usr_cfg.dvp_cfg.output_mode;
	if (output_mode == TUYA_CAMERA_OUTPUT_YUV422)
	{
		dvp_manage->base_flow_flag = true;
		dvp_manage->encoded_flow_flag = false;
	}
	else if (output_mode == TUYA_CAMERA_OUTPUT_H264 ||
		output_mode == TUYA_CAMERA_OUTPUT_JPEG)
	{
		dvp_manage->base_flow_flag = false;
		dvp_manage->encoded_flow_flag = true;
	}
	else if (output_mode == TUYA_CAMERA_OUTPUT_H264_YUV422_BOTH ||
		output_mode == TUYA_CAMERA_OUTPUT_JPEG_YUV422_BOTH)
	{
		dvp_manage->base_flow_flag = true;
		dvp_manage->encoded_flow_flag = true;
	}

	UINT32_T base_len = 0;
	UINT32_T encoded_len = 0;
	UINT16_T width = dvp_device->usr_cfg.dvp_cfg.width;
	UINT16_T height = dvp_device->usr_cfg.dvp_cfg.height;
#if ENABLE_DVP_SPLIT_FRAME
	base_len = DVP_SLICE_SIZE;
	encoded_len = DVP_SLICE_SIZE;
#else
	base_len = width * height * DVP_PER_MAX_PIXEL_SIZE;
	encoded_len = (base_len * DVP_MIN_COMPRESSION_PERCENT + 99) / 100;
#endif

	if (dvp_manage->base_flow_flag)
	{
		ret |= __dvp_node_init(BASE_FRAME_FLOW, base_len, dvp_manage);
		ret |= tkl_thread_create(&(dvp_manage->base_flow_thread), "dvp_base", 16384, THREAD_PRIO_1, __base_flow_task, (VOID_T *)dvp_manage);
	}

	if (dvp_manage->encoded_flow_flag)
	{
		ret = __dvp_node_init(ENCODED_FRAME_FLOW, encoded_len, dvp_manage);
		ret |= tkl_thread_create(&(dvp_manage->encoded_flow_thread), "dvp_encode", 16384, THREAD_PRIO_1, __encoded_flow_task, (VOID_T *)dvp_manage);
	}

	return ret;
}

static OPERATE_RET __dvp_work_flow_deinit(DVP_MANAGE_T *dvp_manage)
{
	OPERATE_RET ret = 0;

	if (dvp_manage->base_flow_flag)
	{
		tal_semaphore_create_init(&dvp_manage->base_flow_closed_sem, 0, 1);
		if (dvp_manage->base_flow_running)
		{
			TAL_DVP_MSG_T msg = {DVP_FRAME_EXIT, 0};
			tal_queue_post(dvp_manage->base_frame_queue, &msg , SEM_WAIT_FOREVER);
			tal_semaphore_wait_forever(dvp_manage->base_flow_closed_sem);
		}
		tal_semaphore_release(dvp_manage->base_flow_closed_sem);
		dvp_manage->base_flow_closed_sem = NULL;
		dvp_manage->base_flow_thread = NULL;
		dvp_manage->base_flow_flag = false;
	}

	if (dvp_manage->encoded_flow_flag)
	{
		tal_semaphore_create_init(&dvp_manage->encoded_flow_closed_sem, 0, 1);
		if (dvp_manage->encoded_flow_running)
		{
			TAL_DVP_MSG_T msg = {DVP_FRAME_EXIT, 0};
			tal_queue_post(dvp_manage->encoded_frame_queue, &msg , SEM_WAIT_FOREVER);
			tal_semaphore_wait_forever(dvp_manage->encoded_flow_closed_sem);
		}
		tal_semaphore_release(dvp_manage->encoded_flow_closed_sem);
		dvp_manage->encoded_flow_thread = NULL;
		dvp_manage->encoded_flow_closed_sem = NULL;
		dvp_manage->encoded_flow_flag = false;
	}

	return ret;
}

static OPERATE_RET tal_dvp_i2c_init(UINT8_T clk, UINT8_T sda, TUYA_I2C_NUM_E idx, DVP_MANAGE_T *dvp_manage)
{
    if (idx >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, input args is invalid!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

    PR_INFO("set dvp i2c, clk: %d sda: %d, idx: %d\r\n", clk, sda, idx);

    tkl_io_pinmux_config(clk, DVP_I2C_SCL);
    tkl_io_pinmux_config(sda, DVP_I2C_SDA);

    // tp used sw i2c1
    TUYA_IIC_BASE_CFG_T cfg = {
        .role = TUYA_IIC_MODE_MASTER,
        .speed = TUYA_IIC_BUS_SPEED_100K,
        .addr_width = TUYA_IIC_ADDRESS_7BIT,
    };

    OPERATE_RET ret = tkl_i2c_init(idx, &cfg);
	dvp_manage->dvp_i2c_idx = (ret == OPRT_OK) ? idx : TUYA_I2C_NUM_MAX;

    return ret;
}

static OPERATE_RET tal_dvp_i2c_deinit(TUYA_I2C_NUM_E *idx)
{
    if (idx  == NULL || *idx >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, TUYA_I2C_NUM is not init!\r\n", __func__);
        return OPRT_RESOURCE_NOT_READY;
    }

    OPERATE_RET ret = OPRT_OK;
    ret = tkl_i2c_deinit(*idx);
    *idx = TUYA_I2C_NUM_MAX;

    return ret;
}

OPERATE_RET tal_dvp_i2c_read(UINT8_T addr, UINT16_T reg, UINT8_T *buf, UINT16_T buf_len, UINT8_T is_16_reg)
{
    if (!buf || !buf_len)
    {
        PR_ERR("%s, input args is invalid!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

	UINT8_T port = g_dvp_manage.dvp_i2c_idx;
	if (port >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, TUYA_I2C_NUM is not init!\r\n", __func__);
        return OPRT_RESOURCE_NOT_READY;
    }

    if (buf_len > DVP_I2C_MSG_MAX_LEN)
    {
        PR_ERR("%s, buf_len %d is overflow the limit(%d)\r\n", __func__, buf_len, DVP_I2C_MSG_MAX_LEN);
        return OPRT_EXCEED_UPPER_LIMIT;
    }

    UINT8_T write_data[2] = {0};
    UINT8_T write_data_len = 0;
    if (is_16_reg)
    {
        write_data[0] = (UINT8_T)((reg >> 8) & 0xFF);
        write_data[1] = (UINT8_T)(reg & 0xFF);
        write_data_len = 2;
    }
    else
    {
        write_data[0] = (UINT8_T)(reg & 0xFF);
        write_data_len = 1;
    }

    tkl_i2c_master_send(port, addr, write_data, write_data_len, 1);

    tkl_i2c_master_receive(port, addr, buf, buf_len, 0);

    return OPRT_OK;
}

OPERATE_RET tal_dvp_i2c_write(UINT8_T addr, UINT16_T reg, UINT8_T *buf, UINT16_T buf_len, UINT8_T is_16_reg)
{
    if (!buf || !buf_len)
    {
        PR_ERR("%s, input args is invalid!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

	UINT8_T port = g_dvp_manage.dvp_i2c_idx;
	if (port >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, TUYA_I2C_NUM is not init!\r\n", __func__);
        return OPRT_RESOURCE_NOT_READY;
    }

    UINT8_T reg_len = is_16_reg ? 2 : 1;
    UINT8_T write_data_len = reg_len + buf_len;

    if (write_data_len > DVP_I2C_MSG_MAX_LEN)
    {
        PR_ERR("%s, write_data_len(%d+%d) is overflow the limit(%d)\r\n", __func__, buf_len, reg_len, DVP_I2C_MSG_MAX_LEN);
        return OPRT_EXCEED_UPPER_LIMIT;
    }

    memcpy(g_write_buff + reg_len, buf, buf_len);

    if (is_16_reg)
    {
        g_write_buff[0] = (UINT8_T)((reg >> 8) & 0xFF);
        g_write_buff[1] = (UINT8_T)(reg & 0xFF);
    }
    else
    {
        g_write_buff[0] = (UINT8_T)(reg & 0xFF);
    }

    tkl_i2c_master_send(port, addr, g_write_buff, write_data_len, 0);

    return OPRT_OK;
}

static void __dvp_statistics_update(TUYA_DVP_FRAME_MANAGE_T *dvp_frame, DVP_WORK_FLOW_T work_flow, DVP_MANAGE_T *dvp_manage)
{
	if (dvp_frame->is_frame_complete == false)
		return;

	DVP_LOG_T *dvp_log = &(dvp_manage->dvp_log);
	if (work_flow == BASE_FRAME_FLOW)
	{
		dvp_log->base_frame_id = dvp_frame->frame_id;
		dvp_log->total_base_byte += dvp_frame->total_frame_len;
	}
	else
	{
		dvp_log->encoded_frame_id = dvp_frame->frame_id;
		dvp_log->total_encoded_byte += dvp_frame->total_frame_len;
		if (dvp_frame->total_frame_len > 0)
			dvp_log->compress_ratio = (FLOAT_T)dvp_log->base_len / (FLOAT_T)dvp_frame->total_frame_len;
	}
}

OPERATE_RET __dvp_frame_post_handler(TUYA_DVP_FRAME_MANAGE_T *dvp_frame, VOID_T *args)
{
	if (!dvp_frame || !args)
		return OPRT_INVALID_PARM;

	DVP_MANAGE_T *dvp_manage = (DVP_MANAGE_T *)args;
	DVP_WORK_FLOW_T work_flow;
	OPERATE_RET ret = __dvp_work_flow_get(dvp_frame->frame_fmt, &work_flow);
	if (ret)
		return OPRT_INVALID_PARM;

	QUEUE_HANDLE workq = (work_flow == BASE_FRAME_FLOW) ? dvp_manage->base_frame_queue : dvp_manage->encoded_frame_queue;
	UINT8_T status = (work_flow == BASE_FRAME_FLOW) ? dvp_manage->base_flow_running : dvp_manage->encoded_flow_running;

	if (!workq || !status)
		__dvp_frame_manage_unassign(dvp_frame, dvp_manage);

	__dvp_statistics_update(dvp_frame, work_flow, dvp_manage);

	TAL_DVP_MSG_T msg = {DVP_FRAME_REQUEST, dvp_frame};
	ret = tal_queue_post(workq, &msg, 0);
	if (ret != OPRT_OK)
		__dvp_frame_manage_unassign(dvp_frame, dvp_manage);

	return OPRT_OK;
}

static OPERATE_RET __dvp_driver_init(DVP_MANAGE_T *dvp_manage)
{
	if (dvp_manage == NULL || dvp_manage->dvp_device == NULL)
		return OPRT_INVALID_PARM;

	OPERATE_RET ret = 0;
	TUYA_DVP_DEVICE_T *dvp_device = dvp_manage->dvp_device;
	dvp_manage->dvp_frame_handle = dvp_device->usr_cfg.dvp_frame_handle;
	dvp_manage->frame_user_data  = dvp_device->usr_cfg.frame_user_data;

	TUYA_DVP_TKL_CFG_T *tkl_cfg = &(dvp_device->usr_cfg.dvp_cfg.inter_cfg);
    tkl_cfg->setup_cb = __sensor_detect_and_init;
	tkl_cfg->assign_cb = __dvp_frame_assign_handler;
	tkl_cfg->post_cb = __dvp_frame_post_handler;
	tkl_cfg->cb_param = dvp_manage;
	tkl_cfg->sensor_clk = dvp_device->sensor_cfg.clk;

	// video gpio init
	ret = tkl_dvp_init(&(dvp_device->usr_cfg.dvp_cfg));

	return ret;
}

static void __dvp_release(DVP_MANAGE_T *dvp_manage)
{
	__dvp_work_flow_deinit(dvp_manage);

	dvp_manage->dvp_frame_handle = NULL;
	dvp_manage->frame_user_data  = NULL;

	if (dvp_manage->dvp_device)
	{
		tal_free(dvp_manage->dvp_device);
		dvp_manage->dvp_device = NULL;
	}
}

static OPERATE_RET __sensor_detect_and_init(VOID_T *args)
{
	if (!args)
        return OPRT_INVALID_PARM;

	DVP_MANAGE_T *dvp_manage = (DVP_MANAGE_T *)args;
    if (dvp_manage->dvp_device == NULL)
        return OPRT_INVALID_PARM;

    TUYA_DVP_DEVICE_T *dvp_device = dvp_manage->dvp_device;
	if (dvp_device->sensor_cfg.detect == NULL)
	{
		PR_ERR("Detect must be supported\r\n");
		return OPRT_NOT_FOUND;
	}

	OPERATE_RET ret = OPRT_OK;
	TUYA_GPIO_BASE_CFG_T cfg = {.direct = TUYA_GPIO_OUTPUT};
	TUYA_DVP_PIN_CFG_T *dvp_pin_cfg = &(dvp_device->usr_cfg.pin_cfg);

	// pwr gpio init
	if (dvp_pin_cfg->dvp_pwr_ctrl.pin < TUYA_GPIO_NUM_MAX)
	{
		cfg.level = UNACTIVE_LEVEL(dvp_pin_cfg->dvp_pwr_ctrl.active_level);
		tkl_gpio_init(dvp_pin_cfg->dvp_pwr_ctrl.pin, &cfg);
	}

	// rst gpio init
	if (dvp_pin_cfg->dvp_rst_ctrl.pin < TUYA_GPIO_NUM_MAX)
	{
		cfg.level = UNACTIVE_LEVEL(dvp_pin_cfg->dvp_rst_ctrl.active_level);
		tkl_gpio_init(dvp_pin_cfg->dvp_rst_ctrl.pin, &cfg);
		tkl_system_sleep(20);
		tkl_gpio_write(dvp_pin_cfg->dvp_rst_ctrl.pin, dvp_pin_cfg->dvp_rst_ctrl.active_level);
	}

	// dvp i2c init
	ret = tal_dvp_i2c_init(dvp_pin_cfg->dvp_i2c_clk.pin, dvp_pin_cfg->dvp_i2c_sda.pin, dvp_pin_cfg->dvp_i2c_idx, dvp_manage);
	if (ret)
	{
		PR_ERR("Dvp I2C(%d) init failed\r\n", dvp_pin_cfg->dvp_i2c_idx);
		return ret;
	}

	if (dvp_pin_cfg->dvp_pwr_ctrl.pin < TUYA_GPIO_NUM_MAX)
	{
		// pwr gpio should pull low after mclk enabled
		tkl_gpio_write(dvp_pin_cfg->dvp_pwr_ctrl.pin, dvp_pin_cfg->dvp_pwr_ctrl.active_level);
	}

	if (dvp_pin_cfg->dvp_rst_ctrl.pin < TUYA_GPIO_NUM_MAX)
	{
		// rst gpio should pull high after pwr pull down
		tkl_gpio_write(dvp_pin_cfg->dvp_rst_ctrl.pin, UNACTIVE_LEVEL(dvp_pin_cfg->dvp_rst_ctrl.active_level));
	}

    tal_system_sleep(20);
	UINT8_T retry_cnt = 0;
	UINT8_T detect_flag = FALSE;
	while (retry_cnt++ < MAX_DETECT_RETRY_TIMES)
	{
		if (dvp_device->sensor_cfg.detect() == OPRT_OK)
		{
			detect_flag = TRUE;
			break;
		}
	}

	if (!detect_flag)
	{
		PR_ERR("Detect dvp failed, Please check the connection between the host and the DVP\r\n");
		return OPRT_NOT_FOUND;
	}

	if (dvp_device->sensor_cfg.init)
		dvp_device->sensor_cfg.init(&(dvp_device->usr_cfg.dvp_cfg));

	ret = __dvp_work_flow_init(dvp_manage);

	return ret;
}

static void __dvp_log_timer_callback(TIMER_ID timer_id, VOID_T *arg)
{
	if (!arg)
		return;

	DVP_MANAGE_T *dvp_manage = (DVP_MANAGE_T *)arg;
	if (dvp_manage->dvp_status != DVP_STATUS_TURN_ON)
		return;

	DVP_LOG_T *dvp_log = &(dvp_manage->dvp_log);
	if (dvp_manage->encoded_flow_flag)
	{
		dvp_log->fps = ((UINT32_T)(dvp_log->encoded_frame_id - dvp_log->prev_encoded_frame_id)) / DVP_LOG_TIMER_INTERVAL;
		dvp_log->kbps = (dvp_log->total_encoded_byte - dvp_log->prev_encoded_byte) / DVP_LOG_TIMER_INTERVAL * 8 / 1000;
		dvp_log->prev_encoded_frame_id = dvp_log->encoded_frame_id;
		dvp_log->prev_encoded_byte = dvp_log->total_encoded_byte;
		PR_DEBUG("[DVP_LOG] FPS: %d; BITRATE: %dKbps; COMPRESS: %d\n", dvp_log->fps, dvp_log->kbps, dvp_log->compress_ratio);
	}
	else
	{
		dvp_log->fps = ((UINT32_T)(dvp_log->base_frame_id - dvp_log->prev_base_frame_id)) / DVP_LOG_TIMER_INTERVAL;
		dvp_log->kbps = (dvp_log->total_base_byte - dvp_log->prev_base_byte) / DVP_LOG_TIMER_INTERVAL * 8 / 1000;
		dvp_log->prev_base_frame_id = dvp_log->base_frame_id;
		dvp_log->prev_base_byte = dvp_log->total_base_byte;
		PR_DEBUG("[DVP_LOG] FPS: %d; BITRATE: %dKbps;\n", dvp_log->fps, dvp_log->kbps);
	}
}

static TUYA_DVP_DEVICE_T *__tal_dvp_init(TUYA_DVP_SENSOR_CFG_T *sensor_cfg, TUYA_DVP_USR_CFG_T *usr_cfg, DVP_MANAGE_T *dvp_manage)
{
	OPERATE_RET ret = 0;
	TUYA_GPIO_CTRL_T *pwr_ctrl = NULL;
	TUYA_GPIO_CTRL_T *rst_ctrl = NULL;

	TUYA_DVP_DEVICE_T *dvp_device = tal_malloc(sizeof(TUYA_DVP_DEVICE_T));
	if (dvp_device == NULL)
		return NULL;

	memcpy(&(dvp_device->sensor_cfg), sensor_cfg, sizeof(TUYA_DVP_SENSOR_CFG_T));
	memcpy(&(dvp_device->usr_cfg), usr_cfg, sizeof(TUYA_DVP_USR_CFG_T));
	dvp_manage->dvp_device = dvp_device;

	ret = __dvp_driver_init(dvp_manage);
    if (ret)
        goto failed;

	return dvp_device;

failed:
	pwr_ctrl = &(dvp_manage->dvp_device->usr_cfg.pin_cfg.dvp_pwr_ctrl);
	if (pwr_ctrl->pin < TUYA_GPIO_NUM_MAX)
	{
		tkl_gpio_write(pwr_ctrl->pin, UNACTIVE_LEVEL(pwr_ctrl->active_level));
		tkl_gpio_deinit(pwr_ctrl->pin);
		pwr_ctrl->pin = TUYA_GPIO_NUM_MAX;
	}

	rst_ctrl = &(dvp_manage->dvp_device->usr_cfg.pin_cfg.dvp_rst_ctrl);
	if (rst_ctrl->pin < TUYA_GPIO_NUM_MAX)
	{
		tkl_gpio_deinit(rst_ctrl->pin);
		rst_ctrl->pin = TUYA_GPIO_NUM_MAX;
	}

	tal_dvp_i2c_deinit(&dvp_manage->dvp_i2c_idx);

	tkl_dvp_deinit();

	__dvp_release(dvp_manage);

	return NULL;
}

TUYA_DVP_DEVICE_T *tal_dvp_init(TUYA_DVP_SENSOR_CFG_T *sensor_cfg, TUYA_DVP_USR_CFG_T *usr_cfg)
{
	if (!sensor_cfg || !usr_cfg)
		return NULL;

	if (g_dvp_manage.dvp_status != DVP_STATUS_TURN_OFF)
	{
		PR_ERR("%s, dvp status(%d) is not closed, please turn off first\r\n", __func__, g_dvp_manage.dvp_status);
		return NULL;
	}
	g_dvp_manage.dvp_status = DVP_STATUS_TURNING_ON;

	TUYA_DVP_DEVICE_T *dvp_device = __tal_dvp_init(sensor_cfg, usr_cfg, &g_dvp_manage);
	g_dvp_manage.dvp_status = (dvp_device != NULL) ? DVP_STATUS_TURN_ON : DVP_STATUS_TURN_OFF;

	if (g_dvp_manage.dvp_status == DVP_STATUS_TURN_ON)
	{
		DVP_LOG_T *dvp_log = &(g_dvp_manage.dvp_log);
		dvp_log->base_len = usr_cfg->dvp_cfg.width * usr_cfg->dvp_cfg.height * __get_base_frame_pixel_size(sensor_cfg->fmt);

		if (!dvp_log->dvp_log_timer_idx)
			tal_sw_timer_create(__dvp_log_timer_callback, &g_dvp_manage, &dvp_log->dvp_log_timer_idx);

		if (dvp_log->dvp_log_timer_idx)
			tal_sw_timer_start(dvp_log->dvp_log_timer_idx, DVP_LOG_TIMER_INTERVAL * 1000 , TAL_TIMER_CYCLE);
	}

	return dvp_device;
}

OPERATE_RET tal_dvp_deinit(TUYA_DVP_DEVICE_T *dvp_device)
{
	if (!dvp_device || g_dvp_manage.dvp_device != dvp_device)
		return OPRT_INVALID_PARM;

	if (g_dvp_manage.dvp_status != DVP_STATUS_TURN_ON)
	{
		PR_ERR("%s, dvp status(%d) is not opened, please turn on first\r\n", __func__, g_dvp_manage.dvp_status);
		return OPRT_COM_ERROR;
	}
	g_dvp_manage.dvp_status = DVP_STATUS_TURNING_OFF;

	tkl_dvp_deinit();

	TUYA_DVP_PIN_CFG_T *dvp_pin_cfg = &(dvp_device->usr_cfg.pin_cfg);

	// pwr gpio deinit
	if (dvp_pin_cfg->dvp_pwr_ctrl.pin < TUYA_GPIO_NUM_MAX)
	{
		tkl_gpio_write(dvp_pin_cfg->dvp_pwr_ctrl.pin, UNACTIVE_LEVEL(dvp_pin_cfg->dvp_pwr_ctrl.active_level));
		tkl_gpio_deinit(dvp_pin_cfg->dvp_pwr_ctrl.pin);
		dvp_pin_cfg->dvp_pwr_ctrl.pin = TUYA_GPIO_NUM_MAX;
	}

	if (dvp_pin_cfg->dvp_rst_ctrl.pin < TUYA_GPIO_NUM_MAX)
	{
		tkl_gpio_deinit(dvp_pin_cfg->dvp_rst_ctrl.pin);
		dvp_pin_cfg->dvp_rst_ctrl.pin = TUYA_GPIO_NUM_MAX;
	}

	tal_dvp_i2c_deinit(&g_dvp_manage.dvp_i2c_idx);

	DVP_LOG_T *dvp_log = &(g_dvp_manage.dvp_log);
	if (dvp_log->dvp_log_timer_idx)
	{
		tal_sw_timer_stop(dvp_log->dvp_log_timer_idx);
		tal_sw_timer_delete(dvp_log->dvp_log_timer_idx);
		dvp_log->dvp_log_timer_idx = NULL;
	}

	TUYA_DVP_TKL_CFG_T *tkl_cfg = &(dvp_device->usr_cfg.dvp_cfg.inter_cfg);
	memset(tkl_cfg, 0, sizeof(TUYA_DVP_TKL_CFG_T));

	__dvp_release(&g_dvp_manage);

	g_dvp_manage.dvp_status = DVP_STATUS_TURN_OFF;

	return OPRT_OK;
}

OPERATE_RET tal_dvp_start(TUYA_DVP_DEVICE_T *dvp_device)
{
	if (!dvp_device || g_dvp_manage.dvp_device != dvp_device)
	return OPRT_INVALID_PARM;

	if (g_dvp_manage.dvp_status != DVP_STATUS_TURN_ON)
	{
		PR_ERR("%s, dvp status(%d) is not opened, please turn on first\r\n", __func__, g_dvp_manage.dvp_status);
		return OPRT_COM_ERROR;
	}

	tkl_dvp_start();

	return OPRT_OK;
}

OPERATE_RET tal_dvp_stop(TUYA_DVP_DEVICE_T *dvp_device)
{
	if (!dvp_device || g_dvp_manage.dvp_device != dvp_device)
		return OPRT_INVALID_PARM;

	if (g_dvp_manage.dvp_status != DVP_STATUS_TURN_ON)
	{
		PR_ERR("%s, dvp status(%d) is not opened, please turn on first\r\n", __func__, g_dvp_manage.dvp_status);
		return OPRT_COM_ERROR;
	}

	tkl_dvp_stop();

	return OPRT_OK;
}