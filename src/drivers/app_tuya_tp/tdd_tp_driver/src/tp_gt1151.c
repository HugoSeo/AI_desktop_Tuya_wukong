#include "tdd_tp.h"

#ifdef TUYA_APP_DRIVERS_TP
#include "tal_tp_service.h"
#include "tal_tp_i2c.h"

/*1- TP PRIVATE CONFIG*/
#define GT1151_WRITE_ADDRESS            (0x28)
#define GT1151_READ_ADDRESS             (0x29)
#define GT1151_POINT_INFO_SIZE          (8)
#define GT1151_CHECK_SUM_POS            (236)

#define GT1151_POINT_INFO_TOTAL_SIZE    (GT1151_MAX_TOUCH_NUM * GT1151_POINT_INFO_SIZE)
#define GT1151_CFG_TABLE_SIZE           sizeof(tp_gt1151_cfg_table)
static uint8_t tp_gt1151_cfg_table[] =
{
    0x41, 0x40, 0x01, 0xe0, 0x01, 0x0a, 0x3d, 0x00, 0x00, 0x00, 
    0x02, 0x0f, 0x50, 0x32, 0x33, 0x01, 0x00, 0x10, 0x08, 0x50, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x28, 0x00, 
    0x64, 0x08, 0x32, 0x3c, 0x28, 0x78, 0x00, 0x00, 0x88, 0xa2, 
    0xb4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x84, 0x24, 0x0c, 0x36, 0x38, 0xc5, 
    0x04, 0x00, 0x3d, 0x72, 0x24, 0xf9, 0x1e, 0x18, 0x23, 0xba, 
    0x3a, 0xa2, 0x42, 0x80, 0x4a, 0x80, 0x53, 0x00, 0x00, 0x00, 
    0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x50, 0x32, 0x66, 
    0x60, 0x27, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x50, 0x78, 0x00, 0x54, 0x85, 0x0a, 
    0x00, 0x04, 0x9c, 0x56, 0x91, 0x5d, 0x88, 0x64, 0x80, 0x6a, 
    0x78, 0x71, 0x73, 0x03, 0x14, 0x03, 0x03, 0x1e, 0x5a, 0xb0, 
    0x64, 0xaf, 0x80, 0x00, 0x00, 0x00, 0x01, 0x32, 0xc8, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x13, 0x14, 0x15, 0x16, 
    0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1d, 0x1f, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x06, 0x08, 0x0c, 
    0x12, 0x13, 0x14, 0x15, 0x17, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 
    0xc4, 0x09, 0x23, 0x23, 0x50, 0x5d, 0x54, 0x50, 0x3c, 0x14, 
    0x32, 0xff, 0xff, 0x0e, 0x41, 0x00, 0x5f, 0x02, 0x40, 0x00, 
    0xaa, 0x00, 0x22, 0x22, 0x00, 0x40, 0x08, 0x05, 0x00
};

/*2- TP COMMON CONFIG*/
#define GT1151_MAX_TOUCH_NUM     (5)

/*3- TP REG ADDR*/
#define GT1151_CONFIG_REG       (0x8050)
#define GT1151_PRODUCT_ID       (0x8140)
#define GT1151_STATUS_REG       (0x814E)
#define GT1151_POINT1_REG       (0x814F)


#define SENSOR_I2C_READ(reg, buff, len) \
	do {\
		tal_tp_i2c_read((GT1151_WRITE_ADDRESS >> 1), reg, buff, len, REG_ADDR_16BITS);\
	}while (0)

#define SENSOR_I2C_WRITE(reg, buff, len) \
	do {\
		tal_tp_i2c_write((GT1151_WRITE_ADDRESS >> 1), reg, buff, len, REG_ADDR_16BITS);\
	}while (0)

static OPERATE_RET tp_obtain_origin_reg(uint8_t *config, uint8_t size)
{
    memset(config, 0, size);

    SENSOR_I2C_READ(GT1151_CONFIG_REG, config, size);

    uint16_t checksum = 0;
    uint16_t tmp = 0;
    for (uint8_t i = 0; i < GT1151_CHECK_SUM_POS; i+=2)
    {
        tmp = (config[i + 1]) | (config[i] << 8); /* 大端 */
        checksum += tmp;
    }
    checksum = (~checksum) + 1; /* 得到对应补码*/

    uint16_t tmp_checksum = (config[GT1151_CHECK_SUM_POS] << 8) + config[GT1151_CHECK_SUM_POS + 1];

    if (tmp_checksum != checksum)
    {
        PR_ERR("%s, check sum 0X%02x and 0X%02x is not equal!\r\n", __func__, tmp_checksum, checksum);
        return OPRT_COM_ERROR;
    }

    return OPRT_OK;
}

