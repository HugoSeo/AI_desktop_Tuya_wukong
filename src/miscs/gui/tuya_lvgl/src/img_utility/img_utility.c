#include "img_utility.h"

//#if (CONFIG_SYS_CPU1)
#include <stdio.h>
#include <math.h>
#include "lvgl.h"
#include "common_function.h"
#include "tuya_app_gui_fs_path.h"
#include "ty_gui_fs.h"
#include "tkl_memory.h"
#ifdef TUYA_LIBWEBP
#include "webp/decode.h"
#endif

#ifdef TUYA_GUI_IMG_PRE_DECODE

#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
//use libjpeg-turbo app component
#include "libjpeg_turbo_decode.h"
#else
    #ifdef ENABLE_TUYA_TKL_JPEG_DECODE
    //use tkl jpeg decode api
    #include "tkl_jpeg_codec.h"
    #else
    //This is the only place that depends on the development environment header files.
    //#include "os/os.h"
    //#include "driver/media_types.h"
    //#include "modules/jpeg_decode_sw.h"
    //#include "jpeg_hw_decode.h"
    #endif
#endif

#if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
#if !(defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1))
    #ifdef ENABLE_TUYA_TKL_JPEG_DECODE
    //TODO...
    #else
    static jpeg_dec_handle_t jpeg_dec_handle = NULL;
    #endif
#endif
#endif

#define PNG_IMG_DECODE_IN_MEMORY
//#define PNG_DECODE_WITH_STB_IMAGE
#define PNG_DECODE_WITH_LIBSPNG
//#define IMG_DECODING_TIME_TEST

#if defined(PNG_DECODE_WITH_STB_IMAGE)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"
#elif defined(PNG_DECODE_WITH_LIBSPNG)
#include <spng.h>
#include <inttypes.h>
#ifdef ENABLE_STB_IMG_SCALE
#include "stb_image_resize2.h"
#endif
#endif

#ifdef IMG_DECODING_TIME_TEST
extern unsigned long long int __current_timestamp(void);
#endif

#if defined(PNG_DECODE_WITH_STB_IMAGE)
static void stabi_convert_color_depth(uint8_t * img, uint32_t px_cnt)
{
#if LV_COLOR_DEPTH == 32
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    lv_color_t * img_c = (lv_color_t *) img;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img_c[i].ch.red = c.ch.blue;
        img_c[i].ch.blue = c.ch.red;
    }
#elif LV_COLOR_DEPTH == 16
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.blue, img_argb[i].ch.green, img_argb[i].ch.red);
        img[i * 3 + 2] = img_argb[i].ch.alpha;
        img[i * 3 + 1] = c.full >> 8;
        img[i * 3 + 0] = c.full & 0xFF;
    }
#elif LV_COLOR_DEPTH == 8
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = c.full;
    }
#elif LV_COLOR_DEPTH == 1
    lv_color32_t * img_argb = (lv_color32_t *)img;
    uint8_t b;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        b = img_argb[i].ch.red | img_argb[i].ch.green | img_argb[i].ch.blue;
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = b > 128 ? 1 : 0;
    }
#endif
}

static int _stabi_io_custom_read(void *user,char *data,int size)
{
    int read_len = 0;
    TUYA_FILE fd = (TUYA_FILE)user;

    read_len = ty_gui_fread(data, size, fd);
    if(read_len < 0)
        read_len = 0;
    return read_len;
}

static void _stabi_io_custom_skip(void *user,int n)
{
    TUYA_FILE fd = (TUYA_FILE)user;
    ty_gui_fseek(fd, n, LV_FS_SEEK_CUR);
}

static int _stabi_io_custom_eof(void *user)
{
    TUYA_FILE fd = (TUYA_FILE)user;
    return (ty_gui_feof(fd)!=0)?1:0;
}
#elif defined(PNG_DECODE_WITH_LIBSPNG)
#if LVGL_VERSION_MAJOR < 9
static void spng_convert_color_depth(uint8_t * img, uint32_t px_cnt, size_t *len)
{
#if LV_COLOR_DEPTH == 32
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    lv_color_t * img_c = (lv_color_t *) img;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img_c[i].ch.red = c.ch.blue;
        img_c[i].ch.blue = c.ch.red;
    }
#elif LV_COLOR_DEPTH == 16
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.blue, img_argb[i].ch.green, img_argb[i].ch.red);
        img[i * 3 + 2] = img_argb[i].ch.alpha;
    #if defined(LV_COLOR_16_SWAP) && (LV_COLOR_16_SWAP == 1)
        img[i * 3 + 0] = c.full >> 8;
        img[i * 3 + 1] = c.full & 0xFF;
    #else
        img[i * 3 + 1] = c.full >> 8;
        img[i * 3 + 0] = c.full & 0xFF;
    #endif
    }
#elif LV_COLOR_DEPTH == 8
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = c.full;
    }
#elif LV_COLOR_DEPTH == 1
    lv_color32_t * img_argb = (lv_color32_t *)img;
    uint8_t b;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        b = img_argb[i].ch.red | img_argb[i].ch.green | img_argb[i].ch.blue;
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = b > 128 ? 1 : 0;
    }
#endif
}
#else
static void spng_convert_color_depth(uint8_t * img_p, uint32_t px_cnt, size_t *len)
{
#if LV_COLOR_DEPTH == 16                                    //转换为:LV_COLOR_FORMAT_RGB565A8
    lv_color32_t * img_argb = (lv_color32_t *)img_p;
    size_t rgb_size = px_cnt * 2;
    size_t alpha_size = px_cnt;
    lv_color16_t *rgb_data = (lv_color16_t*)img_p;
    uint8_t *alpha_data = tkl_system_psram_malloc(alpha_size);
    uint32_t i;

    if (alpha_data == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] Failed to allocate memory for alpha_data ?\r\n", __FUNCTION__, __LINE__);
        return;
    }
    for (i = 0; i < px_cnt; i++) {
        uint8_t r = img_argb[i].red;
        uint8_t g = img_argb[i].green;
        uint8_t b = img_argb[i].blue;
        uint8_t a = img_argb[i].alpha;
        rgb_data[i].blue = r >> 3;
        rgb_data[i].green = g >> 2;
        rgb_data[i].red = b >> 3;
    #if defined(LV_COLOR_16_SWAP) && (LV_COLOR_16_SWAP == 1)
        uint16_t *_rgb_data = (uint16_t *)rgb_data;
        uint16_t pixel = _rgb_data[i];
        pixel = (pixel >> 8) | (pixel << 8);
        _rgb_data[i] = pixel;
    #endif
        alpha_data[i] = a;
    }
    memcpy((void *)(img_p + rgb_size), (void *)alpha_data, alpha_size);
    tkl_system_psram_free((void *)alpha_data);
    *len = rgb_size + alpha_size;
#elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
    lv_color32_t * img_argb = (lv_color32_t *)img_p;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        uint8_t blue = img_argb[i].blue;
        img_argb[i].blue = img_argb[i].red;
        img_argb[i].red = blue;
    }
#else
    #error "unknown color depth"
#endif
}
#endif
#endif

void jpg_convert_color_depth(unsigned char * img_p, unsigned int px_cnt)
{
#if (LV_COLOR_DEPTH == 24) || (LV_COLOR_DEPTH == 32)            //24位色深时,bk底层jpg解码只支持RGB的顺序,需转换成LVGL的BGR顺序!
    lv_color_t * img_argb = (lv_color_t *)img_p;
    unsigned int i;
    for(i = 0; i < px_cnt; i++) {
        unsigned char blue = img_argb[i].blue;
        img_argb[i].blue = img_argb[i].red;
        img_argb[i].red = blue;
    }
#endif
}

void img_byte_order_swap(unsigned char * img_p, unsigned int px_cnt)
{
#if (LV_COLOR_DEPTH == 16)
    #if defined(LV_COLOR_16_SWAP) && (LV_COLOR_16_SWAP == 1)
        TY_GUI_LOG_PRINT("[%s][%d] start pic img byte order swap !\r\n", __FUNCTION__, __LINE__);
        for(int i = 0; i < px_cnt; i++) {
            uint16_t *pixel = (uint16_t*)(img_p + i * 2);
            *pixel = (*pixel >> 8) | (*pixel << 8);
        }
    #endif
#endif
}

