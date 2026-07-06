/**
 * @file libjpeg_turbo_decode.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "libjpeg_turbo_decode.h"

#if defined(TUYA_LIBJPEG_TURBO) && (TUYA_LIBJPEG_TURBO == 1)
#include <stdio.h>
#include <turbojpeg.h>
#include <jpeglib.h>
#include <jpegint.h>
#include <setjmp.h>
#include <math.h>
#include "img_utility.h"
#include "tkl_memory.h"

/*********************
 *      DEFINES
 *********************/
#ifdef ENABLE_STB_IMG_SCALE
#include "stb_image_resize2.h"
#define TJ_SECONDARY_SCALING                   //二次缩放
#endif
//#define IMG_DECODING_TIME_TEST
#define JPEG_PIXEL_SIZE 3 /* RGB888 */
#define JPEG_SIGNATURE 0xFFD8FF
#define IS_JPEG_SIGNATURE(x) (((x) & 0x00FFFFFF) == JPEG_SIGNATURE)

/**********************
 *      TYPEDEFS
 **********************/
typedef struct error_mgr_s {
    struct jpeg_error_mgr pub;
    jmp_buf jb;
} error_mgr_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static OPERATE_RET libjpeg_turbo_decode_jpeg_file(uint8_t * data, uint32_t data_size, uint8_t *out_data);
static bool libjpeg_turbo_get_jpeg_head_info(uint8_t * data, uint32_t data_size, uint32_t * width, uint32_t * height, uint32_t * orientation);
static bool libjpeg_turbo_get_jpeg_size(uint8_t * data, uint32_t data_size, uint32_t * width, uint32_t * height);
//static bool libjpeg_turbo_get_jpeg_direction(uint8_t * data, uint32_t data_size, uint32_t * orientation);
static void libjpeg_turbo_error_exit(j_common_ptr cinfo);
static OPERATE_RET jpg_dec_img_start(gui_img_frame_buffer_t *jpeg_frame, lv_img_dsc_t *img_dst);
static OPERATE_RET jpg_dec_img_start_with_scale(gui_img_frame_buffer_t *jpeg_frame, lv_img_dsc_t *img_dst, int target_width, int target_height);

/**********************
 *  STATIC VARIABLES
 **********************/
//static const int JPEG_EXIF = 0x45786966; /* Exif data structure tag */
//static const int JPEG_BIG_ENDIAN_TAG = 0x4d4d;
//static const int JPEG_LITTLE_ENDIAN_TAG = 0x4949;

/**********************
 *      MACROS
 **********************/
#define TRANS_32_VALUE(big_endian, data) big_endian ? \
    ((*(data) << 24) | (*((data) + 1) << 16) | (*((data) + 2) << 8) | *((data) + 3)) : \
    (*(data) | (*((data) + 1) << 8) | (*((data) + 2) << 16) | (*((data) + 3) << 24))
#define TRANS_16_VALUE(big_endian, data) big_endian ? \
    ((*(data) << 8) | *((data) + 1)) : (*(data) | (*((data) + 1) << 8))

/**********************
 *   GLOBAL FUNCTIONS
 **********************/
