#include "tkl_i2c.h"
#include "tal_system.h"
#include <stdint.h>
#include <stdio.h>
#include "tal_log.h"

int drv_read_regs(uint8_t port, uint8_t slave_addr, uint8_t reg_addr, uint8_t *buffer, uint16_t bytes)
{
    OPERATE_RET rt;
    rt = tkl_i2c_master_send(port, slave_addr, &reg_addr, 1, true);
    if(rt != OPRT_OK)
    {
        TAL_PR_ERR("[drv_read_regs] send reg_adr[%x] read failed:%d", reg_addr, rt);  
        return rt;
    }

    rt = tkl_i2c_master_receive(port, slave_addr, buffer, bytes, false);
    if(rt != OPRT_OK)
    {
        TAL_PR_ERR("[drv_read_regs] receive reg_adr[%x] read failed:%d", reg_addr, rt);  
        return rt;
    }

    return rt;
}

int drv_write_regs(uint8_t port, uint8_t slave_addr, uint8_t reg_addr, uint8_t *buffer, uint16_t bytes)
{
    if(bytes >= 16) {
        TAL_PR_INFO("[drv_write_regs] bytes %d", bytes);
        return -1;
    }

    uint8_t buff[16];
    buff[0] = reg_addr;
    memcpy(buff+1, buffer, bytes);

    OPERATE_RET rt = tkl_i2c_master_send(port, slave_addr, buff, bytes+1, false);
    if(rt != OPRT_OK) {
        TAL_PR_ERR("[drv_write_regs] reg_addr[%x] write failed:%d", reg_addr, rt);  
    }
    return rt;
}