#if !(defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1))
#ifdef ENABLE_TUYA_TKL_JPEG_DECODE
static OPERATE_RET jpg_dec_img_start(gui_img_frame_buffer_t *jpeg_frame, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    int flag = 0;
    TKL_JPEG_CODEC_INFO_T jpeg_info = { 0 };
    JPEG_DEC_OUT_FMT out_fmt = JPEG_DEC_OUT_RGB565;
    unsigned char bytes_per_pixel = 2;
#if LV_COLOR_DEPTH == 16
    bytes_per_pixel = 2;
    out_fmt = JPEG_DEC_OUT_RGB565;
#elif LV_COLOR_DEPTH == 24
    bytes_per_pixel = 3;
    out_fmt = JPEG_DEC_OUT_RGB888;
#elif LV_COLOR_DEPTH == 32
    bytes_per_pixel = 3;
    out_fmt = JPEG_DEC_OUT_RGB888;
#else
    #error "unknown color depth"
#endif

    tkl_jpeg_codec_init();

    do {
        ret = tkl_jpeg_codec_img_info_get(jpeg_frame->frame, jpeg_frame->length, &jpeg_info);
        if (ret != OPRT_OK) {
            TY_GUI_LOG_PRINT("[%s][%d] img info get fail ??? \r\n", __FUNCTION__, __LINE__);
            break;
        }
        img_dst->header.w = jpeg_info.out_width;
        img_dst->header.h = jpeg_info.out_height;
        img_dst->data_size = img_dst->header.w*img_dst->header.h*bytes_per_pixel;
        if(img_dst->data == NULL) {
            img_dst->data = tkl_system_psram_malloc(img_dst->data_size);
            if(!img_dst->data) {
                TY_GUI_LOG_PRINT("[%s][%d] malloc psram size %d fail\r\n", __FUNCTION__, __LINE__, img_dst->data_size);
                ret = OPRT_MALLOC_FAILED;
                break;
            }
            flag = 1;
        }
        ret = tkl_jpeg_codec_convert((UINT8_T *)jpeg_frame->frame, (UINT8_T *)img_dst->data, &jpeg_info, out_fmt);
        if (ret != OPRT_OK) {
            TY_GUI_LOG_PRINT("[%s][%d] jpg sw decoder error\r\n", __FUNCTION__, __LINE__);
            if(flag) {
                tkl_system_psram_free((void *)img_dst->data);
                img_dst->data = NULL;
            }
        }
        else {
        #if LVGL_VERSION_MAJOR < 9
            img_dst->header.always_zero = 0;
            img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
        #else
            jpg_convert_color_depth((unsigned char *)img_dst->data, img_dst->data_size/bytes_per_pixel);
            img_dst->header.magic= LV_IMAGE_HEADER_MAGIC;
            img_dst->header.stride = img_dst->header.w*bytes_per_pixel;
            #if LV_COLOR_DEPTH == 32
            img_dst->header.cf = LV_COLOR_FORMAT_RGB888;        //最高支持RGB888
            #else
            img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
            #endif
        #endif
        }
    }while(0);

    tkl_jpeg_codec_deinit();
    TY_GUI_LOG_PRINT("[%s][%d] jpg img decode %s ! \r\n", __FUNCTION__, __LINE__, (ret == OPRT_OK)?"successful":"fail");
    return ret;
}

#else
static OPERATE_RET jpg_dec_img_start(gui_img_frame_buffer_t *jpeg_frame, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    sw_jpeg_dec_res_t result;
    int flag = 0;
    unsigned char bytes_per_pixel = 2;
#if LV_COLOR_DEPTH == 16
    bytes_per_pixel = 2;
#elif LV_COLOR_DEPTH == 24
    bytes_per_pixel = 3;
#elif LV_COLOR_DEPTH == 32
    bytes_per_pixel = 3;
#else
    #error "unknown color depth"
#endif

    do{
        #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        ret = bk_jpeg_get_img_info(jpeg_frame->length, jpeg_frame->frame, &result, NULL);
        #else
        ret = bk_jpeg_get_img_info(jpeg_frame->length, jpeg_frame->frame, &result);
        #endif
        if (ret != OPRT_OK)
        {
            TY_GUI_LOG_PRINT("[%s][%d] get img info fail:%d\r\n", __FUNCTION__, __LINE__, ret);
            ret = OPRT_COM_ERROR;
            break;
        }
    #if LV_COLOR_DEPTH == 16                                //16位色深:像素宽度要求偶数!
        if ((result.pixel_x%2) != 0) {
            TY_GUI_LOG_PRINT("[%s][%d] 16 color depth can't support sw decode Odd width pixels jpg file [%d*%d]???\r\n", __FUNCTION__, __LINE__, 
                result.pixel_x, result.pixel_y);
            ret = OPRT_COM_ERROR;
            break;
        }
    #elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32      //24位/32位色深:像素宽度要求4的倍数!
        if ((result.pixel_x & 3) != 0) {
            TY_GUI_LOG_PRINT("[%s][%d] %d color depth can't support sw decode not 4x's width pixels jpg file [%d*%d]???\r\n", __FUNCTION__, __LINE__, 
                LV_COLOR_DEPTH, result.pixel_x, result.pixel_y);
            ret = OPRT_COM_ERROR;
            break;
        }
    #endif
        img_dst->header.w = result.pixel_x;
        img_dst->header.h = result.pixel_y;
        img_dst->data_size = img_dst->header.w*img_dst->header.h*bytes_per_pixel;
        if(img_dst->data == NULL)
        {
            img_dst->data = tkl_system_psram_malloc(img_dst->data_size);
            if(!img_dst->data)
            {
                TY_GUI_LOG_PRINT("[%s][%d] malloc psram size %d fail\r\n", __FUNCTION__, __LINE__, img_dst->data_size);
                ret = OPRT_MALLOC_FAILED;
                break;
            }

            flag = 1;
        }

    #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        JD_FORMAT_OUTPUT output_format = JD_FORMAT_RGB565;
        #if LV_COLOR_DEPTH == 16
            output_format = JD_FORMAT_RGB565;
        #elif LV_COLOR_DEPTH == 24
            output_format = JD_FORMAT_RGB888;
        #elif LV_COLOR_DEPTH == 32
            output_format = JD_FORMAT_RGB888;
        #endif
        ret = bk_jpeg_dec_sw_start_one_time(JPEGDEC_BY_FRAME, jpeg_frame->frame, (uint8_t *)img_dst->data, jpeg_frame->length, img_dst->data_size, (sw_jpeg_dec_res_t *)&result, 0, output_format, 0, NULL, NULL);
        //ret = bk_jpeg_dec_sw_start_by_handle(jpeg_dec_handle, JPEGDEC_BY_FRAME, jpeg_frame->frame, (uint8_t *)img_dst->data, jpeg_frame->length, img_dst->data_size, (sw_jpeg_dec_res_t *)&result);
    #else
        ret = bk_jpeg_dec_sw_start(JPEGDEC_BY_FRAME, jpeg_frame->frame, (uint8_t *)img_dst->data, jpeg_frame->length, img_dst->data_size, (sw_jpeg_dec_res_t *)&result);
    #endif
        if (ret != OPRT_OK)
        {
            TY_GUI_LOG_PRINT("[%s][%d] sw decoder error\r\n", __FUNCTION__, __LINE__);
            if(flag)
            {
                tkl_system_psram_free((void *)img_dst->data);
                img_dst->data = NULL;
            }
            break;
        }

    #if LVGL_VERSION_MAJOR < 9
        img_dst->header.always_zero = 0;
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
    #else
        jpg_convert_color_depth((unsigned char *)img_dst->data, img_dst->data_size/bytes_per_pixel);
        img_dst->header.magic= LV_IMAGE_HEADER_MAGIC;
        img_dst->header.stride = img_dst->header.w*bytes_per_pixel;
        #if LV_COLOR_DEPTH == 32
            img_dst->header.cf = LV_COLOR_FORMAT_RGB888;        //最高支持RGB888
        #else
            img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
        #endif
    #endif
    }while(0);

    return ret;
}
#endif
#endif