#ifdef IMG_DECODING_TIME_TEST
extern unsigned long long int __current_timestamp(void);
#endif
static OPERATE_RET __jpg_dec_img(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    gui_img_frame_buffer_t *jpeg_frame = NULL;
    unsigned long long int last_run_ms = 0, curr_run_ms = 0;

    do{
        if (is_file) {
        #if defined(TUYA_FILE_SYSTEM) && (TUYA_FILE_SYSTEM == 1)
        #ifdef IMG_DECODING_TIME_TEST
            last_run_ms = __current_timestamp();
        #endif
            jpeg_frame = img_read_file(file_name);
        #ifdef IMG_DECODING_TIME_TEST
            curr_run_ms = __current_timestamp();
        #endif
            TY_GUI_LOG_PRINT("[%s][%d] file '%s' get use '%llu'ms\r\n",__FUNCTION__, __LINE__, file_name,
                curr_run_ms-last_run_ms);
        #else
            TY_GUI_LOG_PRINT("[%s][%d] don't support jpg file decode ??? \r\n",__FUNCTION__, __LINE__);
        #endif
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
        if (target_width == 0 && target_height == 0)    //decode without scaling!
            ret = jpg_dec_img_start(jpeg_frame, img_dst);
        else {          //decode with scaling!
            ret = jpg_dec_img_start_with_scale(jpeg_frame, img_dst, target_width, target_height);
        }
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

    return ret;
}

OPERATE_RET jpg_dec_img(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst)
{
    return __jpg_dec_img(is_file, file_name, img_data, img_size, img_dst, 0, 0);
}

OPERATE_RET jpg_dec_img_with_scale(bool is_file, char *file_name, uint8_t *img_data, uint32_t img_size, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    return __jpg_dec_img(is_file, file_name, img_data, img_size, img_dst, target_width, target_height);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
static OPERATE_RET jpg_dec_img_start(gui_img_frame_buffer_t *jpeg_frame, lv_img_dsc_t *img_dst)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
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

    uint32_t width;
    uint32_t height;
    uint32_t orientation = 0;

    if(!libjpeg_turbo_get_jpeg_head_info(jpeg_frame->frame, jpeg_frame->length, &width, &height, &orientation)) {
        TY_GUI_LOG_PRINT("[%s][%d] get head info fail\r\n", __FUNCTION__, __LINE__);
        return ret;
    }

    img_dst->header.w = (orientation % 180) ? height : width;
    img_dst->header.h = (orientation % 180) ? width : height;
    img_dst->data_size = img_dst->header.w*img_dst->header.h*bytes_per_pixel;
#if LVGL_VERSION_MAJOR >= 9
    img_dst->header.stride = img_dst->header.w*bytes_per_pixel;
#endif
    if(img_dst->data == NULL)
    {
        img_dst->data = tkl_system_psram_malloc(img_dst->data_size);
        if(!img_dst->data)
        {
            TY_GUI_LOG_PRINT("[%s][%d] malloc psram size %d fail\r\n", __FUNCTION__, __LINE__, img_dst->data_size);
            return ret;
        }
    }

/******************************/
    ret = libjpeg_turbo_decode_jpeg_file(jpeg_frame->frame, jpeg_frame->length, (uint8_t *)img_dst->data);
    if(ret != OPRT_OK) {
        TY_GUI_LOG_PRINT("decode jpeg file failed");
        return ret;
    }
/******************************/
    
#if LVGL_VERSION_MAJOR < 9
    img_dst->header.always_zero = 0;
    img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
#else
    //jpg_convert_color_depth(img_dst->data, img_dst->data_size/bytes_per_pixel);
    img_dst->header.magic= LV_IMAGE_HEADER_MAGIC;
    #if LV_COLOR_DEPTH == 32
        img_dst->header.cf = LV_COLOR_FORMAT_RGB888;        //最高支持RGB888
    #else
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
    #endif
#endif

    return ret;
}

static OPERATE_RET libjpeg_turbo_decode_jpeg_file(uint8_t * data, uint32_t data_size, uint8_t *out_data)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    unsigned char bytes_per_pixel = 2;
    /* This struct contains the JPEG decompression parameters and pointers to
     * working space (which is allocated as needed by the JPEG library).
     */
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
     * Note that this struct must live as long as the main JPEG parameter
     * struct, to avoid dangling-pointer problems.
     */
    error_mgr_t jerr;
    //lv_color_format_t cf = LV_COLOR_FORMAT_NATIVE;

    /* More stuff */
    JSAMPARRAY buffer;  /* Output row buffer */

    int row_stride;     /* physical row width in output buffer */
    uint32_t image_angle = 0;   /* image rotate angle */

    /* allocate and initialize JPEG decompression object */

    /* We set up the normal JPEG error routines, then override error_exit. */
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = libjpeg_turbo_error_exit;
    /* Establish the setjmp return context for my_error_exit to use. */
    if(setjmp(jerr.jb)) {
        TY_GUI_LOG_PRINT("decoding error");
        /* If we get here, the JPEG code has signaled an error.
        * We need to clean up the JPEG object, close the input file, and return.
        */
        jpeg_destroy_decompress(&cinfo);
        ret = OPRT_COM_ERROR;
        return ret;
    }

    /* Get rotate angle from Exif data */
    //if(!libjpeg_turbo_get_jpeg_direction(data, data_size, &image_angle)) {
    //    TY_GUI_LOG_PRINT("read jpeg orientation failed.");
    //}

    /* Now we can initialize the JPEG decompression object. */
    jpeg_create_decompress(&cinfo);

    /* specify data source (eg, a file or buffer) */

    jpeg_mem_src(&cinfo, data, data_size);

    /* read file parameters with jpeg_read_header() */

    jpeg_read_header(&cinfo, TRUE);

    /* We can ignore the return value from jpeg_read_header since
     *   (a) suspension is not possible with the stdio data source, and
     *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
     * See libjpeg.doc for more info.
     */

    /* set parameters for decompression */

#if LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
    cinfo.out_color_space = JCS_EXT_BGR;
    //cf = LV_COLOR_FORMAT_RGB888;
    bytes_per_pixel = 3;
#elif LV_COLOR_DEPTH == 16
    cinfo.out_color_space = JCS_RGB565;
    bytes_per_pixel = 2;
#elif LV_COLOR_DEPTH == 8
    cinfo.out_color_space = JCS_GRAYSCALE;
    bytes_per_pixel = 1;
#else
    #error "unknown color depth"
#endif
    /* In this example, we don't need to change any of the defaults set by
     * jpeg_read_header(), so we do nothing here.
     */

    /* Start decompressor */

    jpeg_start_decompress(&cinfo);
    TY_GUI_LOG_PRINT("[%s][%d]:bytes_per_pixel '%d', output_components '%d'", __func__, __LINE__, bytes_per_pixel, cinfo.output_components);
    /* We can ignore the return value since suspension is not possible
     * with the stdio data source.
     */

    /* We may need to do some setup of our own at this point before reading
     * the data.  After jpeg_start_decompress() we have the correct scaled
     * output image dimensions available, as well as the output colormap
     * if we asked for color quantization.
     * In this example, we need to make an output work buffer of the right size.
     */
    /* JSAMPLEs per row in output buffer */
    //if (cinfo.out_color_space == JCS_RGB565)
    //    row_stride = cinfo.output_width * 2;
    //else
        row_stride = cinfo.output_width * cinfo.output_components;
    /* Make a one-row-high sample array that will go away when done with image */
    buffer = (*cinfo.mem->alloc_sarray)
             ((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);
    uint32_t buf_width = (image_angle % 180) ? cinfo.output_height : cinfo.output_width;
    //uint32_t buf_height = (image_angle % 180) ? cinfo.output_width : cinfo.output_height;
    uint32_t stride = buf_width * bytes_per_pixel;
    if(out_data != NULL) {
        uint32_t line_index = 0;
        /* while (scan lines remain to be read) */
        /* jpeg_read_scanlines(...); */

        /* Here we use the library's state variable cinfo.output_scanline as the
         * loop counter, so that we don't have to keep track ourselves.
         */
        while(cinfo.output_scanline < cinfo.output_height) {
            /* jpeg_read_scanlines expects an array of pointers to scanlines.
             * Here the array is only one element long, but you could ask for
             * more than one scanline at a time if that's more convenient.
             */
            jpeg_read_scanlines(&cinfo, buffer, 1);

            /* Assume put_scanline_someplace wants a pointer and sample count. */
            memcpy(out_data + line_index * stride, buffer[0], stride);
            line_index++;
        }
    }

    /* Finish decompression */

    jpeg_finish_decompress(&cinfo);

    /* We can ignore the return value since suspension is not possible
     * with the stdio data source.
     */

    /* Release JPEG decompression object */

    /* This is an important step since it will release a good deal of memory. */
    jpeg_destroy_decompress(&cinfo);

    /* And we're done! */
    ret = OPRT_OK;
    return ret;
}

static bool libjpeg_turbo_get_jpeg_head_info(uint8_t * data, uint32_t data_size, uint32_t * width, uint32_t * height, uint32_t * orientation)
{
    if(data == NULL) {
        return false;
    }

    if(!libjpeg_turbo_get_jpeg_size(data, data_size, width, height)) {
        TY_GUI_LOG_PRINT("read jpeg size failed.");
    }

    //if(!libjpeg_turbo_get_jpeg_direction(data, data_size, orientation)) {
    //    TY_GUI_LOG_PRINT("read jpeg orientation failed.");
    //}
    return true;
}

static bool libjpeg_turbo_get_jpeg_size(uint8_t * data, uint32_t data_size, uint32_t * width, uint32_t * height)
{
    struct jpeg_decompress_struct cinfo;
    error_mgr_t jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = libjpeg_turbo_error_exit;

    if(setjmp(jerr.jb)) {
        TY_GUI_LOG_PRINT("read jpeg head failed");
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, data, data_size);

    int ret = jpeg_read_header(&cinfo, TRUE);

    if(ret == JPEG_HEADER_OK) {
        *width = cinfo.image_width;
        *height = cinfo.image_height;
    }
    else {
        TY_GUI_LOG_PRINT("read jpeg head failed: %d", ret);
    }

    jpeg_destroy_decompress(&cinfo);

    return JPEG_HEADER_OK;
}

#if 0
static bool libjpeg_turbo_get_jpeg_direction(uint8_t * data, uint32_t data_size, uint32_t * orientation)
{
    struct jpeg_decompress_struct cinfo;
    error_mgr_t jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = libjpeg_turbo_error_exit;

    if(setjmp(jerr.jb)) {
        TY_GUI_LOG_PRINT("read jpeg orientation failed");
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, data, data_size);

    jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);

    cinfo.marker->read_markers(&cinfo);

    jpeg_saved_marker_ptr marker = cinfo.marker_list;
    while(marker != NULL) {
        if(marker->marker == JPEG_APP0 + 1) {
            JOCTET FAR * app1_data = marker->data;
            if(TRANS_32_VALUE(true, app1_data) == JPEG_EXIF) {
                uint16_t endian_tag = TRANS_16_VALUE(true, app1_data + 4 + 2);
                if(!(endian_tag == JPEG_LITTLE_ENDIAN_TAG || endian_tag == JPEG_BIG_ENDIAN_TAG)) {
                    jpeg_destroy_decompress(&cinfo);
                    return false;
                }
                bool is_big_endian = endian_tag == JPEG_BIG_ENDIAN_TAG;
                /* first ifd offset addr : 4bytes(Exif) + 2bytes(0x00) + 2bytes(align) + 2bytes(tag mark) */
                unsigned int offset = TRANS_32_VALUE(is_big_endian, app1_data + 8 + 2);
                /* ifd base : 4bytes(Exif) + 2bytes(0x00) */
                unsigned char * ifd = 0;
                do {
                    /* ifd start: 4bytes(Exif) + 2bytes(0x00) + offset value(2bytes(align) + 2bytes(tag mark) + 4bytes(offset size)) */
                    unsigned int entry_offset = 4 + 2 + offset + 2;
                    if(entry_offset >= marker->data_length) {
                        jpeg_destroy_decompress(&cinfo);
                        return false;
                    }
                    ifd = app1_data + entry_offset;
                    unsigned short num_entries = TRANS_16_VALUE(is_big_endian, ifd - 2);
                    if(entry_offset + num_entries * 12 >= marker->data_length) {
                        jpeg_destroy_decompress(&cinfo);
                        return false;
                    }
                    for(int i = 0; i < num_entries; i++) {
                        unsigned short tag = TRANS_16_VALUE(is_big_endian, ifd);
                        if(tag == 0x0112) {
                            /* ifd entry: 12bytes = 2bytes(tag number) + 2bytes(kind of data) + 4bytes(number of components) + 4bytes(data)
                            * orientation kind(0x03) of data is unsigned short */
                            int dirc = TRANS_16_VALUE(is_big_endian, ifd + 2 + 2 + 4);
                            switch(dirc) {
                                case 1:
                                    *orientation = 0;
                                    break;
                                case 3:
                                    *orientation = 180;
                                    break;
                                case 6:
                                    *orientation = 90;
                                    break;
                                case 8:
                                    *orientation = 270;
                                    break;
                                default:
                                    *orientation = 0;
                            }
                        }
                        ifd += 12;
                    }
                    offset = TRANS_32_VALUE(is_big_endian, ifd);
                } while(offset != 0);
            }
            break;
        }
        marker = marker->next;
    }

    jpeg_destroy_decompress(&cinfo);

    return JPEG_HEADER_OK;
}
#endif

static void libjpeg_turbo_error_exit(j_common_ptr cinfo)
{
    error_mgr_t * myerr = (error_mgr_t *)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->jb, 1);
}

