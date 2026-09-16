#include "tdd_tp.h"

#ifdef TUYA_APP_DRIVERS_TP
#include "tal_tp_service.h"
#include "tal_tp_i2c.h"

#define CST816_ADDR                    0x15

#define CST816_GESTURE_NONE            0x00
#define CST816_GESTURE_MOVE_UP         0x01
#define CST816_GESTURE_MOVE_DN         0x02
#define CST816_GESTURE_MOVE_LT         0x03
#define CST816_GESTURE_MOVE_RT         0x04
#define CST816_GESTURE_CLICK           0x05
#define CST816_GESTURE_DBLCLICK        0x0B
#define CST816_GESTURE_LONGPRESS       0x0C

#define REG_STATUS                     0x00
#define REG_GESTURE_ID                 0x01
#define REG_FINGER_NUM                 0x02
#define REG_XPOS_HIGH                  0x03
#define REG_XPOS_LOW                   0x04
#define REG_YPOS_HIGH                  0x05
#define REG_YPOS_LOW                   0x06
#define REG_CHIP_ID                    0xA7
#define REG_FW_VERSION                 0xA9
#define REG_IRQ_CTL                    0xFA
#define REG_DIS_AUTOSLEEP              0xFE

#define IRQ_EN_MOTION                  0x70

#define CST816_POINT_NUM        1

#define SENSOR_I2C_READ(reg, buff, len) \
	do {\
		tal_tp_i2c_read(CST816_ADDR, reg, buff, len, REG_ADDR_8BITS);\
	}while (0)

#define SENSOR_I2C_WRITE(reg, buff, len) \
	do {\
		tal_tp_i2c_write(CST816_ADDR, reg, buff, len, REG_ADDR_8BITS);\
	}while (0)


static void tp_cst816x_init(struct ty_tp_device_cfg *cfg)
{
	if (!cfg || !cfg->tp_cfg)
	{
		PR_ERR("%s, input args is null!\r\n", __func__);
		return;
	}   

    tkl_gpio_write(cfg->tp_cfg->tp_rst.pin, !cfg->tp_cfg->tp_rst.active_level);
    tal_system_sleep(50);
    
    OPERATE_RET rt = OPRT_OK;
    uint8_t chip_id = 0, tmp = 0;

    SENSOR_I2C_READ(REG_CHIP_ID, &chip_id, sizeof(chip_id));
    PR_DEBUG("[==== CST816X] Touch Chip id: 0x%08x\r\n", chip_id);

    tmp = 0x01;
    SENSOR_I2C_WRITE(REG_DIS_AUTOSLEEP, &tmp, 1);

    tmp = IRQ_EN_MOTION;
    SENSOR_I2C_WRITE(REG_IRQ_CTL, &tmp, 1);
}


static void tp_cst816x_read_pont_info(ty_tp_read_data_t *read_point)
{
	if (!read_point || !read_point->point_arr)
	{
		PR_ERR("[==== CST816X] read input error \r\n");
		return;
	}   
    
    uint8_t read_num =0, finger_num = 0;
    uint8_t buf[13];

    SENSOR_I2C_READ(REG_STATUS, buf, sizeof(buf));

    // PR_DEBUG("[==== CST816X] %d  %d  %d  %d  %d  %d  %d  %d  %d  %d  %d  %d  %d  %d ",
    //     buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
    //     buf[7], buf[8], buf[9], buf[10], buf[11], buf[12], buf[13]);

	finger_num = buf[REG_FINGER_NUM];
    read_point->point_cnt = finger_num;
    read_point->support_get_event = FALSE;

    if(finger_num) 
    {
        /* get point coordinates */
        for (uint8_t i = 0; i < CST816_POINT_NUM; i++) {
            read_point->point_arr[i].tp_point_id = 0;
            
            uint16_t mirror_xy_y = ((buf[REG_XPOS_HIGH] & 0x0f) << 8) + buf[REG_XPOS_LOW];
            uint16_t mirror_xy_x = ((buf[REG_YPOS_HIGH] & 0x0f) << 8) + buf[REG_YPOS_LOW];

            uint16_t flipped_x = (320 - 1) - mirror_xy_x;
            uint16_t flipped_y = mirror_xy_y;  // Y轴保持XY镜像后的结果

            read_point->point_arr[i].tp_point_x = flipped_x;
            read_point->point_arr[i].tp_point_y = flipped_y;
        }
    }
    else
    {
        return;
    }

}

const ty_tp_device_cfg_t tp_cst816x_device =
{
	.name = "cst816x",
    .width = 320,
    .height = 240,
    .intr_type = TUYA_GPIO_IRQ_FALL,
    .mirror_type = TP_MIRROR_NONE,
    .max_support_tp_num = CST816_POINT_NUM,
	.init = tp_cst816x_init,
	.read_pont_info = tp_cst816x_read_pont_info,
};
#endif