static OPERATE_RET _read_file_to_mem(TUYA_FILE fd, char *filename, int filelen, unsigned int* paddr)
{
    unsigned char *sram_addr = NULL;
    //uint32 once_read_len = 1024*2;
    unsigned int once_read_len = filelen;
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    int fd = -1;
#else
    //TUYA_FILE fd = NULL;
#endif
    int read_len = 0;
    OPERATE_RET ret = OPRT_COM_ERROR;

    do{
    #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        fd = open(filename, O_RDONLY);
        if(fd < 0)
    #else
        //fd = ty_gui_fopen(filename, "r");
        if(fd == NULL)
    #endif
        {
            TY_GUI_LOG_PRINT("[%s][%d] open fail:%s\r\n", __FUNCTION__, __LINE__, filename);
            ret = OPRT_COM_ERROR;
            break;
        }

        sram_addr = tkl_system_psram_malloc(once_read_len);
        if (sram_addr == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }

        while(1)
        {
        #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
            read_len = read(fd, sram_addr, once_read_len);
        #else
            read_len = ty_gui_fread(sram_addr, once_read_len, fd);
        #endif
            if(read_len < 0)
            {
                TY_GUI_LOG_PRINT("[%s][%d] read file fail.\r\n", __FUNCTION__, __LINE__);
                ret= OPRT_COM_ERROR;
                break;
            }
            
            if (read_len == 0)
            {
                //TY_GUI_LOG_PRINT("[%s][%d] read file complete.\r\n", __FUNCTION__, __LINE__);
                ret = OPRT_OK;
                break;
            }
            
            if(once_read_len != read_len)
            {
                if (read_len % 4)
                {
                    read_len = (read_len / 4 + 1) * 4;
                }
                ty_psram_word_memcpy(paddr, sram_addr, read_len);
            }
            else
            {
                ty_psram_word_memcpy(paddr, sram_addr, once_read_len);
                paddr += (once_read_len/4);
            }
        }
    }while(0);

    if(sram_addr)
    {
        tkl_system_psram_free(sram_addr);
    }

#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    if(fd > 0)
    {
        close(fd);
    }
#else
    //if (fd != NULL)
    //    ty_gui_fclose(fd);
#endif
    return ret;
}

#ifndef PNG_IMG_DECODE_IN_MEMORY
static INT_T _read_filelen(char *filename)
{
    INT_T ret = OPRT_COM_ERROR;
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    struct stat statbuf;
    
    do{
        if(!filename)
        {
            TY_GUI_LOG_PRINT("[%s][%d]param is null.\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }
        
        ret = stat(filename, &statbuf);
        if(OPRT_OK != ret)
        {
            TY_GUI_LOG_PRINT("[%s][%d] sta fail:%s\r\n", __FUNCTION__, __LINE__, filename);
            break;
        }

        ret = statbuf.st_size;
        TY_GUI_LOG_PRINT("[%s][%d] %s size:%d\r\n", __FUNCTION__, __LINE__, filename, ret);
    }while(0);
#else
    if(filename != NULL)
        ret = ty_gui_fgetsize(filename);
    else {
        TY_GUI_LOG_PRINT("[%s][%d]param is null.\r\n", __FUNCTION__, __LINE__);
        ret = OPRT_COM_ERROR;
    }
#endif
    return ret;
}
#endif

gui_img_frame_buffer_t *img_read_file(char *file_name)
{
    gui_img_frame_buffer_t *jpeg_frame = NULL;
    int file_len = 0;
    OPERATE_RET ret = OPRT_OK;
    TUYA_FILE fd = NULL;

    do{
        fd = ty_gui_fopen(file_name, "r");
        if(fd == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] %s open fail ?\r\n", __FUNCTION__, __LINE__, file_name);
            break;
        }
        ty_gui_fseek(fd, 0, GUI_FS_SEEK_END);
        file_len = (int)ty_gui_ftell(fd);
        ty_gui_fseek(fd, 0, GUI_FS_SEEK_SET);
        //file_len = _read_filelen(file_name);
        if(file_len <= 0)
        {
            TY_GUI_LOG_PRINT("[%s][%d] %s don't exit in fatfs\r\n", __FUNCTION__, __LINE__, file_name);
            break;
        }

        jpeg_frame = tkl_system_malloc(sizeof(gui_img_frame_buffer_t));
        if(!jpeg_frame)
        {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            break;
        }

        memset(jpeg_frame, 0, sizeof(gui_img_frame_buffer_t));
        jpeg_frame->frame = tkl_system_psram_malloc(file_len);
        jpeg_frame->length = file_len;
        if(!jpeg_frame->frame)
        {
            tkl_system_free(jpeg_frame);
            jpeg_frame = NULL;
            TY_GUI_LOG_PRINT("[%s][%d] psram malloc fail\r\n", __FUNCTION__, __LINE__);
            break;
        }

        ret = _read_file_to_mem(fd, (char *)file_name, file_len, (unsigned int *)jpeg_frame->frame);
        if(OPRT_OK != ret)
        {
            tkl_system_psram_free(jpeg_frame->frame);
            jpeg_frame->frame = NULL;

            tkl_system_free(jpeg_frame);
            jpeg_frame = NULL;
        }
    }while(0);

    if (fd != NULL)
        ty_gui_fclose(fd);
    return jpeg_frame;
}

gui_img_frame_buffer_t *img_read_raw_data(unsigned char *img_data, unsigned int img_size)
{
    gui_img_frame_buffer_t *jpeg_frame = NULL;

    do{
        if(img_size <= 0)
        {
            TY_GUI_LOG_PRINT("[%s][%d] don't exit in fatfs\r\n", __FUNCTION__, __LINE__);
            break;
        }

        jpeg_frame = tkl_system_malloc(sizeof(gui_img_frame_buffer_t));
        if(!jpeg_frame)
        {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            break;
        }
        jpeg_frame->frame = img_data;
        jpeg_frame->length = img_size;
    }while(0);

    return jpeg_frame;
}

#if !(defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1))
static OPERATE_RET jpg_dec_img(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    gui_img_frame_buffer_t *jpeg_frame = NULL;
    unsigned long long int last_run_ms = 0, curr_run_ms = 0;

#if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
    #if 0
        ret = bk_jpeg_dec_sw_init_by_handle(&jpeg_dec_handle, NULL, 0);
        if (ret != OPRT_OK) {
            TY_GUI_LOG_PRINT("[%s][%d] jpeg sw init handle fail\r\n", __FUNCTION__, __LINE__);
            jpeg_dec_handle = NULL;
            return ret;
        }
        #if LV_COLOR_DEPTH == 16
            jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_RGB565);
        #elif LV_COLOR_DEPTH == 24
            jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_RGB888);
        #elif LV_COLOR_DEPTH == 32
            jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_RGB888);
        #else
            #error "unknown color depth"
        #endif
    #endif
#else
    bk_jpeg_dec_sw_init(NULL, 0);
    #if LV_COLOR_DEPTH == 16
        jd_set_format(JD_FORMAT_RGB565);
    #elif LV_COLOR_DEPTH == 24
        jd_set_format(JD_FORMAT_RGB888);
    #elif LV_COLOR_DEPTH == 32
        jd_set_format(JD_FORMAT_RGB888);
    #else
        #error "unknown color depth"
    #endif
#endif

    do{
        if (is_file) {
            #ifdef IMG_DECODING_TIME_TEST
            last_run_ms = __current_timestamp();
            #endif
            jpeg_frame = img_read_file(file_name);
            #ifdef IMG_DECODING_TIME_TEST
            curr_run_ms = __current_timestamp();
            #endif
            TY_GUI_LOG_PRINT("[%s][%d] file '%s' get use '%llu'ms\r\n",__FUNCTION__, __LINE__, file_name,
                curr_run_ms-last_run_ms);
        }
        else
            jpeg_frame = img_read_raw_data(img_data, img_size);
        if (jpeg_frame == NULL)
        {
            ret = OPRT_COM_ERROR;
            break;
        }
        #ifdef IMG_DECODING_TIME_TEST
        last_run_ms = __current_timestamp();
        #endif
        ret = jpg_dec_img_start(jpeg_frame, img_dst);
        #ifdef IMG_DECODING_TIME_TEST
        curr_run_ms = __current_timestamp();
        #endif
        if(OPRT_OK == ret)
        {
            img_byte_order_swap((unsigned char *)img_dst->data, img_dst->header.w * img_dst->header.h);
            TY_GUI_LOG_PRINT("[%s][%d] sw decode success, width:%d, height:%d, size:%d, decode use time '%llu'ms\r\n", 
                            __FUNCTION__, __LINE__,
                            img_dst->header.w, img_dst->header.h, img_dst->data_size, curr_run_ms-last_run_ms);
        }
    }while(0);

    if (jpeg_frame)
    {
        if(jpeg_frame->frame && is_file)
        {
            tkl_system_psram_free(jpeg_frame->frame);
            jpeg_frame->frame = NULL;
        }
        
        tkl_system_free(jpeg_frame);
        jpeg_frame = NULL;
    }