/**
 * jpg decode with scale
 */
static int decode_with_arbitrary_scale(const unsigned char *jpeg_buf,
                                unsigned long jpeg_size,
                                unsigned char **output_buf,
                                int *out_width, int *out_height,
                                int target_width, int target_height) {
    int ret = -1;
    tjhandle handle = tjInitDecompress();
    if (!handle) return ret;
    
    // 1. 获取原始尺寸
    int orig_width, orig_height, subsamp, colorspace;
    if (tjDecompressHeader3(handle, jpeg_buf, jpeg_size,
                       &orig_width, &orig_height, &subsamp, &colorspace) < 0) {
        TY_GUI_LOG_PRINT("%s:tjDecompressHeader3 fail ?", __func__);
        tjDestroy(handle);
        return ret;
     }
    
    // 2. 计算目标缩放比例
    double target_scale_x = (double)target_width / orig_width;
    double target_scale_y = (double)target_height / orig_height;
    double target_scale = fmin(target_scale_x, target_scale_y);  // 保持比例
    
    // 3. 寻找最接近的TurboJPEG缩放因子
    int num_factors = 0;
    const tjscalingfactor *scaling_factors = tjGetScalingFactors(&num_factors);
    
    tjscalingfactor best_scale = {1, 1};
    double best_diff = 1.0;
    
    for (int i = 0; i < num_factors; i++) {
        double scale = (double)scaling_factors[i].num / scaling_factors[i].denom;
        double diff = fabs(scale - target_scale);
        if (diff < best_diff) {
            best_diff = diff;
            best_scale = scaling_factors[i];
        }
    }
    
    // 4. 计算TurboJPEG缩放后的尺寸
    int tj_width = TJSCALED(orig_width, best_scale);
    int tj_height = TJSCALED(orig_height, best_scale);
    int pixelFormat = TJPF_RGB;
    unsigned char bytes_per_pixel = 3;
#if LV_COLOR_DEPTH == 16 || LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
        pixelFormat = TJPF_RGB;
        bytes_per_pixel = 3;
#elif LV_COLOR_DEPTH == 8
        pixelFormat = TJPF_GRAY;
        bytes_per_pixel = 1;
#else
    #error "unknown color depth"
#endif

    // 5. 使用TurboJPEG进行第一次缩放
    unsigned char *tj_buf = (unsigned char *)tkl_system_psram_malloc(tj_width * tj_height * bytes_per_pixel);
    if (tj_buf == NULL) {
        TY_GUI_LOG_PRINT("%s-%d:malloc fail ?", __func__, __LINE__);
        tjDestroy(handle);
        return ret;
    }
    if (tjDecompress2(handle, jpeg_buf, jpeg_size, tj_buf,
                 tj_width, 0, tj_height, pixelFormat, TJFLAG_FASTDCT) == 0) {
    #ifdef TJ_SECONDARY_SCALING
        if (target_width != tj_width && target_height != tj_height) {       //是否二次缩放!
            int cal_width = 0, cal_height = 0;
            unsigned char *resized_data = NULL;
            stbir_pixel_layout pixel_layout = STBIR_RGB;

            cal_width = (int)round(target_scale*orig_width);
            cal_height = (int)round(target_scale*orig_height);
            TY_GUI_LOG_PRINT("%s-%d: calculated desired w:'%d', h:'%d'", __func__, __LINE__,cal_width, cal_height);
            #if LV_COLOR_DEPTH == 16 || LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
            resized_data = (unsigned char *)tkl_system_psram_malloc(cal_width * cal_height * 3);
            pixel_layout = STBIR_RGB;
            #elif LV_COLOR_DEPTH == 8
            resized_data = (unsigned char *)tkl_system_psram_malloc(cal_width * cal_height);
            pixel_layout = STBIR_1CHANNEL;
            #else
            #error "unknown color depth"
            #endif
            if (resized_data == NULL) {
                TY_GUI_LOG_PRINT("%s-%d:malloc fail ?", __func__, __LINE__);
                tkl_system_psram_free(tj_buf);
                tjDestroy(handle);
                return ret;
            }
            if (stbir_resize_uint8_linear((const unsigned char *)tj_buf, tj_width, tj_height, 0, resized_data, cal_width, cal_height, 0, pixel_layout) == NULL) {
                TY_GUI_LOG_PRINT("%s-%d: secondary scaling fail ?", __func__, __LINE__);
                tkl_system_psram_free(tj_buf);
                tkl_system_psram_free(resized_data);
                tjDestroy(handle);
                return ret;
            }
            tkl_system_psram_free(tj_buf);
            tj_buf = resized_data;
            tj_width = cal_width;
            tj_height = cal_height;
        }
    #endif

        uint32_t px_cnt = tj_width * tj_height;
        uint32_t i;
        #if LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 32
        for(i = 0; i < px_cnt; i++) {
            uint8_t blue = tj_buf[i*3+2];
            tj_buf[i*3+2] = tj_buf[i*3];
            tj_buf[i*3] = blue;
        }
        *output_buf = tj_buf;
        #elif LV_COLOR_DEPTH == 16
        uint16_t *tmp_buf = (uint16_t *)tkl_system_psram_malloc(tj_width * tj_height * sizeof(uint16_t));
        if (tmp_buf == NULL) {
            TY_GUI_LOG_PRINT("%s-%d:malloc fail ?", __func__, __LINE__);
            tkl_system_psram_free(tj_buf);
            tjDestroy(handle);
            return ret;
        }
        for(i = 0; i < px_cnt; i++) {
            uint8_t r = tj_buf[i*3];
            uint8_t g = tj_buf[i*3+1];
            uint8_t b = tj_buf[i*3+2];
            tmp_buf[i] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        }
        tkl_system_psram_free(tj_buf);
        tj_buf = NULL;
        *output_buf = (unsigned char *)tmp_buf;
        #elif LV_COLOR_DEPTH == 8
        *output_buf = tj_buf;
        #endif
        *out_width = tj_width;
        *out_height = tj_height;
        ret = 0;
        TY_GUI_LOG_PRINT("%s:[orig w:%d, h:%d][target w:%d, h:%d][final w:%d, h:%d]", __func__,
            orig_width, orig_height,
            target_width, target_height,
            tj_width, tj_height);
    }
    else {
        TY_GUI_LOG_PRINT("%s:tjDecompress2 fail ?", __func__);
    }

    tjDestroy(handle);
    return ret;
}

