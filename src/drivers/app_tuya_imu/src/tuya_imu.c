#include "tuya_imu.h"

STATIC THREAD_HANDLE s_imu_task;
STATIC SEM_HANDLE s_imu_sem;

static int imu_write_reg(uint8_t reg_addr, uint8_t value)
{
    return drv_write_regs(IMU_I2C_ID, SLAVE_ADDR, reg_addr, &value, 1);
}

static int imu_read_reg(uint8_t reg_addr, uint8_t* buffer, uint16_t bytes)
{
    return drv_read_regs(IMU_I2C_ID, SLAVE_ADDR, reg_addr, buffer, bytes);
}

#if 1
void drv_sc7a20h_cfg()
{
    imu_write_reg(SC_CTRL_REG1, 0x47);
    imu_write_reg(SC_CTRL_REG2, 0x31);
    imu_write_reg(SC_CTRL_REG3, 0x40);      //AOI中断on int1
    imu_write_reg(SC_CTRL_REG4, 0x88);      //±2g
    imu_write_reg(SC_CTRL_REG5, 0x00);
    imu_write_reg(SC_INT1_CFG,  0x2a);      //x,y,z高事件或检测
    imu_write_reg(SC_INT1_THS,  0x01);      //检测门限:1-127,值越小,灵敏度越高
    imu_write_reg(SC_INT1_DURATION, 0x00);

}
#else
void drv_sc7a20h_cfg()
{
    imu_write_reg(SC_CTRL_REG1, 0x47);
    imu_write_reg(SC_CTRL_REG2, 0x31);
    imu_write_reg(SC_CTRL_REG3, 0x40);      //AOI中断on int1
    // imu_write_reg(SC_CTRL_REG3, 0xC0);      //AOI中断on int1 && 单击中断输出到int1
    imu_write_reg(SC_CTRL_REG4, 0x88);      //±2g
    imu_write_reg(SC_CTRL_REG5, 0x00);
    imu_write_reg(SC_INT1_CFG,  0x2a);      //x,y,z高事件或检测
    imu_write_reg(SC_INT1_THS,  0x01);      //检测门限:1-127,值越小,灵敏度越高
    imu_write_reg(SC_INT1_DURATION, 0x00);


    // imu_write_reg(SC_CLICK_CFG, 0x0F);
    // imu_write_reg(SC_CLICK_COEFF1, 0x18);
    // imu_write_reg(SC_CLICK_COEFF2, 0x58);
    // imu_write_reg(SC_CLICK_COEFF3, 0x03);
    // imu_write_reg(SC_CLICK_COEFF4, 0x01);

    // imu_write_reg(SC_CLICK_CFG, 0x07);
    // imu_write_reg(SC_CLICK_COEFF1, 0x28);
    // imu_write_reg(SC_CLICK_COEFF2, 0x58);
    // imu_write_reg(SC_CLICK_COEFF3, 0x05);
    // imu_write_reg(SC_CLICK_COEFF4, 0x01);
}
#endif

OPERATE_RET ty_drv_sc7a20_read_accel(SC7A20_ACCEL_DATA_T *acc_data)
{
    OPERATE_RET rt = OPRT_OK;

    if (acc_data == NULL) {
        return OPRT_INVALID_PARM;
    }
    
    rt = imu_read_reg(SC_OUT_X_L|SC7A20_INC_READ, acc_data->reg_value, 6);

    /* 取10位带符号整形数据 */
    acc_data->axis.accel_x >>= 6;
    acc_data->axis.accel_y >>= 6;
    acc_data->axis.accel_z >>= 6;

    return rt;
}

VOID imu_irq_notify(VOID *args)
{
    tal_semaphore_post(s_imu_sem);
}