#if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
    #if 0
        jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_VYUY);
        bk_jpeg_dec_sw_deinit_by_handle(jpeg_dec_handle);
        jpeg_dec_handle = NULL;
    #endif
#else
    jd_set_format(JD_FORMAT_VYUY);
    bk_jpeg_dec_sw_deinit();
#endif

    return ret;
}
#endif

/*
jpg硬件解码有以下限制：
1.只能解码yuv422格式，常用图片都是yuv420格式，需要先使用工具转换成yuv422格式(参考\project_resource_sample\jpgconvert\readme.txt进行格式转换)
2.仅支持像素宽度为32的倍数, 像素高度为8的倍数的图片
*/
#ifdef ENABLE_JPEG_IMAGE_DECODE_WITH_HW
static OPERATE_RET jpeg_hw_decode(gui_img_frame_buffer_t *_jpeg_frame, lv_img_dsc_t *_img_dst)
{
    //TODO...jpg hw decode移至tkl层
    return OPRT_COM_ERROR;
}

static OPERATE_RET _dec_img_file_with_hw(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    gui_img_frame_buffer_t *jpeg_frame = NULL;

    do{
        if (is_file)
            jpeg_frame = img_read_file(file_name);
        else
            jpeg_frame = img_read_raw_data(img_data, img_size);
        if (jpeg_frame == NULL)
        {
            ret = OPRT_COM_ERROR;
            break;
        }

        //通过软解获取图片信息
        sw_jpeg_dec_res_t result;
        //bk_jpeg_dec_sw_init_compatible();
    #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        bk_jpeg_dec_sw_init_by_handle(&jpeg_dec_handle, NULL, 0);
    #else
        bk_jpeg_dec_sw_init(NULL, 0);
    #endif

    #if LV_COLOR_DEPTH == 16
        #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_RGB565);
        #else
        jd_set_format(JD_FORMAT_RGB565);
        #endif
    #elif LV_COLOR_DEPTH == 24
        #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_RGB888);
        #else
        jd_set_format(JD_FORMAT_RGB888);
        #endif
    #elif LV_COLOR_DEPTH == 32
        #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_RGB888);
        #else
        jd_set_format(JD_FORMAT_RGB888);
        #endif
    #else
        #error "unknown color depth"
    #endif
        #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        ret = bk_jpeg_get_img_info(jpeg_frame->length, jpeg_frame->frame, &result, NULL);
        #else
        ret = bk_jpeg_get_img_info(jpeg_frame->length, jpeg_frame->frame, &result);
        #endif
        if (ret != OPRT_OK)
        {
            TY_GUI_LOG_PRINT("[%s][%d] get img info fail:%d\r\n", __FUNCTION__, __LINE__, ret);
            ret = OPRT_COM_ERROR;
            break;
        }
        if ((result.pixel_x%32) != 0 || (result.pixel_y%8) != 0) {      //硬解码仅支持:像素宽度为32的倍数, 像素高度为8的倍数!
            TY_GUI_LOG_PRINT("[%s][%d] can't support hw decode pixels jpg file [%d*%d]???\r\n", __FUNCTION__, __LINE__, 
                result.pixel_x, result.pixel_y);
            ret = OPRT_COM_ERROR;
            break;
        }

        img_dst->header.w = result.pixel_x;
        img_dst->header.h = result.pixel_y;
        img_dst->data_size = img_dst->header.w*img_dst->header.h*2;
        #if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
        jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_VYUY);
        bk_jpeg_dec_sw_deinit_by_handle(jpeg_dec_handle);
        jpeg_dec_handle = NULL;
        #else
        jd_set_format(JD_FORMAT_VYUY);
        bk_jpeg_dec_sw_deinit();
        #endif
        
        ret = jpeg_hw_decode(jpeg_frame, img_dst);
        if(OPRT_OK == ret)
        {
            TY_GUI_LOG_PRINT("[%s][%d] hw decode success, width:%d, height:%d, size:%d\r\n", 
                            __FUNCTION__, __LINE__,
                            img_dst->header.w, img_dst->header.h, img_dst->data_size);
        }
    }while(0);

#if defined(TUYA_CPU_ARCH_SMP) && (TUYA_CPU_ARCH_SMP == 1)
    if (jpeg_dec_handle != NULL) {
        jd_set_format_by_handle(jpeg_dec_handle, JD_FORMAT_VYUY);
        bk_jpeg_dec_sw_deinit_by_handle(jpeg_dec_handle);
        jpeg_dec_handle = NULL;
     }
#endif

    if (jpeg_frame)
    {
        if(jpeg_frame->frame && is_file)
        {
            tkl_system_psram_free(jpeg_frame->frame);
            jpeg_frame->frame = NULL;
        }
        
        tkl_system_free(jpeg_frame);
        jpeg_frame = NULL;
    }

    return ret;
}
#else
static OPERATE_RET _dec_img_file_with_hw(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst)
{
    TY_GUI_LOG_PRINT("[%s][%d] unsupport jpg hw decode ???\r\n", __func__, __LINE__);
    return OPRT_COM_ERROR;
}
#endif

/**
 图像文件加载: 硬解码jpg格式
 * @brief load jpg file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
 */
static OPERATE_RET jpg_img_load_with_hw_dec(char *filename, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    
    do{
        if(!filename || !img_dst)
        {
            ret = OPRT_INVALID_PARM;
            break;
        }

        ret = _dec_img_file_with_hw(true, filename, NULL, 0, img_dst);
    }while(0);

    return ret;
}

/**
 图像文件加载: 解码jpg格式
 * @brief load jpg file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @param[in] jpg_hw_dec: use hw or sw jpg decode
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
 */
OPERATE_RET jpg_img_load(char *filename, lv_img_dsc_t *img_dst, bool jpg_hw_dec)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    bool jpg_hw_dec_on = false;

#ifdef ENABLE_JPEG_IMAGE_DECODE_WITH_HW
    jpg_hw_dec_on = jpg_hw_dec;
#endif

    if (jpg_hw_dec_on)
        ret = jpg_img_load_with_hw_dec(filename, img_dst);
    else {
        do{
            if(!filename || !img_dst)
            {
                ret = OPRT_COM_ERROR;
                break;
            }
            ret = jpg_dec_img(true, filename, NULL, 0, img_dst);
        }while(0);
    }
    return ret;
}

/**
 图像文件加载: 支持带缩放的解码jpg文件格式
 * @brief load jpg file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @param[in] target_width: target output width.
 * @param[in] target_height: target output height.
 * @retval  OPRT_OK:success
 */
#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
OPERATE_RET jpg_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;

    do{
        if(!filename || !img_dst)
        {
            ret = OPRT_COM_ERROR;
            break;
        }
        ret = jpg_dec_img_with_scale(true, filename, NULL, 0, img_dst, target_width, target_height);
    }while(0);
 
    return ret;
}
#endif

/*
 图像文件卸载: jpg格式
*/
void jpg_img_unload(lv_img_dsc_t *img_dst)
{
    if (img_dst->data != NULL)
        tkl_system_psram_free((void *)img_dst->data);
    img_dst->data = NULL;
}

static OPERATE_RET __raw_png_img_load_with_scale(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height);

/*
 图像文件加载: 支持缩放png文件格式解码
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
*/
STATIC OPERATE_RET __png_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;

#ifdef PNG_IMG_DECODE_IN_MEMORY
    unsigned long long int last_run_ms = 0, curr_run_ms = 0;

    #ifdef IMG_DECODING_TIME_TEST
    last_run_ms = __current_timestamp();
    #endif
    gui_img_frame_buffer_t *jpeg_frame = img_read_file(filename);
    #ifdef IMG_DECODING_TIME_TEST
    curr_run_ms = __current_timestamp();
    #endif
    TY_GUI_LOG_PRINT("[%s][%d] file '%s' get use '%llu'ms\r\n",__FUNCTION__, __LINE__, filename,
        curr_run_ms-last_run_ms);

    if (jpeg_frame == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d]read file '%s' fail ?\r\n", __FUNCTION__, __LINE__, filename);
    }
    else {
        #ifdef IMG_DECODING_TIME_TEST
        last_run_ms = __current_timestamp();
        #endif
        if (target_width == 0 && target_height == 0) {
            ret = raw_img_load(image_type_png, jpeg_frame->frame, jpeg_frame->length, img_dst, false);
        }
        else {
            ret = __raw_png_img_load_with_scale(jpeg_frame->frame, jpeg_frame->length, img_dst, target_width, target_height);
        }
        #ifdef IMG_DECODING_TIME_TEST
        curr_run_ms = __current_timestamp();
        #endif
        tkl_system_psram_free(jpeg_frame->frame);
        jpeg_frame->frame = NULL;
        tkl_system_free(jpeg_frame);
        jpeg_frame = NULL;
        TY_GUI_LOG_PRINT("[%s][%d] file '%s' decode use '%llu'ms\r\n",__FUNCTION__, __LINE__, filename,
            curr_run_ms-last_run_ms);
    }
