#include "tdd_tp.h"

#ifdef TUYA_APP_DRIVERS_TP
#include "tal_tp_service.h"
#include "tal_tp_i2c.h"

/*1- TP PRIVATE CONFIG*/
#define CST810_WRITE_ADDRESS            (0x2A)
#define CST810_READ_ADDRESS             (0x2B)
#define CST810_POINT_INFO_SIZE          (6)

/*2- TP COMMON CONFIG*/
#define CST810_MAX_TOUCH_NUM     (1)

#define CST810_POINT_INFO_TOTAL_SIZE    (CST810_MAX_TOUCH_NUM * CST810_POINT_INFO_SIZE + 3)

/*3- TP REG ADDR*/
#define CST810_ALL_DATA_REG     (0x00)
#define CST810_GESTURE_ID       (0x01)
#define CST810_FINGER_REG       (0x02)
#define CST810_XPOSH_REG        (0x03)
#define CST810_PRODUCT_ID       (0xA7)
#define CST810_IRQ_CTL          (0xFA)
#define CST810_DIS_AUTOSLEEP    (0xFE)



#define SENSOR_I2C_READ(reg, buff, len) \
	do {\
		tal_tp_i2c_read((CST810_WRITE_ADDRESS >> 1), reg, buff, len, REG_ADDR_8BITS);\
	}while (0)

#define SENSOR_I2C_WRITE(reg, buff, len) \
	do {\
		tal_tp_i2c_write((CST810_WRITE_ADDRESS >> 1), reg, buff, len, REG_ADDR_8BITS);\
	}while (0)


static void tp_cst810_init(struct ty_tp_device_cfg *cfg)
{
	if (!cfg || !cfg->tp_cfg)
	{
		PR_ERR("%s, input args is null!\r\n", __func__);
		return;
	}
    UCHAR_T product_info[4] = {0};

    SENSOR_I2C_READ(CST810_PRODUCT_ID, product_info, sizeof(product_info));
    PR_DEBUG("%s, project_id: %x,chip_id: %x,fw_ver: %02x%02x\r\n", __func__, product_info[0], product_info[1], product_info[2], product_info[3]);   
    
    // uint8_t tmp = 1;
    // SENSOR_I2C_WRITE(CST810_DIS_AUTOSLEEP, &tmp, 1);

    // tmp = 0x70;
    // SENSOR_I2C_WRITE(CST810_IRQ_CTL, &tmp, 1);
    return;
}

static uint8_t read_buf[CST810_POINT_INFO_TOTAL_SIZE];
static void tp_cst810_read_pont_info(ty_tp_read_data_t *read_point)
{
	if (!read_point || !read_point->point_arr)
	{
		PR_ERR("%s, input args is null!\r\n", __func__);
		goto clear_status_and_exit;
	}

    SENSOR_I2C_READ(CST810_ALL_DATA_REG, read_buf, CST810_POINT_INFO_TOTAL_SIZE);

    // PR_DEBUG("%s,buf:%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\r\n", __func__, read_buf[0],\
    //      read_buf[1], read_buf[2], read_buf[3], read_buf[4], read_buf[5], read_buf[6], read_buf[7], read_buf[8]);

    uint8_t tp_num = read_buf[CST810_FINGER_REG];
    
    if (tp_num == 0x00){
        PR_DEBUG("%s, no touch\r\n", __func__);
        goto clear_status_and_exit;
    }

    read_point->point_cnt = tp_num;
    read_point->support_get_event = FALSE;

    uint8_t off_set= 0;
    for (uint8_t i = 0; i < tp_num; ++i)
    {
        off_set = i * CST810_POINT_INFO_SIZE;

        read_point->point_arr[i].tp_point_id = (read_buf[off_set + 5] & 0xF0)>>4;
        read_point->point_arr[i].tp_point_x = ((read_buf[off_set + 3] & 0x0F)<<8 | (read_buf[off_set + 4]));
        read_point->point_arr[i].tp_point_y = (read_buf[off_set + 5] & 0x0F)<<8 | (read_buf[off_set + 6]);
        read_point->point_arr[i].tp_point_event = TP_EVENT_UNKONW;
        // PR_DEBUG("%s, point_id: %d, x: %d, y: %d\r\n", __func__, read_point->point_arr[i].tp_point_id, read_point->point_arr[i].tp_point_x, read_point->point_arr[i].tp_point_y);
    }

clear_status_and_exit:
    return;
}

const ty_tp_device_cfg_t tp_cst810_device =
{
	.name = "cst810",
    .width = 360,
    .height = 360,
    .intr_type = TUYA_GPIO_IRQ_FALL,
    .mirror_type = TP_MIRROR_NONE,
    .max_support_tp_num = CST810_MAX_TOUCH_NUM,
	.init = tp_cst810_init,
	.read_pont_info = tp_cst810_read_pont_info,
};
#endif