static void tp_gt1151_init(struct ty_tp_device_cfg *cfg)
{
	if (!cfg || !cfg->tp_cfg)
	{
		PR_ERR("%s, input args is null!\r\n", __func__);
		return;
	}

    uint8_t ret = tp_obtain_origin_reg(tp_gt1151_cfg_table, GT1151_CFG_TABLE_SIZE);
    if (ret)
    {
		PR_ERR("%s, obtain origin reg failed!\r\n", __func__);
		return;
    }

    /* Todo: update usr_cfg into tp_gt1151_cfg_table */

    uint16_t checksum = 0;
    uint16_t tmp = 0;
    for (uint8_t i = 0; i < GT1151_CHECK_SUM_POS; i+=2)
    {
        tmp = (tp_gt1151_cfg_table[i + 1]) | (tp_gt1151_cfg_table[i] << 8); /* 大端 */
        checksum += tmp;
    }
    checksum = (~checksum) + 1; /* 得到对应补码，对端校验和校准方式： sum(data[i]) + checksum == 0 */

    tp_gt1151_cfg_table[GT1151_CHECK_SUM_POS] = (checksum >> 8) & 0xFF;
    tp_gt1151_cfg_table[GT1151_CHECK_SUM_POS + 1] = checksum & 0XFF;
    tp_gt1151_cfg_table[GT1151_CHECK_SUM_POS + 2] = 1;

    SENSOR_I2C_WRITE(GT1151_CONFIG_REG, tp_gt1151_cfg_table, GT1151_CFG_TABLE_SIZE);

    return;
}

static uint8_t read_buf[GT1151_POINT_INFO_TOTAL_SIZE];
static void tp_gt1151_read_pont_info(ty_tp_read_data_t *read_point)
{
	if (!read_point || !read_point->point_arr)
	{
		PR_ERR("%s, input args is null!\r\n", __func__);
		goto clear_status_and_exit;
	}

    uint8_t temp_status = 0;
    SENSOR_I2C_READ(GT1151_STATUS_REG, &temp_status, sizeof(uint8_t));
    if ((temp_status & 0x80) == 0)
    {
        PR_ERR("%s, temp: %d, no data!\r\n", __func__, temp_status);
        goto clear_status_and_exit;
    }

    uint8_t tp_num = (temp_status & 0x0F);
    read_point->point_cnt = tp_num;
    read_point->support_get_event = FALSE;

    SENSOR_I2C_READ(GT1151_POINT1_REG, read_buf, GT1151_POINT_INFO_SIZE * tp_num);

    uint8_t off_set= 0;
    for (uint8_t i = 0; i < tp_num; ++i)
    {
        off_set = i * GT1151_POINT_INFO_SIZE;

        read_point->point_arr[i].tp_point_id = read_buf[off_set] & 0x0F;
        read_point->point_arr[i].tp_point_x = read_buf[off_set + 1] | (read_buf[off_set + 2] << 8);
        read_point->point_arr[i].tp_point_y = read_buf[off_set + 3] | (read_buf[off_set + 4] << 8);
        read_point->point_arr[i].tp_point_event = TP_EVENT_UNKONW;
    }

clear_status_and_exit:
    temp_status = 0;
    SENSOR_I2C_WRITE(GT1151_STATUS_REG, &temp_status, sizeof(uint8_t));

    return;
}

const ty_tp_device_cfg_t tp_gt1151_device =
{
	.name = "gt1151",
    .width = 320,
    .height = 480,
    .intr_type = TUYA_GPIO_IRQ_FALL,
    .mirror_type = TP_MIRROR_NONE,
    .max_support_tp_num = GT1151_MAX_TOUCH_NUM,
	.init = tp_gt1151_init,
	.read_pont_info = tp_gt1151_read_pont_info,
};
#endif