#else
    
    if(!filename || !img_dst){
        ret = OPRT_COM_ERROR;
        TY_GUI_LOG_PRINT("[%s][%d]param invalid\r\n", __FUNCTION__, __LINE__);
        return ret;
    }
    #if defined(PNG_DECODE_WITH_STB_IMAGE)
        int channels = 0, png_width = 0, png_height = 0;
        uint8_t * data = NULL;
        TUYA_FILE fd = NULL;
        stbi_io_callbacks callbacks = {
            .read = _stabi_io_custom_read,
            .skip = _stabi_io_custom_skip,
            .eof = _stabi_io_custom_eof
        };
        fd = ty_gui_fopen(filename, "r");
        if(fd == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] stb image decode open png file '%s' fail ???\r\n", __FUNCTION__, __LINE__, filename);
            return ret;
        }
    #if LVGL_VERSION_MAJOR < 9
        img_dst->header.always_zero = 0;
    #else
        img_dst->header.magic = LV_IMAGE_HEADER_MAGIC;
    #endif
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        data = (uint8_t *)stbi_load_from_callbacks(&callbacks, (void *)fd, (int *)&png_width, (int *)&png_height, &channels, 0);
        ty_gui_fclose(fd);
        if (data != NULL) {
            stabi_convert_color_depth(data,  png_width * png_height);
            img_dst->data = data;
            img_dst->data_size = _read_filelen(filename);
            img_dst->header.w = png_width;
            img_dst->header.h = png_height;
            ret = OPRT_OK;
        }
        else {
            TY_GUI_LOG_PRINT("[%s][%d] stb image decode png fail [reason '%s']???\r\n", __FUNCTION__, __LINE__, (stbi_failure_reason()!=NULL)?stbi_failure_reason():"unknown");
            return ret;
        }
    #elif defined(PNG_DECODE_WITH_LIBSPNG)
        //TODO...
    #else
        lv_img_decoder_dsc_t img_decoder_dsc;
        memset((char *)&img_decoder_dsc, 0, sizeof(img_decoder_dsc));
        img_decoder_dsc.src_type = LV_IMG_SRC_FILE;
        ret = lv_img_decoder_open(&img_decoder_dsc, filename, img_decoder_dsc.color, img_decoder_dsc.frame_id);
        if(ret != LV_RES_OK) {
            TY_GUI_LOG_PRINT("[%s][%d] decode fail:%d\r\n", __FUNCTION__, __LINE__, ret);
            ret = OPRT_COM_ERROR;
            return ret;
        }

        memcpy(&img_dst->header, &img_decoder_dsc.header, sizeof(lv_img_header_t));
        img_dst->data_size = _read_filelen(filename);
        img_dst->data = img_decoder_dsc.img_data;
        lv_mem_free((void *)img_decoder_dsc.src);
        ret = OPRT_OK;
    #endif
#endif
    return ret;
}

/*
 图像文件加载: 支持缩放的png文件格式解码
 * @brief load png file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
*/
OPERATE_RET png_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    return __png_img_load_with_scale(filename, img_dst, target_width, target_height);
}

/*
 图像文件加载: png格式
 * @brief load png file from sd card, and store data in img_dst
 * @param[in] filename: only filename with path
 * @param[in] img_dst: if the data of img_dst is NULL, will use memory of psram malloc, so when you don't use, should free this ram
 * @retval  OPRT_OK:success
 * @retval  <0: decode fail or file don't exit in sd
*/
OPERATE_RET png_img_load(char *filename, lv_img_dsc_t *img_dst)
{
    return __png_img_load_with_scale(filename, img_dst, 0, 0);
}

/*
 图像文件卸载: png格式
*/
void png_img_unload(lv_img_dsc_t *img_dst)
{
    if (img_dst->data != NULL) {
        #if defined(PNG_DECODE_WITH_STB_IMAGE)
        stbi_image_free((void *)img_dst->data);
        #elif defined(PNG_DECODE_WITH_LIBSPNG)
        tkl_system_psram_free((void *)img_dst->data);
        #else
        lv_mem_free((void *)img_dst->data);
        #endif
    }
    img_dst->data = NULL;
}

/*
 jpg/jpeg/png等图像文件加载: 通过图像id
*/
OPERATE_RET img_file_load_by_id(uint32_t img_file_id, lv_img_dsc_t *img_dst, bool jpg_hw_dec)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    BOOL_T is_exist = false;
    char file_name[32];
    const char *fmt_array[6] = {"png", "jpg", "jpeg", "PNG", "JPG", "JPEG"};
    bool jpg_hw_dec_on = false;

#ifdef ENABLE_JPEG_IMAGE_DECODE_WITH_HW
    jpg_hw_dec_on = jpg_hw_dec;
#endif

    for (uint32_t i=0; i<6; i++) {
        memset(file_name, 0, sizeof(file_name));
        snprintf(file_name, sizeof(file_name), "%lu.%s", img_file_id, fmt_array[i]);
        ty_gui_fs_is_exist(tuya_app_gui_get_picture_full_path(file_name), &is_exist);
        if (is_exist)
            break;
    }
    if (is_exist) {
        GUI_IMAGE_TYPE_E type = get_image_type(file_name);

        TY_GUI_LOG_PRINT("[%s][%d] find img file '%s' by id '%lu'\r\n", __FUNCTION__, __LINE__, file_name, img_file_id);
        if (type == image_type_jpg || type == image_type_jpeg)
            ret = jpg_img_load(tuya_app_gui_get_picture_full_path(file_name), img_dst, jpg_hw_dec_on);
        else if (type == image_type_png)
            ret = png_img_load(tuya_app_gui_get_picture_full_path(file_name), img_dst);
    }
    return ret;
}

