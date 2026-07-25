#ifndef __IMG_UTILITY_H
#define __IMG_UTILITY_H

#include "tuya_cloud_types.h"
#include "tuya_app_gui_core_config.h"
#include "lvgl.h"

typedef enum {
    image_type_png = 0,             //png 文件
    image_type_jpg,                 //jpg 文件
    image_type_jpeg,                //jpeg文件
    image_type_gif,                 //gif 文件
    image_type_struct,              //png,jpg或jpeg数据结构体
    image_type_struct_gif,          //gif数据结构体
    image_type_unknown
} GUI_IMAGE_TYPE_E;

typedef struct
{
    unsigned char *frame;
    unsigned int length;
}gui_img_frame_buffer_t;

GUI_IMAGE_TYPE_E get_image_type(char *img_name);

//#define ENABLE_STB_IMG_SCALE
//#define ENABLE_JPEG_IMAGE_DECODE_WITH_HW

#ifdef TUYA_GUI_IMG_PRE_DECODE

/*
 读取图像文件
 返回:NULL失败
*/
gui_img_frame_buffer_t *img_read_file(char *file_name);

/*
 读取图像原始数据流
 返回:NULL失败
*/
gui_img_frame_buffer_t *img_read_raw_data(unsigned char *img_data, unsigned int img_size);

void jpg_convert_color_depth(unsigned char * img_p, unsigned int px_cnt);

void img_byte_order_swap(unsigned char * img_p, unsigned int px_cnt);

/*
 图像文件加载: 解码jpg格式
 jpg_hw_dec:是否使用硬件jpg解码
 (
    jpg硬件解码有以下限制：
    1.只能解码yuv422格式，常用图片都是yuv420格式，需要先使用工具转换成yuv422格式(参考\project_resource_sample\jpgconvert\readme.txt进行格式转换)
    2.仅支持像素宽度为32的倍数, 像素高度为8的倍数的图片
    3.开启双frame_buf
 )
 返回:0-成功; 否则失败
*/
OPERATE_RET jpg_img_load(char *filename, lv_img_dsc_t *img_dst, bool jpg_hw_dec);

#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
/*
 图像文件加载: 支持缩放解码jpg文件格式
 target_width: target output width(0:不缩放).
 target_height: target output height(0:不缩放).
 返回:0-成功; 否则失败
*/
OPERATE_RET jpg_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height);
#endif

/*
 图像文件卸载: jpg格式
*/
void jpg_img_unload(lv_img_dsc_t *img_dst);

/*
 图像文件加载: png格式
 返回:0-成功; 否则失败
*/
OPERATE_RET png_img_load(char *filename, lv_img_dsc_t *img_dst);

/*
 图像文件加载: 支持缩放的png文件格式解码
 * @brief load png file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @param[in] target_width: target output width(0:不缩放).
 * @param[in] target_height: target output height(0:不缩放).
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
*/
OPERATE_RET png_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height);

/*
 图像文件卸载: png格式
*/
void png_img_unload(lv_img_dsc_t *img_dst);

/*
 图像数据流加载: jpg/jpeg/png
 jpg_hw_dec:是否使用硬件jpg解码
 (
    jpg硬件解码有以下限制：
    1.只能解码yuv422格式，常用图片都是yuv420格式，需要先使用工具转换成yuv422格式(参考\project_resource_sample\jpgconvert\readme.txt进行格式转换)
    2.仅支持像素宽度为32的倍数, 像素高度为8的倍数的图片
    3.开启双frame_buf
 )
 返回:0-成功; 否则失败
*/
OPERATE_RET raw_img_load(GUI_IMAGE_TYPE_E image_type, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, bool jpg_hw_dec);

/*
 支持缩放的png图像数据流加载
 * @param[in]target_width: target output width(0:不缩放).
 * @param[in]target_height: target output height(0:不缩放).
 返回:0-成功; 否则失败
*/
OPERATE_RET raw_png_img_load_with_scale(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height);

/*
 支持缩放的jpg/jpeg图像数据流加载
 target_width: target output width(0:不缩放).
 target_height: target output height(0:不缩放).
 返回:0-成功; 否则失败
*/
#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
OPERATE_RET raw_jpg_img_load_with_scale(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height);
#endif

/*
 图像数据流卸载: jpg/jpeg/png
*/
void raw_img_unload(lv_img_dsc_t *img_dst);

/*
 jpg/jpeg/png等图像文件加载: 
 img_file_id:图像id
 jpg_hw_dec:是否使用硬件jpg解码
 (
    jpg硬件解码有以下限制：
    1.只能解码yuv422格式，常用图片都是yuv420格式，需要先使用工具转换成yuv422格式(参考\project_resource_sample\jpgconvert\readme.txt进行格式转换)
    2.仅支持像素宽度为32的倍数, 像素高度为8的倍数的图片
    3.开启双frame_buf
 )
 返回:0-成功; 否则失败
*/
OPERATE_RET img_file_load_by_id(uint32_t img_file_id, lv_img_dsc_t *img_dst, bool jpg_hw_dec);

/*
 jpg/jpeg/png等图像文件卸载:
*/
void img_file_unload(lv_img_dsc_t *img_dst);

#ifdef TUYA_LIBWEBP
/*
 图像文件加载: 支持缩放的webp文件格式解码
 * @brief load webp file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @param[in] target_width: target output width(0:不缩放).
 * @param[in] target_height: target output height(0:不缩放).
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
*/
OPERATE_RET webp_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height);
#endif
#endif

/*
 gif文件加载
 返回:0-成功; 否则失败
*/
OPERATE_RET gif_img_load(char *filename, lv_img_dsc_t *img_dst);

/*
 gif文件卸载:
*/
void gif_img_unload(lv_img_dsc_t *img_dst);

#ifdef TUYA_LIBWEBP
/*
 支持缩放的webp图像数据流加载
 target_width: target output width(0:不缩放).
 target_height: target output height(0:不缩放).
 返回:0-成功; 否则失败
*/
OPERATE_RET raw_webp_img_load_with_scale(const uint8_t* webp_data, size_t data_size, lv_img_dsc_t *img_dst, int target_width, int target_height);

/*
 图像文件卸载: webp格式
*/
void webp_img_unload(lv_img_dsc_t *img_dst);
#endif

#endif