static OPERATE_RET jpg_dec_img_start_with_scale(gui_img_frame_buffer_t *jpeg_frame, lv_img_dsc_t *img_dst, int target_width, int target_height)
{
    OPERATE_RET ret = OPRT_COM_ERROR;
    unsigned char bytes_per_pixel = 2;
    int out_width = 0, out_height = 0;
#if LV_COLOR_DEPTH == 16
    bytes_per_pixel = 2;
#elif LV_COLOR_DEPTH == 24
    bytes_per_pixel = 3;
#elif LV_COLOR_DEPTH == 32
    bytes_per_pixel = 3;
#else
#error "unknown color depth"
#endif

/******************************/
    ret = decode_with_arbitrary_scale(jpeg_frame->frame, jpeg_frame->length, (unsigned char **)&img_dst->data,  \
                                        &out_width, &out_height, target_width, target_height);
    if(ret != 0) {
        TY_GUI_LOG_PRINT("decode jpeg file failed");
        return ret;
    }
    ret = OPRT_OK;
    img_dst->header.w = out_width;
    img_dst->header.h = out_height;
#if LVGL_VERSION_MAJOR >= 9
    img_dst->header.stride = img_dst->header.w*bytes_per_pixel;
#endif
    img_dst->data_size = img_dst->header.w*img_dst->header.h*bytes_per_pixel;
/******************************/
    
#if LVGL_VERSION_MAJOR < 9
    img_dst->header.always_zero = 0;
    img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
#else
    //jpg_convert_color_depth(img_dst->data, img_dst->data_size/bytes_per_pixel);
    img_dst->header.magic= LV_IMAGE_HEADER_MAGIC;
#if LV_COLOR_DEPTH == 32
        img_dst->header.cf = LV_COLOR_FORMAT_RGB888;        //最高支持RGB888
#else
        img_dst->header.cf = LV_IMG_CF_TRUE_COLOR;
#endif
#endif

    return ret;
}

#endif /*TUYA_LIBJPEG_TURBO*/
