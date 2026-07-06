/**
 * @file libjpeg_turbo_encode.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "libjpeg_turbo_encode.h"

#if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
#include <stdio.h>
#include <jpeglib.h>
#include <jpegint.h>
#include <setjmp.h>
#include "img_utility.h"
#include "ty_gui_fs.h"
#include "tkl_memory.h"

/*********************
 *      DEFINES
 *********************/ 

/**********************
 *      TYPEDEFS
 **********************/
struct error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/
static void error_exit(j_common_ptr cinfo) {
    struct error_mgr* myerr = (struct error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

static unsigned char* rgb565_to_rgb888(const uint16_t* rgb565_data, int width, int height) {
    unsigned char* rgb888 = tkl_system_psram_malloc(width * height * 3);
    if (!rgb888) return NULL;

    for (int i = 0; i < width * height; i++) {
        uint16_t pixel = rgb565_data[i];
        
        // 从 RGB565 提取分量
        uint8_t r = (pixel >> 8) & 0xF8;  // 高5位
        uint8_t g = (pixel >> 3) & 0xFC;  // 中间6位  
        uint8_t b = (pixel << 3) & 0xF8;  // 低5位
        
        // 扩展到 8位（简单左移，可能不是最佳方法）
        r |= (r >> 5);  // 5位 -> 8位
        g |= (g >> 6);  // 6位 -> 8位
        b |= (b >> 5);  // 5位 -> 8位
        
        rgb888[i * 3] = r;
        rgb888[i * 3 + 1] = g;
        rgb888[i * 3 + 2] = b;
    }
    return rgb888;
}

OPERATE_RET encode_jpeg_advanced(unsigned char** out_buffer, 
                        unsigned long *out_size,
                        unsigned char* pixel_data,
                        int width, int height,
                        int pixel_format,
                        int quality,
                        int progressive) {
    OPERATE_RET ret = OPRT_COM_ERROR;
    struct jpeg_compress_struct cinfo;
    struct error_mgr jerr;
    unsigned char* converted_data = NULL;
    unsigned char* __pixel_data = pixel_data;
    int __pixel_format = pixel_format;

    JSAMPROW row_pointer[1];
    int row_stride, components;

    // 设置错误处理
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = error_exit;
    
    if (setjmp(jerr.setjmp_buffer)) {
        TY_GUI_LOG_PRINT("encoding error");
        jpeg_destroy_compress(&cinfo);
        return ret;
    }

    jpeg_create_compress(&cinfo);

    jpeg_mem_dest(&cinfo, out_buffer, out_size);

    switch (pixel_format) {
        case JCS_GRAYSCALE:
            components = 1;
            row_stride = width;
            break;
        case JCS_RGB:
            components = 3;
            row_stride = width * 3;
            break;
        case JCS_YCbCr:
            components = 3;
            row_stride = width * 3;
            break;
        case JCS_RGB565:
            converted_data = rgb565_to_rgb888((uint16_t*)pixel_data, width, height);
            if (!converted_data) {
                TY_GUI_LOG_PRINT("%s: convert rgb888 fail ???\n", __func__);
                jpeg_destroy_compress(&cinfo);
                return ret;
            }
            __pixel_data = converted_data;
            __pixel_format = JCS_RGB;
            components = 3;
            row_stride = width * 3;
            break;
        default:
            TY_GUI_LOG_PRINT("%s: unsupport pixel_format '%d' ???\n", __func__, pixel_format);
            jpeg_destroy_compress(&cinfo);
            return ret;
    }

    // 设置图像参数
    cinfo.input_components = components;
    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.in_color_space = __pixel_format;

    // 设置默认参数
    cinfo.data_precision = 8;
    jpeg_set_defaults(&cinfo);

    // 设置质量
    jpeg_set_quality(&cinfo, quality, TRUE);

#if 0
    // 是否使用渐进式 JPEG
    if (progressive) {
        jpeg_simple_progression(&cinfo);
    }

    // 设置采样因子（影响压缩率）
    // 4:2:0 采样（通常用于高质量压缩）
    cinfo.comp_info[0].h_samp_factor = 2;
    cinfo.comp_info[0].v_samp_factor = 2;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;
#endif

    // 开始压缩
    jpeg_start_compress(&cinfo, TRUE);

    // 计算行跨度并写入数据
    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] =  (JSAMPROW)&__pixel_data[cinfo.next_scanline * row_stride];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    if (converted_data)
        tkl_system_psram_free(converted_data);
    return OPRT_OK;
}

OPERATE_RET jpg_enc_img(const char *out_file_name, uint8_t *img_data, DISPLAY_PIXEL_FORMAT_E fmt, uint32_t img_width, uint32_t img_height, uint32_t quality/*50-80*/)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    TUYA_FILE file_hdl = NULL;
    unsigned char* output_buffer = NULL;
    unsigned long output_size = 0;
    INT_T len = 0;

    if (fmt == TY_PIXEL_FMT_RGB565)
        ret = encode_jpeg_advanced(&output_buffer, &output_size, img_data, img_width, img_height, JCS_RGB565, quality, 1);
    else if (fmt == TY_PIXEL_FMT_RGB888)
        ret = encode_jpeg_advanced(&output_buffer, &output_size, img_data, img_width, img_height, JCS_RGB, quality, 1);
    else {
        TY_GUI_LOG_PRINT("%s: unsupport fmt '%d' ?\n", __func__, fmt);
    }

    if (ret == OPRT_OK) {
        if ((file_hdl = ty_gui_fopen(out_file_name, "wb")) == NULL) {
            TY_GUI_LOG_PRINT("%s: open file '%s' fail ???\n", __func__, out_file_name);
            ret = OPRT_COM_ERROR;
        }
        else {
            len = ty_gui_fwrite(output_buffer, output_size, file_hdl);
            if (len != output_size) {
                TY_GUI_LOG_PRINT("%s: write file '%s' len mismatch '%d'-'%d'  ???\n", __func__, out_file_name, len, output_size);
                ret = OPRT_COM_ERROR;
            }
            ty_gui_fclose(file_hdl);
        }
    }

    if (output_buffer != NULL)
        tkl_system_sram_free(output_buffer);            //此内存由libjpeg-turbo库申请,所以统一使用库内存释放接口!
    TY_GUI_LOG_PRINT("%s: gen JPEG file '%s' '%s' !\n", __func__, out_file_name, (ret == OPRT_OK)?"successful":"fail");
    if (ret == OPRT_OK) {
        TY_GUI_LOG_PRINT("%s: JPEG file '%s' size:%d, width:%d, height:%d !\n", __func__, out_file_name, output_size, img_width, img_height);
    }

    return ret;
}

#else
OPERATE_RET jpg_enc_img(const char *out_file_name, uint8_t *img_data, DISPLAY_PIXEL_FORMAT_E fmt, uint32_t img_width, uint32_t img_height, uint32_t quality/*50-80*/)
{
    TY_GUI_LOG_PRINT("%s: unsupport !\n", __func__);
    return OPRT_NOT_SUPPORTED;
}
#endif