#include "tal_display_service.h"
#include "tal_memory.h"

#define UNACTIVE_LEVEL(x) ((x == 0)? 1: 0)
/**
* @brief tal_display_open
*
* @param[in] device: device
* @param[in] cfg: gpio cfg
*
* @note This API is used to open display device.
*
* @return !NULL on success. NULL on error
*/
TY_DISPLAY_HANDLE tal_display_open(ty_display_device_s *device, ty_display_cfg *cfg)
{
    if(device == NULL || cfg == NULL) {
        return OPRT_INVALID_PARM;
    }

    ty_display_device_s *handle = NULL;
    handle = Malloc(sizeof(ty_display_device_s));
    if(handle == NULL) {
        return NULL;
    }
    
    memcpy(handle, device, sizeof(ty_display_device_s));

    ty_display_cfg *display_cfg = Malloc(sizeof(ty_display_cfg));
    if(display_cfg == NULL) {
        Free(handle);
        return NULL;
    }

    memcpy(display_cfg, cfg, sizeof(ty_display_cfg));

    OPERATE_RET ret = 0;
    if(handle->type == DISPLAY_RGB) {
        handle->rgb.display_cfg = &(display_cfg->rgb_cfg);
        ret = tal_display_rgb_open(&handle->rgb);
    }else if(handle->type == DISPLAY_8080) {
        handle->mcu8080.display_cfg = &(display_cfg->mcu8080_cfg);
        ret = tal_display_8080_open(&handle->mcu8080);
    }else if(handle->type == DISPLAY_SPI) {
        handle->spi.display_cfg = &(display_cfg->spi_cfg);
        ret = tal_display_spi_open(&handle->spi);
    }else if(handle->type == DISPLAY_QSPI) {
        handle->qspi.display_cfg = &(display_cfg->qspi_cfg);
        ret = tal_display_qspi_open(&handle->qspi);
    }

    if(ret) {
        Free(handle);
        Free(display_cfg);
        return NULL;
    }

    return handle;
}

/**
* @brief tal_display_close
*
* @param[in] handle: display_device handle
*
* @note This API is used to close display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_close(TY_DISPLAY_HANDLE handle)
{
    OPERATE_RET ret = 0;
    if(handle->type == DISPLAY_RGB) { 
        ret = tal_display_rgb_close(&handle->rgb);
        Free(handle->rgb.display_cfg);
    }else if(handle->type == DISPLAY_8080) {
        ret = tal_display_8080_close(&handle->mcu8080);
        Free(handle->mcu8080.display_cfg);
    }else if(handle->type == DISPLAY_SPI) {
        ret = tal_display_spi_close(&handle->spi);
        Free(handle->spi.display_cfg);
    }else if(handle->type == DISPLAY_QSPI) {
        ret = tal_display_qspi_close(&handle->qspi);
        Free(handle->qspi.display_cfg);
    }else {
        return -2;
    }

    Free(handle);
    return ret;
}


/**
 *
* @brief tal_display_flush
*
* @param[in] handle: display_device handle
* @param[in] frame_buffer: frame buff
*
* @note This API is used to flush display device.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_flush(TY_DISPLAY_HANDLE handle, ty_frame_buffer_t *frame_buff)
{
    OPERATE_RET ret = 0;

    if(handle->type == DISPLAY_RGB) {
        ret = tal_display_rgb_flush(&(handle->rgb), frame_buff);
    }else if(handle->type == DISPLAY_8080) {
        ret = tal_display_8080_flush(&(handle->mcu8080), frame_buff);
    }else if(handle->type == DISPLAY_SPI) {
        ret = tal_display_spi_flush(&(handle->spi), frame_buff);
    }else if(handle->type == DISPLAY_QSPI) {
        ret = tal_display_qspi_flush(&(handle->qspi), frame_buff);
    }else {
        return -2;
    }

    return ret;
}

/**
* @brief tal_display_bl_open
*
* @param[in] handle: display_device handle
*
* @note This API is used to open bl.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_bl_open(TY_DISPLAY_HANDLE handle)
{
    TUYA_GPIO_NUM_E pin_id;
    TUYA_GPIO_LEVEL_E level;
    if(handle->type == DISPLAY_RGB) {
        pin_id = handle->rgb.display_cfg->bl.pin;
        level = handle->rgb.display_cfg->bl.active_level;
    }else if(handle->type == DISPLAY_8080) {
        pin_id = handle->mcu8080.display_cfg->bl.pin;
        level = handle->mcu8080.display_cfg->bl.active_level;
    }else if(handle->type == DISPLAY_QSPI) {
        pin_id = handle->qspi.display_cfg->bl.pin;
        level = handle->qspi.display_cfg->bl.active_level;
    }else if(handle->type == DISPLAY_SPI) {
        pin_id = handle->spi.display_cfg->bl.pin;
        level = handle->spi.display_cfg->bl.active_level;
    }else {
        return -2;
    }

    if(pin_id == TUYA_GPIO_NUM_MAX) {
        return 0;
    }

    return tkl_gpio_write(pin_id, level);
}

/**
* @brief tal_display_bl_close
*
* @param[in] handle: display_device handle
*
* @note This API is used to close bl.
*
* @return OPRT_OK on success. Others on error, please refer to tuya_error_code.h
*/
OPERATE_RET tal_display_bl_close(TY_DISPLAY_HANDLE handle)
{
    TUYA_GPIO_NUM_E pin_id;
    TUYA_GPIO_LEVEL_E level;
    if(handle->type == DISPLAY_RGB) {
        pin_id = handle->rgb.display_cfg->bl.pin;
        level = UNACTIVE_LEVEL(handle->rgb.display_cfg->bl.active_level);
    }else if(handle->type == DISPLAY_8080) {
        pin_id = handle->mcu8080.display_cfg->bl.pin;
        level = UNACTIVE_LEVEL(handle->mcu8080.display_cfg->bl.active_level);
    }else if(handle->type == DISPLAY_QSPI) {
        pin_id = handle->qspi.display_cfg->bl.pin;
        level = handle->qspi.display_cfg->bl.active_level;
    }else if(handle->type == DISPLAY_SPI) {
        pin_id = handle->spi.display_cfg->bl.pin;
        level = UNACTIVE_LEVEL(handle->spi.display_cfg->bl.active_level);
    }else {
        return -2;
    }

    if(pin_id == TUYA_GPIO_NUM_MAX) {
        return 0;
    }
    return tkl_gpio_write(pin_id, level);
}
