#include "tal_tp_i2c.h"
#include "uni_log.h"
#include "tkl_i2c.h"
#include "tkl_pinmux.h"

#define TUYA_I2C_SCL (g_tp_i2c_idx << 1)
#define TUYA_I2C_SDA ((g_tp_i2c_idx << 1) + 1)

TUYA_I2C_NUM_E g_tp_i2c_idx = TUYA_I2C_NUM_MAX;
static uint8_t g_write_buff[TP_I2C_MSG_MAX_LEN] = {0};

OPERATE_RET tal_tp_i2c_init(uint8_t clk, uint8_t sda, TUYA_I2C_NUM_E idx)
{
    if (idx >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, input args is invalid!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

    PR_INFO("set tp i2c, clk: %d sda: %d, idx: %d\r\n", clk, sda, idx);
    g_tp_i2c_idx = idx;

    tkl_io_pinmux_config(clk, TUYA_I2C_SCL);
    tkl_io_pinmux_config(sda, TUYA_I2C_SDA);

    // tp used sw i2c1
    TUYA_IIC_BASE_CFG_T cfg = {
        .role = TUYA_IIC_MODE_MASTER,
        .speed = TUYA_IIC_BUS_SPEED_100K,
        .addr_width = TUYA_IIC_ADDRESS_7BIT,
    };
    tkl_i2c_init(g_tp_i2c_idx, &cfg);

    return OPRT_OK;
}

OPERATE_RET tal_tp_i2c_deinit(void)
{
    if (g_tp_i2c_idx >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, TUYA_I2C_NUM is not init!\r\n", __func__);
        return OPRT_RESOURCE_NOT_READY;
    }
    
    OPERATE_RET ret = OPRT_OK;
    ret = tkl_i2c_deinit(g_tp_i2c_idx);
    g_tp_i2c_idx = TUYA_I2C_NUM_MAX;

    return ret;
}

OPERATE_RET tal_tp_i2c_read(uint8_t addr, uint16_t reg, uint8_t *buf, uint16_t buf_len, uint8_t is_16_reg)
{
    if (!buf || !buf_len)
    {
        PR_ERR("%s, input args is invalid!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

    if (g_tp_i2c_idx >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, TUYA_I2C_NUM is not init!\r\n", __func__);
        return OPRT_RESOURCE_NOT_READY;
    }

    if (buf_len > TP_I2C_MSG_MAX_LEN)
    {
        PR_ERR("%s, buf_len %d is overflow the limit(%d)\r\n", __func__, buf_len, TP_I2C_MSG_MAX_LEN);
        return OPRT_EXCEED_UPPER_LIMIT;
    }

    uint8_t write_data[2] = {0};
    uint8_t wirte_data_len = 0;
    if (is_16_reg)
    {
        write_data[0] = (uint8_t)((reg >> 8) & 0xFF);
        write_data[1] = (uint8_t)(reg & 0xFF);
        wirte_data_len = 2;
    }
    else
    {
        write_data[0] = (uint8_t)(reg & 0xFF);
        wirte_data_len = 1;
    }

    tkl_i2c_master_send(g_tp_i2c_idx, addr, write_data, wirte_data_len, 1);

    tkl_i2c_master_receive(g_tp_i2c_idx, addr, buf, buf_len, 0);

    return OPRT_OK;
}

OPERATE_RET tal_tp_i2c_write(uint8_t addr, uint16_t reg, uint8_t *buf, uint16_t buf_len, uint8_t is_16_reg)
{
    if (!buf || !buf_len)
    {
        PR_ERR("%s, input args is invalid!\r\n", __func__);
        return OPRT_INVALID_PARM;
    }

    if (g_tp_i2c_idx >= TUYA_I2C_NUM_MAX)
    {
        PR_ERR("%s, TUYA_I2C_NUM is not init!\r\n", __func__);
        return OPRT_RESOURCE_NOT_READY;
    }

    uint8_t reg_len = is_16_reg ? 2 : 1;
    uint8_t write_data_len = reg_len + buf_len;

    if (write_data_len > TP_I2C_MSG_MAX_LEN)
    {
        PR_ERR("%s, write_data_len(%d+%d) is overflow the limit(%d)\r\n", __func__, buf_len, reg_len, TP_I2C_MSG_MAX_LEN);
        return OPRT_EXCEED_UPPER_LIMIT;
    }

    memcpy(g_write_buff + reg_len, buf, buf_len);

    if (is_16_reg)
    {
        g_write_buff[0] = (uint8_t)((reg >> 8) & 0xFF);
        g_write_buff[1] = (uint8_t)(reg & 0xFF);
    }
    else
    {
        g_write_buff[0] = (uint8_t)(reg & 0xFF);
    }

    tkl_i2c_master_send(g_tp_i2c_idx, addr, g_write_buff, write_data_len, 0);

    return OPRT_OK;
}