/*
 图像文件卸载:
*/
void img_file_unload(lv_img_dsc_t *img_dst)
{
    if (img_dst->data != NULL) {
        if (img_dst->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {     //png image
            #if defined(PNG_DECODE_WITH_STB_IMAGE)
            stbi_image_free((void *)img_dst->data);
            #elif defined(PNG_DECODE_WITH_LIBSPNG)
            tkl_system_psram_free((void *)img_dst->data);
            #else
            lv_mem_free((void *)img_dst->data);
            #endif
        }
        else
            tkl_system_psram_free((void *)img_dst->data);
    }
    img_dst->data = NULL;
}

static OPERATE_RET raw_jpg_img_load_with_hw_dec(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    
    do{
        if(!img_dst)
        {
            ret = OPRT_COM_ERROR;
            break;
        }

        ret = _dec_img_file_with_hw(false, NULL, img_data, img_size, img_dst);
    }while(0);

    return ret;
}

static OPERATE_RET raw_jpg_img_load(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    
    do{
        if(!img_dst)
        {
            ret = OPRT_COM_ERROR;
            break;
        }
        ret = jpg_dec_img(false, NULL, img_data, img_size, img_dst);
    }while(0);

    return ret;
}

#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
OPERATE_RET raw_jpg_img_load_with_scale(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    
    do{
        if(!img_dst)
        {
            ret = OPRT_COM_ERROR;
            break;
        }
        ret = jpg_dec_img_with_scale(false, NULL, img_data, img_size, img_dst, target_width, target_height);
    }while(0);

    return ret;
}
#endif

static OPERATE_RET __raw_png_img_load_with_scale(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;

#if defined(PNG_DECODE_WITH_STB_IMAGE)
    int channels = 0, png_width = 0, png_height = 0;
    uint8_t * data = NULL;
#if LVGL_VERSION_MAJOR < 9
    img_dst->header.always_zero = 0;
#else
    img_dst->header.magic = LV_IMAGE_HEADER_MAGIC;
#endif
    img_dst->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    data = (uint8_t *)stbi_load_from_memory(img_data, img_size, &png_width, &png_height, &channels, 0);
    if (data != NULL) {
        stabi_convert_color_depth(data, png_width * png_height);
        img_dst->data = data;
        img_dst->data_size = img_size;
        img_dst->header.w = png_width;
        img_dst->header.h = png_height;
        ret = OPRT_OK;
    }
    else {
        TY_GUI_LOG_PRINT("[%s][%d] stb image decode png fail [reason '%s']???\r\n", __FUNCTION__, __LINE__, (stbi_failure_reason()!=NULL)?stbi_failure_reason():"unknown");
        return ret;
    }
#elif defined(PNG_DECODE_WITH_LIBSPNG)
    struct spng_ihdr ihdr;
    size_t image_size = 0;
    int fmt = SPNG_FMT_PNG;
    spng_ctx *ctx = spng_ctx_new(0);
    if (!ctx) {
        TY_GUI_LOG_PRINT("[%s][%d] Failed to create SPNG context ?\r\n", __FUNCTION__, __LINE__);
        return ret;
    }
    ret = spng_set_png_buffer(ctx, img_data, img_size);
    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s][%d] spng_set_png_buffer failed: %s ?\r\n", __FUNCTION__, __LINE__, spng_strerror(ret));
        spng_ctx_free(ctx);
        return ret;
    }
    ret = spng_get_ihdr(ctx, &ihdr);
    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s][%d] spng_get_ihdr failed: %s ?\r\n", __FUNCTION__, __LINE__, spng_strerror(ret));
        spng_ctx_free(ctx);
        return ret;
    }
    TY_GUI_LOG_PRINT("[%s][%d] png Image width: %u, height: %u, bit depth: %u, Color type: %u\r\n",  __FUNCTION__, __LINE__,
        ihdr.width, ihdr.height, ihdr.bit_depth, ihdr.color_type);
    #if 1
    fmt = SPNG_FMT_RGBA8;
    #else
    if(ihdr.color_type == SPNG_COLOR_TYPE_INDEXED) 
        fmt = SPNG_FMT_RGB8;
    else {
        switch (ihdr.color_type) {
            case SPNG_COLOR_TYPE_GRAYSCALE:
                if (ihdr.bit_depth == 16) {
                    fmt = SPNG_FMT_G8; // 16 位灰度
                } else {
                    fmt = SPNG_FMT_G8;  // 8 位灰度
                }
                break;
            case SPNG_COLOR_TYPE_GRAYSCALE_ALPHA:
                if (ihdr.bit_depth == 16) {
                    fmt = SPNG_FMT_GA16; // 16 位灰度 + Alpha
                } else {
                    fmt = SPNG_FMT_GA8;  // 8 位灰度 + Alpha
                }
                break;
            case SPNG_COLOR_TYPE_TRUECOLOR:
                if (ihdr.bit_depth == 16) {
                    fmt = SPNG_FMT_RGB8; // 48 位 RGB
                } else {
                    fmt = SPNG_FMT_RGB8;  // 24 位 RGB
                }
                break;
            case SPNG_COLOR_TYPE_TRUECOLOR_ALPHA:
                if (ihdr.bit_depth == 16) {
                    fmt = SPNG_FMT_RGBA16; // 64 位 RGBA
                } else {
                    fmt = SPNG_FMT_RGBA8;  // 32 位 RGBA
                }
                break;
            default:
                TY_GUI_LOG_PRINT("[%s][%d] Unsupported color type: %u ?\r\n", __FUNCTION__, __LINE__, ihdr.color_type);
                spng_ctx_free(ctx);
                ret = OPRT_COM_ERROR;
                return ret;
        }

    }
    #endif
    ret = spng_decoded_image_size(ctx, fmt, &image_size);
    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s][%d] spng_decoded_image_size failed: %s ?\r\n", __FUNCTION__, __LINE__, spng_strerror(ret));
        spng_ctx_free(ctx);
        return ret;
    }
    img_dst->data = tkl_system_psram_malloc(image_size);
    if (img_dst->data == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] Failed to allocate memory for image ?\r\n", __FUNCTION__, __LINE__);
        spng_ctx_free(ctx);
        ret = OPRT_COM_ERROR;
        return ret;
    }
    ret = spng_decode_image(ctx, (void *)img_dst->data, image_size, fmt, 0);
    if (ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("[%s][%d] spng_decode_image failed: %s ?\r\n", __FUNCTION__, __LINE__, spng_strerror(ret));
        tkl_system_psram_free((void *)img_dst->data);
        img_dst->data = NULL;
        spng_ctx_free(ctx);
        return ret;
    }
    spng_ctx_free(ctx);
#ifdef ENABLE_STB_IMG_SCALE
    if (target_width != 0 && target_width != ihdr.width && target_height != 0 && target_height != ihdr.height) {         //scaling start!
        double target_scale_x = (double)target_width / ihdr.width;
        double target_scale_y = (double)target_height / ihdr.height;
        double target_scale = fmin(target_scale_x, target_scale_y);             // 保持比例
        int cal_width = (int)round(target_scale*ihdr.width);
        int cal_height = (int)round(target_scale*ihdr.height);

        TY_GUI_LOG_PRINT("[%s][%d] png scaling start [w:%d->%d (desired '%d'), h:%d->%d (desired '%d')]\r\n", __FUNCTION__, __LINE__, 
            ihdr.width, cal_width, target_width, ihdr.height, cal_height, target_height);
        uint8_t* resized_data = (uint8_t *)tkl_system_psram_malloc(cal_width * cal_height * 4);
        if (resized_data == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] Failed to allocate memory for image ?\r\n", __FUNCTION__, __LINE__);
            tkl_system_psram_free((void *)img_dst->data);
            img_dst->data = NULL;
            ret = OPRT_COM_ERROR;
            return ret;
        }
        if (stbir_resize_uint8_linear((const unsigned char *)img_dst->data, ihdr.width, ihdr.height, 0, resized_data, cal_width, cal_height, 0, STBIR_RGBA) != NULL) {
            tkl_system_psram_free((void *)img_dst->data);
            img_dst->data = resized_data;
            ihdr.width = cal_width;
            ihdr.height = cal_height;
            image_size = cal_width * cal_height * 4;
        }
        else {
            TY_GUI_LOG_PRINT("[%s][%d] png scaling fail ?\r\n", __FUNCTION__, __LINE__);
            tkl_system_psram_free(resized_data);
            tkl_system_psram_free((void *)img_dst->data);
            img_dst->data = NULL;
            ret = OPRT_COM_ERROR;
            return ret;
        }
    }
#endif
    spng_convert_color_depth((uint8_t *)img_dst->data, ihdr.width * ihdr.height, &image_size);
    TY_GUI_LOG_PRINT("[%s][%d] spng_decode_image successful [real size %lu]!\r\n", __FUNCTION__, __LINE__, image_size);
#if LVGL_VERSION_MAJOR < 9
    img_dst->header.always_zero = 0;
    img_dst->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
#else
    img_dst->header.magic = LV_IMAGE_HEADER_MAGIC;
    #if LV_COLOR_DEPTH == 16
    img_dst->header.cf = LV_COLOR_FORMAT_RGB565A8;
    img_dst->header.stride = ihdr.width * 2;                //RGB部分的stride, alpha data不计.
    #elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
    img_dst->header.cf = LV_COLOR_FORMAT_ARGB8888;          //LVGL-v9所有色深位数按照此格式!
    img_dst->header.stride = ihdr.width * 4;
    #else
    #error "unknown color depth"
    #endif
#endif
    //img_dst->data_size = img_size;
    img_dst->data_size = image_size;
    img_dst->header.w = ihdr.width;
    img_dst->header.h = ihdr.height;
