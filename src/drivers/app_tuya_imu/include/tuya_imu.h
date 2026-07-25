#ifndef __TUYA_IMU_H__
#define __TUYA_IMU_H__

#include "tuya_cloud_types.h"
#include "tuya_device_cfg.h"
#include "tkl_i2c.h"
#include "tal_log.h"
#include "tal_system.h"
#include "tal_memory.h"
#include "tal_sw_timer.h"
#include "base_event.h"
#include "tal_thread.h"
#include "tal_semaphore.h"
#include "tal_mutex.h"

#define IMU_I2C_ID      TUYA_I2C_NUM_0

#define SC7A20_INC_READ      0x80  //递增读取
#define SC7A20_TILT_THR      200   //倾斜阈值

#define SLAVE_ADDR      0x19 
#define SC_CHIP_ID      0x11

#define SC_WHO_AM_I     0x0F
#define SC_CTRL_REG0    0x1F
#define SC_CTRL_REG1    0x20
#define SC_CTRL_REG2    0x21
#define SC_CTRL_REG3    0x22
#define SC_CTRL_REG4    0x23
#define SC_CTRL_REG5    0x24
#define SC_CTRL_REG6    0x25
#define SC_STATUS_REG   0x27
#define SC_OUT_X_L      0x28
#define SC_OUT_X_H      0x29
#define SC_OUT_Y_L      0x2A
#define SC_OUT_Y_H      0x2B
#define SC_OUT_Z_L      0x2C
#define SC_OUT_Z_H      0x2D
#define SC_FIFO_CTRL_REG   0x2E
#define SC_FIFO_SRC_REG    0x2F
#define SC_INT1_CFG        0x30
#define SC_INT1_SOURCE     0x31
#define SC_INT1_THS        0x32
#define SC_INT1_DURATION   0x33
#define SC_INT2_CFG        0x34
#define SC_INT2_SOURCE     0x35
#define SC_INT2_THS        0x36
#define SC_INT2_DURATION   0x37
#define SC_CLICK_CFG       0x38
#define SC_CLICK_SRC       0x39
#define SC_CLICK_COEFF1    0x3A
#define SC_CLICK_COEFF2    0x3B
#define SC_CLICK_COEFF3    0x3C
#define SC_CLICK_COEFF4    0x3D

typedef enum
{
    SC_INIT_STATUS = 0x00,
    SC_ROLL_LEFT_TILT,         // X轴左倾
    SC_ROLL_RIGHT_TILT,        // X轴右倾
    SC_PITCH_FRONT_TILT,       // Y轴前倾
    SC_PITCH_BACK_TILT,        // Y轴后倾
    SC_YAW_UPRIGHT,            // Z轴正立
    SC_YAW_INVERT,             // Z轴倒置
    SC_ATTI_MAX,
}SC7A20_ATTITUDE_E;

typedef union {
    UINT8_T reg_value[6];
    struct {
        INT16_T accel_x;
        INT16_T accel_y;
        INT16_T accel_z;
    }axis;
}SC7A20_ACCEL_DATA_T;

int drv_read_regs(uint8_t port, uint8_t slave_addr, uint8_t reg_addr, uint8_t *buffer, uint16_t bytes);

int drv_write_regs(uint8_t port, uint8_t slave_addr, uint8_t reg_addr, uint8_t *buffer, uint16_t bytes);

int tuya_imu_init(TUYA_I2C_NUM_E i2c_num, TUYA_GPIO_NUM_E gpio_num);


#endif  //__TUYA_IMU_H__