STATIC VOID imu_status_check_task(VOID *args)
{
    OPERATE_RET rt;
    STATIC SC7A20_ACCEL_DATA_T acc_data = {0};
    uint8_t click_src = 0, aoi1_src = 0;
    SC7A20_ATTITUDE_E pre_attitude = SC_INIT_STATUS;
    SC7A20_ATTITUDE_E now_attitude = SC_INIT_STATUS;

    drv_sc7a20h_cfg();

    while(1)
    {
        rt = tal_semaphore_wait(s_imu_sem, 1000);
        if(rt == OPRT_OK)
        {
            imu_read_reg(SC_INT1_SOURCE, &aoi1_src, 1);
            TAL_PR_DEBUG("SC_INT1_SOURCE: 0x%02X", aoi1_src);
            imu_read_reg(SC_CLICK_SRC, &click_src, 1);
            TAL_PR_DEBUG("SC_CLICK_SRC: 0x%02X", click_src);
            // if (click_src & 0x01) {
            //     TAL_PR_DEBUG("click one: 0x%02X", click_src);
            // }
            
            // if (aoi1_src & 0x40) {
            //     TAL_PR_DEBUG("AOT1: 0x%02X", aoi1_src);
            // }
        }

        ty_drv_sc7a20_read_accel(&acc_data);
        TAL_PR_DEBUG("[tuya_imu]accel_x:%d, accel_y:%d, accel_z:%d", acc_data.axis.accel_x, acc_data.axis.accel_y, acc_data.axis.accel_z);

        if (acc_data.axis.accel_x > SC7A20_TILT_THR) {
            now_attitude = SC_ROLL_LEFT_TILT;
        }
        else if (acc_data.axis.accel_x < -SC7A20_TILT_THR) {
            now_attitude = SC_ROLL_RIGHT_TILT;
        }
        else if (acc_data.axis.accel_y > SC7A20_TILT_THR) {
            now_attitude = SC_PITCH_BACK_TILT;
        }
        else if (acc_data.axis.accel_y < -SC7A20_TILT_THR) {
            now_attitude = SC_PITCH_FRONT_TILT;
        }
        else if (acc_data.axis.accel_z > SC7A20_TILT_THR) {
            now_attitude = SC_YAW_UPRIGHT;
        }
        else if (acc_data.axis.accel_z < -SC7A20_TILT_THR) {
            now_attitude = SC_YAW_INVERT;
        }

        if (now_attitude != pre_attitude) 
        {
            pre_attitude = now_attitude;
            TAL_PR_DEBUG("[tuya_imu]now_attitude = %d, pre_attitude = %d", now_attitude, pre_attitude);
        }

    }
}

int tuya_imu_init(TUYA_I2C_NUM_E i2c_num, TUYA_GPIO_NUM_E gpio_num)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_IIC_BASE_CFG_T i2c_cfg = {
        .role = TUYA_IIC_MODE_MASTER,
        .speed = TUYA_IIC_BUS_SPEED_100K,
        .addr_width = TUYA_IIC_ADDRESS_7BIT
    };

    rt = tkl_i2c_init(i2c_num, &i2c_cfg);
    if(rt != OPRT_OK) {
        TAL_PR_ERR("i2c[%d] init failed:%d",i2c_num, rt);
        return rt;
    }

    uint8_t value = 0;
    imu_read_reg(SC_WHO_AM_I, &value, 1);
    TAL_PR_DEBUG("[tuya_imu] sc7a22h chipid [0x%02x]", value);
    if(value != SC_CHIP_ID)
    {
        return OPRT_COM_ERROR;
    }

    //配置中断
    TUYA_GPIO_BASE_CFG_T key_cfg = {
        .mode = TUYA_GPIO_PULLUP,
        .direct = TUYA_GPIO_INPUT,
        .level = TUYA_GPIO_LEVEL_HIGH
    };

    TUYA_GPIO_IRQ_T irq_cfg = {
        .mode = TUYA_GPIO_IRQ_FALL,
        .cb   = imu_irq_notify,
        .arg  = (void *)(intptr_t)gpio_num,  
    };

    TUYA_CALL_ERR_RETURN(tkl_gpio_init(gpio_num, &key_cfg));
    TUYA_CALL_ERR_RETURN(tal_gpio_irq_init(gpio_num, &irq_cfg));
    TUYA_CALL_ERR_RETURN(tkl_gpio_irq_enable(gpio_num));
    TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&s_imu_sem, 0, 1));

    THREAD_CFG_T thrd_cfg = {
        .priority = THREAD_PRIO_5,
        .stackDepth = 3* 1024,
        .thrdname = "imu_task",
        #ifdef ENABLE_EXT_RAM
        .psram_mode = 1,
        #endif            
    };
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&s_imu_task, NULL, NULL, imu_status_check_task, NULL, &thrd_cfg));

    return OPRT_OK;
}