#else
    #if LVGL_VERSION_MAJOR < 9
    lv_img_decoder_dsc_t img_decoder_dsc;
    img_dst->header.always_zero = 0;
    #else
    lv_image_decoder_dsc_t img_decoder_dsc = { 0 };
    img_dst->header.magic = LV_IMAGE_HEADER_MAGIC;
    #endif
    img_dst->header.w = 0;  // 宽度会由解码器填充
    img_dst->header.h = 0;  // 高度会由解码器填充
    img_dst->header.cf = 0;  // cf不要赋值!
    img_dst->data = img_data;
    img_dst->data_size = img_size;

    memset((char *)&img_decoder_dsc, 0, sizeof(img_decoder_dsc));
    #if LVGL_VERSION_MAJOR < 9
    ret = lv_img_decoder_open(&img_decoder_dsc, (void *)img_dst, img_decoder_dsc.color, img_decoder_dsc.frame_id);
    #else
    ret = lv_image_decoder_open(&img_decoder_dsc, (void *)img_dst, NULL);
    #endif
    if(ret != LV_RES_OK) {
        TY_GUI_LOG_PRINT("[%s][%d] decode fail:%d\r\n", __FUNCTION__, __LINE__, ret);
        ret = OPRT_COM_ERROR;
        return ret;
    }
    ret = OPRT_OK;
    #if LVGL_VERSION_MAJOR < 9
    memcpy(&img_dst->header, &img_decoder_dsc.header, sizeof(lv_img_header_t));
    img_dst->data = img_decoder_dsc.img_data;
    #else
    memcpy(&img_dst->header, &img_decoder_dsc.decoded->header, sizeof(lv_image_header_t));
    img_dst->data = img_decoder_dsc.decoded->data;
    img_dst->data_size = img_decoder_dsc.decoded->data_size;
    //lv_image_decoder_close(&img_decoder_dsc);                   //???
    #endif
#endif

#if LVGL_VERSION_MAJOR < 9
    TY_GUI_LOG_PRINT("[%s][%d] decode success, src_type:%d, width:%d, height:%d, cf:%d, size:%d\r\n", 
                    __FUNCTION__, __LINE__, lv_img_src_get_type((void *)img_dst),
                    img_dst->header.w, img_dst->header.h, img_dst->header.cf, img_dst->data_size);
#else
TY_GUI_LOG_PRINT("[%s][%d] decode success, src_type:%d, width:%d, height:%d, stride:%d, cf:%d, size:%d\r\n", 
                __FUNCTION__, __LINE__, lv_image_src_get_type((void *)img_dst),
                img_dst->header.w, img_dst->header.h, img_dst->header.stride, img_dst->header.cf, img_dst->data_size);
#endif
    return ret;
}

/*
 图像数据流加载: 软解码jpg/软解码jpeg/png
*/
OPERATE_RET raw_img_load(GUI_IMAGE_TYPE_E image_type, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, bool jpg_hw_dec)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    bool jpg_hw_dec_on = false;

#ifdef ENABLE_JPEG_IMAGE_DECODE_WITH_HW
    jpg_hw_dec_on = jpg_hw_dec;
#endif

    if ((image_type==image_type_jpg) || (image_type==image_type_jpeg)) {
        if (jpg_hw_dec_on)
            ret = raw_jpg_img_load_with_hw_dec(img_data, img_size, img_dst);
        else
            ret = raw_jpg_img_load(img_data, img_size, img_dst);
        TY_GUI_LOG_PRINT("[%s][%d] raw jpg sw decode ret:'%d'\r\n", __FUNCTION__, __LINE__, ret);
    }
    else if (image_type==image_type_png){
        ret = __raw_png_img_load_with_scale(img_data, img_size, img_dst, 0, 0);
        TY_GUI_LOG_PRINT("[%s][%d] raw png decode ret:'%d'\r\n", __FUNCTION__, __LINE__, ret);
    }
    return ret;
}

/*
 png图像数据流加载: 支持缩放
*/
OPERATE_RET raw_png_img_load_with_scale(uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;

    ret = __raw_png_img_load_with_scale(img_data, img_size, img_dst, target_width, target_height);
    TY_GUI_LOG_PRINT("[%s][%d] raw png decode with scale ret:'%d'\r\n", __FUNCTION__, __LINE__, ret);

    return ret;
}

/*
 图像数据流卸载: jpg/jpeg/png
*/
void raw_img_unload(lv_img_dsc_t *img_dst)
{
    if (img_dst->data != NULL) {
        if (img_dst->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA) {     //png image
            #if defined(PNG_DECODE_WITH_STB_IMAGE)
            stbi_image_free((void *)img_dst->data);
            #elif defined(PNG_DECODE_WITH_LIBSPNG)
            tkl_system_psram_free((void *)img_dst->data);
            #else
            lv_mem_free((void *)img_dst->data);
            #endif
        }
        else
            tkl_system_psram_free((void *)img_dst->data);
    }
    img_dst->data = NULL;
}

#ifdef TUYA_LIBWEBP
OPERATE_RET webp_img_load_with_scale(char *filename, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    gui_img_frame_buffer_t *webp_frame = NULL;

    webp_frame = img_read_file(filename);
    if (webp_frame != NULL) {
        ret = raw_webp_img_load_with_scale(webp_frame->frame, webp_frame->length, img_dst, target_width, target_height);
        if(webp_frame->frame) {
            tkl_system_psram_free(webp_frame->frame);
        }
        tkl_system_free(webp_frame);
    }
    return ret;
}
#endif
#endif

/*
gif文件加载
*/
OPERATE_RET gif_img_load(char *filename, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    lv_fs_file_t file;
    lv_fs_res_t res;

    res = lv_fs_open(&file, filename, LV_FS_MODE_RD);
    if (res != LV_FS_RES_OK) {
        LV_LOG_ERROR("Failed to open file: %s\n", filename);
        return ret;
    }

    // 读取文件大小
    lv_fs_seek(&file, 0, LV_FS_SEEK_END);  // 移动到文件末尾
    uint32_t file_size;
    lv_fs_tell(&file, &file_size);   // 获取文件大小
    lv_fs_seek(&file, 0, LV_FS_SEEK_SET);   // 移动回文件开头

    // 分配内存以存储文件内容
    uint8_t *buffer = tkl_system_psram_malloc(file_size);
    if (buffer == NULL) {
        LV_LOG_ERROR("Memory allocation failed\n");
        lv_fs_close(&file);
        return ret;
    }

    uint32_t bytes_read;
    res = lv_fs_read(&file, buffer, file_size, &bytes_read);
    if (res != LV_FS_RES_OK || bytes_read != file_size) {
        LV_LOG_ERROR("Failed to read file: %s\n", filename);
        tkl_system_psram_free(buffer);  // 释放内存
    }
    else {
        LV_LOG_WARN("------------------------gif file '%s' load successful !\r\n", filename);
        img_dst->data = buffer;
        img_dst->data_size = file_size;
        ret = OPRT_OK;
    }
    lv_fs_close(&file);
    return ret;
}

/*
 gif文件卸载:
*/
void gif_img_unload(lv_img_dsc_t *img_dst)
{
    uint8_t *tmp_data = (uint8_t *)img_dst->data;
    if (tmp_data != NULL)
        tkl_system_psram_free((void *)tmp_data);
    img_dst->data = NULL;
}

GUI_IMAGE_TYPE_E get_image_type(char *img_name)
{
    char *ptr = NULL;
    GUI_IMAGE_TYPE_E type = image_type_unknown;

    if (img_name) {
        ptr = strrchr(img_name, '.');
        if (ptr) {
            if (strcmp(ptr, ".jpg") == 0 || strcmp(ptr, ".JPG") == 0)
                type = image_type_jpg;
            else if (strcmp(ptr, ".jpeg") == 0 || strcmp(ptr, ".JPEG") == 0)
                type = image_type_jpeg;
            else if (strcmp(ptr, ".png") == 0 || strcmp(ptr, ".PNG") == 0)
                type = image_type_png;
            else if (strcmp(ptr, ".gif") == 0 || strcmp(ptr, ".GIF") == 0)
                type = image_type_gif;
        }
    }
    if (type == image_type_unknown){
        TY_GUI_LOG_PRINT("unknow image type as name '%s'\r\n", (img_name!=NULL)?img_name:"null");
    }
    return type;
}

#ifdef TUYA_LIBWEBP
#if LVGL_VERSION_MAJOR < 9
static void webp_convert_color_depth(uint8_t * img, uint32_t px_cnt, size_t *len)
{
#if LV_COLOR_DEPTH == 32
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    lv_color_t * img_c = (lv_color_t *) img;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img_c[i].ch.red = c.ch.blue;
        img_c[i].ch.blue = c.ch.red;
    }
#elif LV_COLOR_DEPTH == 16
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint16_t r, g, b, raw, swapped;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.blue, img_argb[i].ch.green, img_argb[i].ch.red);
        img[i * 3 + 2] = img_argb[i].ch.alpha;
    #if defined(LV_COLOR_16_SWAP) && (LV_COLOR_16_SWAP == 1)
        img[i * 3 + 0] = c.full >> 8;
        img[i * 3 + 1] = c.full & 0xFF;
    #else
        img[i * 3 + 1] = c.full >> 8;
        img[i * 3 + 0] = c.full & 0xFF;
        raw = *((uint16_t *)&img[i * 3]);
        r = (raw >> 11) & 0x1F;
        g = (raw >> 5)  & 0x3F;
        b = raw         & 0x1F;
        swapped = (b << 11) | (g << 5) | r;
        memcpy((void *)&img[i * 3], (void *)&swapped, 2);
    #endif
    }
#elif LV_COLOR_DEPTH == 8
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = c.full;
    }
#elif LV_COLOR_DEPTH == 1
    lv_color32_t * img_argb = (lv_color32_t *)img;
    uint8_t b;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        b = img_argb[i].ch.red | img_argb[i].ch.green | img_argb[i].ch.blue;
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = b > 128 ? 1 : 0;
    }
#endif
}
#else
static void webp_convert_color_depth(uint8_t * img_p, uint32_t px_cnt, size_t *len)
{
#if LV_COLOR_DEPTH == 16                                    //转换为:LV_COLOR_FORMAT_RGB565A8
    lv_color32_t * img_argb = (lv_color32_t *)img_p;
    size_t rgb_size = px_cnt * 2;
    size_t alpha_size = px_cnt;
    lv_color16_t *rgb_data = (lv_color16_t*)img_p;
    uint8_t *alpha_data = tkl_system_psram_malloc(alpha_size);
    uint32_t i;

    if (alpha_data == NULL) {
        TY_GUI_LOG_PRINT("[%s][%d] Failed to allocate memory for alpha_data ?\r\n", __FUNCTION__, __LINE__);
        return;
    }
    for (i = 0; i < px_cnt; i++) {
        uint8_t r = img_argb[i].red;
        uint8_t g = img_argb[i].green;
        uint8_t b = img_argb[i].blue;
        uint8_t a = img_argb[i].alpha;
        rgb_data[i].blue = b >> 3;
        rgb_data[i].green = g >> 2;
        rgb_data[i].red = r >> 3;
    #if defined(LV_COLOR_16_SWAP) && (LV_COLOR_16_SWAP == 1)
        uint16_t *_rgb_data = (uint16_t *)rgb_data;
        uint16_t pixel = _rgb_data[i];
        pixel = (pixel >> 8) | (pixel << 8);
        _rgb_data[i] = pixel;
    #endif
        alpha_data[i] = a;
    }
    memcpy((void *)(img_p + rgb_size), (void *)alpha_data, alpha_size);
    tkl_system_psram_free((void *)alpha_data);
    *len = rgb_size + alpha_size;
#elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
    //lv_color32_t * img_argb = (lv_color32_t *)img_p;
    //uint32_t i;
    //for(i = 0; i < px_cnt; i++) {
    //    uint8_t blue = img_argb[i].blue;
    //    img_argb[i].blue = img_argb[i].red;
    //    img_argb[i].red = blue;
    //}
#else
    #error "unknown color depth"
#endif
}
#endif

OPERATE_RET raw_webp_img_load_with_scale(const uint8_t* webp_data, size_t data_size, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    int width = 0, height = 0;
    WebPDecoderConfig config = { 0 };
    OPERATE_RET ret = OPRT_COM_ERROR;

    if (!WebPInitDecoderConfig(&config)) {
        TY_GUI_LOG_PRINT("webp decode init error !\n");
        return ret;
    }

    // 获取图片特性（包括Alpha信息）
    if (WebPGetFeatures(webp_data, data_size, &config.input) != VP8_STATUS_OK) {
        WebPFreeDecBuffer(&config.output);
        TY_GUI_LOG_PRINT("webp decode get feature error !\n");
        return ret;
    }

    if (config.input.has_alpha) {
        config.output.colorspace = MODE_BGRA;
    }
    else {
    #if LV_COLOR_DEPTH == 16
        config.output.colorspace = MODE_RGB_565;
    #elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
        config.output.colorspace = MODE_BGR;
    #else
        #error "unknown color depth"
    #endif
    }

    if (target_width == 0 && target_height == 0) {
        config.options.use_scaling = 0;
        width = config.input.width;
        height = config.input.height;
    }
    else {
        // 计算保持宽高比的尺寸
        #if 0
        width = target_width;
        height = target_height;
        if (!WebPRescalerGetScaledDimensions(config.input.width, config.input.height, &width, &height)) {
            WebPFreeDecBuffer(&config.output);
            LV_LOG_ERROR("webp decode get scale dimension error !\n");
            return NULL;
        }
        #else
        double target_scale_x = (double)target_width / config.input.width;
        double target_scale_y = (double)target_height / config.input.height;
        double target_scale = fmin(target_scale_x, target_scale_y);
        width = (int)round(target_scale*config.input.width);
        height = (int)round(target_scale*config.input.height);
        #endif
        config.options.use_scaling = 1;                    // 启用缩放
        config.options.scaled_width = width;
        config.options.scaled_height = height;
    }

    TY_GUI_LOG_PRINT("webp: %s, original [w: %d, h: %d], target [w: %d, h: %d], actual [w: %d, h: %d], colorspace [ %d ]!\n",
        config.input.has_alpha ? "has_alpha":"no_alpha",
        config.input.width, config.input.height,
        target_width, target_height,
        width, height,
        config.output.colorspace);

    // 可选：是否启用多线程解码（如果在FreeRTOS中使用了多任务）
    config.options.use_threads = 0;

    // 计算缓冲区大小
    size_t buffer_size;
    if (config.output.colorspace == MODE_RGB_565) {
        buffer_size = width * height * 2;  // 2字节/像素
    } else {
        buffer_size = width * height * (config.input.has_alpha ? 4 : 3);
    }

    // 分配外部缓冲区
    void* output_buffer = tkl_system_psram_malloc(buffer_size);
    if (!output_buffer) {
        WebPFreeDecBuffer(&config.output);
        TY_GUI_LOG_PRINT("webp decode malloc error !\n");
        return ret;
    }

    // 设置外部缓冲区
    config.output.u.RGBA.rgba = (uint8_t*)output_buffer;
    config.output.u.RGBA.size = buffer_size;
    if (config.output.colorspace == MODE_RGB_565) {
        config.output.u.RGBA.stride = width * 2;
    } else {
        config.output.u.RGBA.stride = width * (config.input.has_alpha ? 4 : 3);
    }
    config.output.is_external_memory = 1;

    // 执行解码
    VP8StatusCode decode_status = WebPDecode(webp_data, data_size, &config);
    if (decode_status != VP8_STATUS_OK) {
        tkl_system_psram_free(output_buffer);
        WebPFreeDecBuffer(&config.output);
        TY_GUI_LOG_PRINT("webp decode error ,err_code '%d'!\n", decode_status);
        return ret;
    }

    if (config.input.has_alpha) {
        webp_convert_color_depth((uint8_t *)output_buffer, width * height, &buffer_size);
    }

    img_dst->header.w = width;
    img_dst->header.h = height;
    img_dst->data_size = buffer_size;
    img_dst->data = output_buffer;

#if LVGL_VERSION_MAJOR < 9
    img_dst->header.always_zero = 0;
    if (config.input.has_alpha) {
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    }
    else
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
#else
    img_dst->header.stride = config.output.u.RGBA.stride;
    img_dst->header.magic= LV_IMAGE_HEADER_MAGIC;
    if (config.input.has_alpha) {
        #if LV_COLOR_DEPTH == 16
        img_dst->header.cf = LV_COLOR_FORMAT_RGB565A8;
        img_dst->header.stride = width * 2;                     //RGB部分的stride, alpha data不计.
        #elif LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
        img_dst->header.cf = LV_COLOR_FORMAT_ARGB8888;
        #else
        #error "unknown color depth"
        #endif
    }
    else {
        #if LV_COLOR_DEPTH == 32
        img_dst->header.cf = LV_COLOR_FORMAT_RGB888;        //最高支持RGB888
        #else
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
        #endif
    }
#endif

    WebPFreeDecBuffer(&config.output);
    ret = OPRT_OK;
    return ret;
}

/*
 图像文件卸载: webp格式
*/
void webp_img_unload(lv_img_dsc_t *img_dst)
{
    if (img_dst->data != NULL)
        tkl_system_psram_free((void *)img_dst->data);
    img_dst->data = NULL;
}
#endif

//#endif

