/*********************************************************************************
* Copyright(C),2019, TUYA www.tuya.com

* FileName:     tuya_ipc_video_proc.c
* Note
* Version       V1.0.0
* Data          2019.02
**********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "tal_mutex.h"
#include "tuya_ipc_video_proc.h"
#include "tal_memory.h"

#define MAX_motion_region_RAWS 5
#define MIN_motion_region_RAWS 1
#define MAX_motion_region_COLS 5
#define MIN_motion_region_COLS 1

#define MAX_SENSITIVITY 10
#define MIN_SENSITIVITY 1

#define MULTI_FRAME_FILTER 3
#define NO_MOTION_TIMEOUT  6

/** Return Pointer Check */
#define TY_CHECK_POINTER(p, errcode)                                                                                   \
    do {                                                                                                               \
        if (!(p)) {                                                                                                    \
            printf("pointer[%s] is NULL\n", #p);                                                                       \
            return errcode;                                                                                            \
        }                                                                                                              \
    } while (0)

typedef struct _tuya_motion_tracking_ctrl {
    INT_T is_first;
    INT_T md_counting;
    INT_T md_no_motion_timeout;
    time_t latest_motion_time;
} tuya_motion_tracking_ctrl;

typedef struct _tuya_motion_tracking_spec {
    INT_T frame_w;
    INT_T frame_h;

    INT_T y_thd;
    INT_T sensitivity; // 1-10

    TUYA_MD_REGION_TYPE_E rect_type;
    TUYA_RPERCENT_RECT_T roi;
    TUYA_MULTI_MD_REGION_T motion_region;
    INT_T row_step;
    INT_T col_step;
    INT_T tracking_enable; // 1 open 0 clode

    UCHAR_T *pre_frame;
    UCHAR_T *curr_frame;
    UCHAR_T *seg;
    UCHAR_T *seg_max;
    UCHAR_T *labelImg;
    UCHAR_T *backgroundImg;

    UCHAR_T *lable_equal;
    TUYA_MULTI_ZONE_INFO_T multi_zone;
    UCHAR_T *p_coverage_frame;     // 有效区域配置层，用于过滤无效点，multi_zone和 polygon 使用
    INT_T is_init;                 // 判断motion是否初始化
    TUYA_MULTI_POLYGON_INFO_T multi_polygon;
} tuya_motion_tracking_spec;

MUTEX_HANDLE lock;
STATIC tuya_motion_tracking_spec g_motion_spec = {0};
STATIC tuya_motion_tracking_ctrl g_motion_ctrl = {0};
STATIC INT_T __is_in_multi_zone(INT_T x, INT_T y);
STATIC INT_T __is_in_polygon(INT_T x, INT_T y, TUYA_MD_POLYGON_T *p_polygon);

// 腐蚀算法
OPERATE_RET Tuya_Ipc_IMP_Erosion(UCHAR_T *srcImg, INT_T width, INT_T height, UCHAR_T *dstImg)
{
    UCHAR_T *pSrc = NULL;
    UCHAR_T *pSrc_temp = NULL;

    if (width - 3 < 0 && height - 3 < 0) {
        return OPRT_INVALID_PARM;
    }
    int mid = (3 + 1) / 2 - 1;
    int i = mid;
    int j = mid;
    int m = 0;
    int n = 0;
    unsigned char val = 0;

    pSrc = srcImg;
    for (i = mid; i < height - mid - 1; i++) {
        for (j = mid; j < width - mid - 1; j++) {
            pSrc_temp = pSrc;
            for (m = 0; m < 3; m++) {
                for (n = 0; n < 3; n++) {
                    if (m == mid && n == mid) {
                        continue;
                    }
                    val &= pSrc_temp[j + n];
                }
                pSrc_temp += width;
            }

            pSrc[j] = val;
            val = 255;
        }
        pSrc += width;
    }

    pSrc = NULL;
    pSrc_temp = NULL;

    return OPRT_OK;
}

// 膨胀算法
OPERATE_RET Tuya_Ipc_IMP_Dilation(UCHAR_T *srcImg, INT_T width, INT_T height, UCHAR_T *dstImg)
{
    UCHAR_T *pSrc = NULL;
    UCHAR_T *pSrc_temp = NULL;

    if (width - 3 < 0 && height - 3 < 0) {
        return OPRT_INVALID_PARM;
    }
    int mid = (3 + 1) / 2 - 1;
    int i = mid;
    int j = mid;
    int m = 0;
    int n = 0;
    unsigned char val = 0;

    pSrc = srcImg;
    for (i = mid; i < height - mid - 1; i++) {
        for (j = mid; j < width - mid - 1; j++) {
            pSrc_temp = pSrc;
            for (m = 0; m < 3; m++) {
                for (n = 0; n < 3; n++) {
                    if (m == 0 && n == 0) {
                        continue;
                    }
                    val |= pSrc_temp[j + n];
                }
                pSrc_temp += width;
            }

            pSrc[j] = val;
            val = 0;
        }
        pSrc += width;
    }

    pSrc = NULL;
    pSrc_temp = NULL;

    return OPRT_OK;
}

OPERATE_RET Tuya_Ipc_Max_Conn_Reg(TUYA_POINT_T *motion_point, tuya_motion_tracking_spec tempcfg)
{
    INT_T height = tempcfg.frame_h;
    INT_T width = tempcfg.frame_w;

    UCHAR_T *srcImg = NULL;
    UCHAR_T *dstImg = NULL;
    UCHAR_T *labelImg = NULL;
    UCHAR_T *labelImg_Temp = NULL;

    INT_T i = 0;
    INT_T j = 0;
    INT_T m = 0;
    INT_T n = 0;

    INT_T lable[1024] = {0};
    for (j = 0; j < tempcfg.frame_w; j++) {
        memset(tempcfg.lable_equal + j * tempcfg.frame_w, 0, tempcfg.frame_w);
    }

    INT_T left = 0;
    INT_T right = 0;
    INT_T top = 0;
    INT_T botton = 0;
    INT_T conn_reg_num = 1;      // conn reg serial num
    INT_T max_conn_reg_num = 0;  // max conn reg size num
    INT_T max_conn_reg_size = 0; // max conn reg size

    srcImg = tempcfg.seg + width;
    memset(tempcfg.labelImg, 0, width * height * sizeof(UCHAR_T));
    labelImg = tempcfg.labelImg + width;

    for (j = 1; j < height - 1; j++) {
        for (i = 1; i < width - 1; i++) {
            if (srcImg[i] == 255 && labelImg[i] == 0) {
                if (conn_reg_num == 1) {
                    labelImg[i] = conn_reg_num;
                    conn_reg_num++;
                } else {
                    INT_T min_neighbor_num = 0;
                    // eight neighbor

                    labelImg_Temp = labelImg - width;
                    for (n = -1; n < 2; n++) {
                        for (m = -1; m < 2; m++) {
                            if (m != 0 || n != 0) {
                                if (labelImg_Temp[(i + m)] != 0) {
                                    if (min_neighbor_num <= labelImg_Temp[(i + m)]) {
                                        min_neighbor_num = labelImg_Temp[(i + m)];
                                    }
                                }
                            }
                        }
                        labelImg_Temp += width;
                    }
                    if (min_neighbor_num == 0) {
                        labelImg[i] = conn_reg_num;
                        conn_reg_num++;
                    } else {
                        labelImg_Temp = labelImg - width;
                        for (n = -1; n < 2; n++) {
                            for (m = -1; m < 2; m++) {
                                if (m != 0 || n != 0) {
                                    if (labelImg_Temp[(i + m)] != 0 && labelImg_Temp[(i + m)] != min_neighbor_num) {
                                        if (min_neighbor_num >= tempcfg.frame_w ||
                                            labelImg_Temp[(i + m)] >= tempcfg.frame_w) {
                                            printf("min_neighbor %d, w h:%d %d, i %d, m %d, labelimg:%d\n",
                                                min_neighbor_num, tempcfg.frame_w, tempcfg.frame_h, i, m, labelImg_Temp[i + m]);
                                            return OPRT_COM_ERROR;
                                        }
                                        tempcfg.lable_equal[min_neighbor_num * tempcfg.frame_w + labelImg_Temp[(i + m)]] = 1;
                                    }
                                }
                            }
                            labelImg_Temp += width;
                        }
                        labelImg[i] = min_neighbor_num;
                    }
                }
                lable[labelImg[i]]++;

                if (max_conn_reg_size <= lable[labelImg[i]]) {
                    max_conn_reg_size = lable[labelImg[i]];
                    max_conn_reg_num = labelImg[i];
                }
            }
        }
        srcImg += width;
        labelImg += width;
    }

    // merge region
    for (m = 0; m < 3; m++) {
        for (j = 0; j < tempcfg.frame_w; j++) {
            if (tempcfg.lable_equal[max_conn_reg_num * tempcfg.frame_w + j] == 1 ||
                tempcfg.lable_equal[j * tempcfg.frame_w + max_conn_reg_num] == 1) {
                for (i = 0; i < tempcfg.frame_w; i++) {
                    if (tempcfg.lable_equal[i * tempcfg.frame_w + j] == 1 ||
                        tempcfg.lable_equal[j * tempcfg.frame_w + i] == 1) {
                        tempcfg.lable_equal[max_conn_reg_num * tempcfg.frame_w + i] = 1;
                        tempcfg.lable_equal[i * tempcfg.frame_w + max_conn_reg_num] = 1;
                    }
                }
            }
        }
    }

    dstImg = tempcfg.seg_max;
    labelImg = tempcfg.labelImg;
    // printf("max_conn_reg_num = %d \n",max_conn_reg_num);
    // printf("max_conn_reg_size = %d \n", max_conn_reg_size);
    for (j = 0; j < height; j++) {
        for (i = 0; i < width; i++) {
            if (max_conn_reg_num >= tempcfg.frame_w || labelImg[i] >= tempcfg.frame_w) {
                printf("min_neighbor %d, w h:%d %d, i %d, m %d, labelimg:%d\n",
                    max_conn_reg_num, tempcfg.frame_w,  tempcfg.frame_h, i, labelImg[i]);
                return OPRT_COM_ERROR;
            }
            if (labelImg[i] == max_conn_reg_num ||
                tempcfg.lable_equal[max_conn_reg_num * tempcfg.frame_w + labelImg[i]] == 1 ||
                tempcfg.lable_equal[labelImg[i] * tempcfg.frame_w + max_conn_reg_num] == 1) {
                dstImg[i] = 255;

                if (left == 0 && right == 0 && top == 0 && botton == 0) {
                    left = i;
                    right = i;
                    top = j;
                    botton = j;
                } else {
                    if (left > i)
                        left = i;
                    if (right < i)
                        right = i;
                    if (top > j)
                        top = j;
                    if (botton < j)
                        botton = j;
                }
            } else {
                dstImg[i] = 0;
            }
        }
        dstImg += width;
        labelImg += width;
    }

    motion_point->x = (left + right) / 2 - width / 2;
    motion_point->y = (top + botton) / 2 - height / 2;

    srcImg = NULL;
    dstImg = NULL;
    labelImg = NULL;
    labelImg_Temp = NULL;
    return OPRT_OK;
}

// 根据多边形配置参数，生成有效区域
static void __multi_polygon_mask(UCHAR_T *p, TUYA_MULTI_POLYGON_INFO_T *p_multi_polygon)
{
    int i;
    int j;
    int k;
    TUYA_MD_POLYGON_T *p_polygon = p_multi_polygon->polygon_list;
    for (j = 1; j < g_motion_spec.frame_h-1; j++) {
        for (i = 1; i < g_motion_spec.frame_w-1; i++) {
            for (k = 0; k < p_multi_polygon->polygon_num; k++) {
                if ((__is_in_polygon(i, j, &p_polygon[k]) == 1)) {
                    p[g_motion_spec.frame_w*j+i] = 1;
                    break;
                }
            }
        }
    }
    return;
}

static int __mask_show()
{
    int i = 0;
    int j = 0;
    UCHAR_T *p = g_motion_spec.p_coverage_frame;
    for (j = 0; j < g_motion_spec.frame_h;) {
        for (i = 0; i < g_motion_spec.frame_w;) {
            printf("%d", p[g_motion_spec.frame_w*j+i]);
            i = i+4;
        }
        j = j+4;
        printf("\n");
    }

    printf("\n");
    return 0;
}

OPERATE_RET tuya_ipc_motion_init(TUYA_MOTION_TRACKING_CFG_T mt_cfg)
{
    int i = 0;
    int j = 0;
    if (g_motion_spec.is_init == 1){
        printf("motion is Initialized\n");
        return OPRT_INVALID_PARM;
    }
    tal_mutex_create_init(&lock);

    if (mt_cfg.frame_w <= 0 || mt_cfg.frame_h <= 0) {
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type != SINGLE && mt_cfg.rect_type != MULTI && mt_cfg.rect_type != MULTI_ZONE && mt_cfg.rect_type != MULTI_POLYGON) {
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type == SINGLE &&
        (mt_cfg.roi.x_percent + mt_cfg.roi.width_percent > 100 ||
         mt_cfg.roi.y_percent + mt_cfg.roi.height_percent > 100 || mt_cfg.roi.x_percent < 0 ||
         mt_cfg.roi.y_percent < 0 || mt_cfg.roi.width_percent < 0 || mt_cfg.roi.height_percent < 0)) {
        return OPRT_INVALID_PARM;
    }
    if (mt_cfg.rect_type == MULTI &&
        (mt_cfg.motion_region.region_num < 0 ||
         mt_cfg.motion_region.region_num > (mt_cfg.motion_region.rows * mt_cfg.motion_region.cols) ||
         mt_cfg.motion_region.rows > MAX_motion_region_RAWS || mt_cfg.motion_region.rows < MIN_motion_region_RAWS ||
         mt_cfg.motion_region.cols > MAX_motion_region_COLS || mt_cfg.motion_region.cols < MIN_motion_region_COLS)) {
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type == MULTI && mt_cfg.motion_region.region_list == NULL) {
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type == MULTI_ZONE) {
        if (mt_cfg.multi_zone.num > TUYA_MD_MULTI_ZONE_NUM || mt_cfg.multi_zone.num < 1) {
            printf("rect_type num:%d %d\n", mt_cfg.rect_type, mt_cfg.multi_zone.num);
            return OPRT_INVALID_PARM;
        }

        for (i = 0; i < mt_cfg.multi_zone.num; i++){
            if (mt_cfg.multi_zone.zone[i].x + mt_cfg.multi_zone.zone[i].width > 100 ||
                mt_cfg.multi_zone.zone[i].y + mt_cfg.multi_zone.zone[i].height > 100  || 
                mt_cfg.multi_zone.zone[i].x < 0 || mt_cfg.multi_zone.zone[i].width < 0 ||
                mt_cfg.multi_zone.zone[i].y < 0 || mt_cfg.multi_zone.zone[i].height < 0) {
                printf("multi_zone [%d], x y:%d %d, w h:%d %d\n", i, mt_cfg.multi_zone.zone[i].x,
                    mt_cfg.multi_zone.zone[i].y, mt_cfg.multi_zone.zone[i].width,
                    mt_cfg.multi_zone.zone[i].height);
                return OPRT_INVALID_PARM;
            }
        }
    }

    if (mt_cfg.rect_type == MULTI_POLYGON) {
        if (mt_cfg.multi_polygon.polygon_num < 1 || mt_cfg.multi_polygon.polygon_num > TUYA_MD_MULTI_POLYGON_NUM) {
            printf("rect_type num:%d %d failed\n", mt_cfg.rect_type, mt_cfg.multi_polygon.polygon_num);
            return OPRT_INVALID_PARM;
        }
        TUYA_MD_POLYGON_T *p_polygon = mt_cfg.multi_polygon.polygon_list;
        for (j = 0; j < mt_cfg.multi_polygon.polygon_num; j++) {
            // 支持的多边形为：三角形~八边形
            if (p_polygon[j].num > TUYA_MD_POLYGON_NUM || p_polygon[j].num < 3) {
                printf("rect_type num:%d %d\n", mt_cfg.rect_type, p_polygon[j].num);
                return OPRT_INVALID_PARM;
            }
            for (i = 0;i< p_polygon[j].num; i++){
                if (p_polygon[j].point[i].x > 100 || p_polygon[j].point[i].y > 100 || 
                    p_polygon[j].point[i].x < 0 || p_polygon[j].point[i].y < 0) {
                    printf("polygon [%d], x y:%d %d\n", i, p_polygon[j].point[i].x, p_polygon[j].point[i].y);
                    return OPRT_INVALID_PARM;
                }
            }
        }
    }

    if (mt_cfg.y_thd < 1 || mt_cfg.y_thd > 255 || mt_cfg.sensitivity < MIN_SENSITIVITY ||
        mt_cfg.sensitivity > MAX_SENSITIVITY || mt_cfg.tracking_enable < 0 || mt_cfg.tracking_enable > 1) {
        return OPRT_INVALID_PARM;
    }

    g_motion_ctrl.is_first = 1;
    g_motion_ctrl.md_counting = 0;
    g_motion_ctrl.md_no_motion_timeout = 10;
    g_motion_ctrl.latest_motion_time = 0;

    INT_T buffer_size = mt_cfg.frame_w * mt_cfg.frame_h;

    g_motion_spec.frame_w = mt_cfg.frame_w;
    g_motion_spec.frame_h = mt_cfg.frame_h;

    g_motion_spec.y_thd = mt_cfg.y_thd;
    g_motion_spec.sensitivity = mt_cfg.sensitivity;

    g_motion_spec.rect_type = mt_cfg.rect_type;
    g_motion_spec.roi = mt_cfg.roi;

    if (g_motion_spec.rect_type == MULTI) {
        g_motion_spec.motion_region.rows = mt_cfg.motion_region.rows;
        g_motion_spec.motion_region.cols = mt_cfg.motion_region.cols;
        g_motion_spec.motion_region.region_num = mt_cfg.motion_region.region_num;

        g_motion_spec.motion_region.region_list = (TUYA_AI_RECT_T *)tal_malloc(
            sizeof(TUYA_AI_RECT_T) * g_motion_spec.motion_region.rows * g_motion_spec.motion_region.cols);
        TY_CHECK_POINTER(g_motion_spec.motion_region.region_list, -1);

        memcpy(g_motion_spec.motion_region.region_list, mt_cfg.motion_region.region_list,
               sizeof(TUYA_AI_RECT_T) * g_motion_spec.motion_region.region_num);

        g_motion_spec.row_step = g_motion_spec.frame_w / mt_cfg.motion_region.rows;
        g_motion_spec.col_step = g_motion_spec.frame_h / mt_cfg.motion_region.cols;
    } else if (g_motion_spec.rect_type == MULTI_ZONE) {
        g_motion_spec.p_coverage_frame = (UCHAR_T *)tal_malloc(sizeof(UCHAR_T) * buffer_size);
        TY_CHECK_POINTER(g_motion_spec.p_coverage_frame, -1);
        g_motion_spec.multi_zone = mt_cfg.multi_zone;
        memset(g_motion_spec.p_coverage_frame, 0, sizeof(UCHAR_T) * buffer_size);
        UCHAR_T *p = g_motion_spec.p_coverage_frame;
        for (j = 1; j < mt_cfg.frame_h-1; j++) {
            for (i = 1; i < mt_cfg.frame_w-1; i++) {
                if ((__is_in_multi_zone(i, j) == 1)) {
                    p[mt_cfg.frame_w*j+i] = 1;
                }
            }
        }
    } else if (g_motion_spec.rect_type == MULTI_POLYGON) {
        g_motion_spec.p_coverage_frame = (UCHAR_T *)tal_malloc(sizeof(UCHAR_T) * buffer_size);
        TY_CHECK_POINTER(g_motion_spec.p_coverage_frame, -1);
        memset(g_motion_spec.p_coverage_frame, 0, sizeof(UCHAR_T) * buffer_size);
        UCHAR_T *p = g_motion_spec.p_coverage_frame;
        __multi_polygon_mask(p, &mt_cfg.multi_polygon);

        g_motion_spec.multi_polygon.polygon_num = mt_cfg.multi_polygon.polygon_num;
        memcpy(g_motion_spec.multi_polygon.polygon_list, mt_cfg.multi_polygon.polygon_list, sizeof(TUYA_MD_POLYGON_T)*mt_cfg.multi_polygon.polygon_num);
    }

    g_motion_spec.tracking_enable = mt_cfg.tracking_enable;

    g_motion_spec.pre_frame = (UCHAR_T *)tal_malloc(sizeof(UCHAR_T) * buffer_size);
    TY_CHECK_POINTER(g_motion_spec.pre_frame, -1);
    g_motion_spec.curr_frame = (UCHAR_T *)tal_malloc(sizeof(UCHAR_T) * buffer_size);
    TY_CHECK_POINTER(g_motion_spec.curr_frame, -1);
    g_motion_spec.seg = (UCHAR_T *)tal_malloc(sizeof(UCHAR_T) * buffer_size);
    TY_CHECK_POINTER(g_motion_spec.seg, -1);
    g_motion_spec.backgroundImg = (UCHAR_T *)tal_malloc(buffer_size * sizeof(UCHAR_T));
    TY_CHECK_POINTER(g_motion_spec.backgroundImg, -1);

    // need when tracking
    if (g_motion_spec.tracking_enable == 1) {
        g_motion_spec.seg_max = (UCHAR_T *)tal_malloc(sizeof(UCHAR_T) * buffer_size);
        TY_CHECK_POINTER(g_motion_spec.seg_max, -1);
        g_motion_spec.labelImg = (UCHAR_T *)tal_malloc(buffer_size * sizeof(UCHAR_T));
        TY_CHECK_POINTER(g_motion_spec.labelImg, -1);
        g_motion_spec.lable_equal = (UCHAR_T *)tal_malloc(mt_cfg.frame_w * mt_cfg.frame_w * sizeof(UCHAR_T));
        TY_CHECK_POINTER(g_motion_spec.lable_equal, -1);
    }

    g_motion_spec.is_init = 1;
    return OPRT_OK;
}

OPERATE_RET tuya_ipc_set_motion(TUYA_MOTION_TRACKING_CFG_T mt_cfg)
{
    int i = 0;
    int j = 0;

    if (g_motion_spec.is_init == 0){
        printf("motion not init\n");
        return OPRT_INVALID_PARM;
    }
    tal_mutex_lock(lock);

    if (g_motion_spec.pre_frame == NULL || g_motion_spec.curr_frame == NULL || g_motion_spec.seg == NULL ||
        g_motion_spec.backgroundImg == NULL) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.frame_w != g_motion_spec.frame_w || mt_cfg.frame_h != g_motion_spec.frame_h) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type != SINGLE && mt_cfg.rect_type != MULTI && mt_cfg.rect_type != MULTI_ZONE && mt_cfg.rect_type != MULTI_POLYGON) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type == SINGLE &&
        (mt_cfg.roi.x_percent + mt_cfg.roi.width_percent > 100 ||
         mt_cfg.roi.y_percent + mt_cfg.roi.height_percent > 100 || mt_cfg.roi.x_percent < 0 ||
         mt_cfg.roi.y_percent < 0 || mt_cfg.roi.width_percent < 0 || mt_cfg.roi.height_percent < 0)) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }
    if (mt_cfg.rect_type == MULTI &&
        (mt_cfg.motion_region.region_num < 0 ||
         mt_cfg.motion_region.region_num > mt_cfg.motion_region.rows * mt_cfg.motion_region.cols ||
         mt_cfg.motion_region.rows > MAX_motion_region_RAWS || mt_cfg.motion_region.rows < MIN_motion_region_RAWS ||
         mt_cfg.motion_region.cols > MAX_motion_region_COLS || mt_cfg.motion_region.cols < MIN_motion_region_COLS)) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }

    if (mt_cfg.rect_type == MULTI && mt_cfg.motion_region.region_list == NULL) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }
    if (mt_cfg.rect_type == MULTI_ZONE) {
        if (mt_cfg.multi_zone.num > TUYA_MD_MULTI_ZONE_NUM || mt_cfg.multi_zone.num < 1) {
            printf("rect_type.num:%d %d\n", mt_cfg.rect_type, mt_cfg.multi_zone.num);
            tal_mutex_unlock(lock);
            return OPRT_INVALID_PARM;
        }
        for (i = 0; i < mt_cfg.multi_zone.num; i++) {
            if (mt_cfg.multi_zone.zone[i].x + mt_cfg.multi_zone.zone[i].width > 100 ||
                mt_cfg.multi_zone.zone[i].y + mt_cfg.multi_zone.zone[i].height > 100 ||
                mt_cfg.multi_zone.zone[i].x < 0 || mt_cfg.multi_zone.zone[i].width < 0 ||
                mt_cfg.multi_zone.zone[i].y < 0 || mt_cfg.multi_zone.zone[i].height < 0) {
                printf("multi_zone [%d], x y:%d %d, w h:%d %d\n", i, mt_cfg.multi_zone.zone[i].x,
                       mt_cfg.multi_zone.zone[i].y, mt_cfg.multi_zone.zone[i].width,
                       mt_cfg.multi_zone.zone[i].height);
                tal_mutex_unlock(lock);
                return OPRT_INVALID_PARM;
            }
        }
    }

    if (mt_cfg.rect_type == MULTI_POLYGON) {
        if (mt_cfg.multi_polygon.polygon_num < 1 || mt_cfg.multi_polygon.polygon_num > TUYA_MD_MULTI_POLYGON_NUM) {
            printf("rect_type num:%d %d failed\n", mt_cfg.rect_type, mt_cfg.multi_polygon.polygon_num);
            tal_mutex_unlock(lock);
            return OPRT_INVALID_PARM;
        }
        TUYA_MD_POLYGON_T *p_polygon = mt_cfg.multi_polygon.polygon_list;
        for (j = 0; j < mt_cfg.multi_polygon.polygon_num; j++) {
            // 支持的多边形为：三角形~八边形
            if (p_polygon[j].num > TUYA_MD_POLYGON_NUM || p_polygon[j].num < 3) {
                printf("rect_type num:%d %d\n", mt_cfg.rect_type, p_polygon[j].num);
                tal_mutex_unlock(lock);
                return OPRT_INVALID_PARM;
            }
            for (i = 0;i< p_polygon[j].num; i++){
                if (p_polygon[j].point[i].x > 100 || p_polygon[j].point[i].y > 100 || 
                    p_polygon[j].point[i].x < 0 || p_polygon[j].point[i].y < 0) {
                    printf("polygon [%d], x y:%d %d\n", i, p_polygon[j].point[i].x, p_polygon[j].point[i].y);
                    tal_mutex_unlock(lock);
                    return OPRT_INVALID_PARM;
                }
            }
        }
    }

    if (mt_cfg.y_thd < 1 || mt_cfg.y_thd > 255 || mt_cfg.sensitivity < MIN_SENSITIVITY ||
        mt_cfg.sensitivity > MAX_SENSITIVITY || mt_cfg.tracking_enable < 0 || mt_cfg.tracking_enable > 1) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }
    g_motion_spec.frame_w = mt_cfg.frame_w;
    g_motion_spec.frame_h = mt_cfg.frame_h;

    g_motion_spec.y_thd = mt_cfg.y_thd;
    g_motion_spec.sensitivity = mt_cfg.sensitivity;

    g_motion_spec.rect_type = mt_cfg.rect_type;

    g_motion_spec.roi = mt_cfg.roi;

    if (g_motion_spec.rect_type == MULTI) {
        g_motion_spec.motion_region.rows = mt_cfg.motion_region.rows;
        g_motion_spec.motion_region.cols = mt_cfg.motion_region.cols;
        g_motion_spec.motion_region.region_num = mt_cfg.motion_region.region_num;

        memcpy(g_motion_spec.motion_region.region_list, mt_cfg.motion_region.region_list,
               sizeof(TUYA_AI_RECT_T) * g_motion_spec.motion_region.region_num);
    } else if (g_motion_spec.rect_type == MULTI_ZONE) {
        g_motion_spec.multi_zone = mt_cfg.multi_zone;
        memset(g_motion_spec.p_coverage_frame, 0, sizeof(UCHAR_T) * mt_cfg.frame_w * mt_cfg.frame_h);
        UCHAR_T *p = g_motion_spec.p_coverage_frame;
        for (j = 1; j < mt_cfg.frame_h-1; j++) {
            for (i = 1; i < mt_cfg.frame_w-1; i++) {
                if ((__is_in_multi_zone(i, j) == 1)) {
                    p[mt_cfg.frame_w*j+i] = 1;
                }
            }
        }
    } else if (g_motion_spec.rect_type == MULTI_POLYGON) {
        if ((g_motion_spec.multi_polygon.polygon_num != mt_cfg.multi_polygon.polygon_num) ||
        (0 != memcmp(g_motion_spec.multi_polygon.polygon_list, mt_cfg.multi_polygon.polygon_list, sizeof(TUYA_MD_POLYGON_T)*mt_cfg.multi_polygon.polygon_num))){
            printf("multi_polygon num:%d -> %d\n", g_motion_spec.multi_polygon.polygon_num, mt_cfg.multi_polygon.polygon_num);
            g_motion_spec.multi_polygon.polygon_num = mt_cfg.multi_polygon.polygon_num;
            memcpy(g_motion_spec.multi_polygon.polygon_list, mt_cfg.multi_polygon.polygon_list, sizeof(TUYA_MD_POLYGON_T)*mt_cfg.multi_polygon.polygon_num);

            memset(g_motion_spec.p_coverage_frame, 0, sizeof(UCHAR_T) * mt_cfg.frame_w * mt_cfg.frame_h);
            UCHAR_T *p = g_motion_spec.p_coverage_frame;
            __multi_polygon_mask(p, &mt_cfg.multi_polygon);
            //__mask_show();
        } else {
            printf("multi_polygon cfg is same %d\n", g_motion_spec.multi_polygon.polygon_num);
        }
    }

    g_motion_spec.tracking_enable = mt_cfg.tracking_enable;

    tal_mutex_unlock(lock);

    return OPRT_OK;
}

void tuya_ipc_get_motion(TUYA_MOTION_TRACKING_CFG_T *mt_cfg)
{
    if (g_motion_spec.is_init == 0){
        printf("motion not init\n");
        return;
    }
    tal_mutex_lock(lock);

    mt_cfg->frame_w = g_motion_spec.frame_w;
    mt_cfg->frame_h = g_motion_spec.frame_h;

    mt_cfg->y_thd = g_motion_spec.y_thd;
    mt_cfg->sensitivity = g_motion_spec.sensitivity;
    mt_cfg->rect_type = g_motion_spec.rect_type;
    mt_cfg->roi = g_motion_spec.roi;
    mt_cfg->multi_zone = g_motion_spec.multi_zone;
    if (g_motion_spec.rect_type == MULTI) {
        mt_cfg->motion_region.rows = g_motion_spec.motion_region.rows;
        mt_cfg->motion_region.cols = g_motion_spec.motion_region.cols;
        mt_cfg->motion_region.region_num = g_motion_spec.motion_region.region_num;
        mt_cfg->motion_region.region_list = g_motion_spec.motion_region.region_list;
    } else if (g_motion_spec.rect_type == MULTI_POLYGON) {
        mt_cfg->multi_polygon.polygon_num = g_motion_spec.multi_polygon.polygon_num;
        memcpy(mt_cfg->multi_polygon.polygon_list, g_motion_spec.multi_polygon.polygon_list, sizeof(TUYA_MD_POLYGON_T)*g_motion_spec.multi_polygon.polygon_num);
    }

    mt_cfg->tracking_enable = g_motion_spec.tracking_enable;

    tal_mutex_unlock(lock);
}

void tuya_ipc_motion_release()
{
    if (g_motion_spec.is_init == 0){
        printf("motion not init\n");
        return;
    }
    tal_mutex_release(lock);

    if (g_motion_spec.backgroundImg != NULL) {
        tal_free(g_motion_spec.backgroundImg);
        g_motion_spec.backgroundImg = NULL;
    }

    if (g_motion_spec.curr_frame != NULL) {
        tal_free(g_motion_spec.curr_frame);
        g_motion_spec.curr_frame = NULL;
    }

    if (g_motion_spec.pre_frame != NULL) {
        tal_free(g_motion_spec.pre_frame);
        g_motion_spec.pre_frame = NULL;
    }
    if (g_motion_spec.seg != NULL) {
        tal_free(g_motion_spec.seg);
        g_motion_spec.seg = NULL;
    }

    if (g_motion_spec.p_coverage_frame != NULL) {
        tal_free(g_motion_spec.p_coverage_frame);
        g_motion_spec.p_coverage_frame = NULL;
    }

    if (g_motion_spec.tracking_enable == 1) {
        if (g_motion_spec.lable_equal != NULL) {
            tal_free(g_motion_spec.lable_equal);
            g_motion_spec.lable_equal = NULL;
        }

        if (g_motion_spec.labelImg != NULL) {
            tal_free(g_motion_spec.labelImg);
            g_motion_spec.labelImg = NULL;
        }

        if (g_motion_spec.seg_max != NULL) {
            tal_free(g_motion_spec.seg_max);
            g_motion_spec.seg_max = NULL;
        }
    }

    if (g_motion_spec.motion_region.region_list != NULL) {
        tal_free(g_motion_spec.motion_region.region_list);
        g_motion_spec.motion_region.region_list = NULL;
    }

    g_motion_spec.frame_w = 0;
    g_motion_spec.frame_h = 0;

    g_motion_spec.y_thd = 0;

    g_motion_spec.sensitivity = 1;

    g_motion_spec.rect_type = SINGLE;
    g_motion_spec.roi.x_percent = 0;
    g_motion_spec.roi.y_percent = 0;
    g_motion_spec.roi.width_percent = 0;
    g_motion_spec.roi.height_percent = 0;
    g_motion_spec.motion_region.rows = 0;
    g_motion_spec.motion_region.cols = 0;
    g_motion_spec.motion_region.region_num = 0;

    g_motion_spec.tracking_enable = 0;

    g_motion_ctrl.is_first = 1;
    g_motion_ctrl.md_counting = 0;
    g_motion_ctrl.md_no_motion_timeout = 10;
    g_motion_ctrl.latest_motion_time = 0;
}

OPERATE_RET tuya_ipc_get_background_frame()
{
    tal_mutex_lock(lock);
    if (g_motion_spec.pre_frame == NULL || g_motion_spec.curr_frame == NULL || g_motion_spec.seg == NULL ||
        g_motion_spec.backgroundImg == NULL) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }

    tuya_motion_tracking_spec tempcfg = g_motion_spec;
    tal_mutex_unlock(lock);

    memcpy(tempcfg.backgroundImg, tempcfg.pre_frame, tempcfg.frame_w * tempcfg.frame_h);

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_background_sub(INT_T *background_motion_flag)
{
    *background_motion_flag = 0;
    tal_mutex_lock(lock);
    if (g_motion_spec.pre_frame == NULL || g_motion_spec.curr_frame == NULL || g_motion_spec.seg == NULL ||
        g_motion_spec.backgroundImg == NULL) {
        tal_mutex_unlock(lock);
        return OPRT_INVALID_PARM;
    }
    tuya_motion_tracking_spec tempcfg = g_motion_spec;
    tal_mutex_unlock(lock);

    INT_T j = 0;
    INT_T i = 0;
    INT_T buff_size = tempcfg.frame_w * tempcfg.frame_h;
    INT_T counting = 0;

    UCHAR_T *pAlg = NULL;
    UCHAR_T *pBackgroundImg = NULL;

    pAlg = tempcfg.pre_frame;
    pBackgroundImg = tempcfg.backgroundImg;

    for (j = 0; j < tempcfg.frame_h; j++) {
        for (i = 0; i < tempcfg.frame_w; i++) {
            // filter border
            if (j != 0 && i != 0 && i != tempcfg.frame_w - 1 && j != tempcfg.frame_h - 1) {
                // do thres
                if (abs(pAlg[i] - pBackgroundImg[i]) >= tempcfg.y_thd) {
                    counting++;
                }
            }
        }
        pAlg += tempcfg.frame_w;
        pBackgroundImg += tempcfg.frame_w;
    }

    if (counting * 10000 / buff_size > 500) {
        *background_motion_flag = 1;
    }

    pAlg = NULL;
    pBackgroundImg = NULL;

    return OPRT_OK;
}

// MULTI_ZONE 判断一个点是否在有效区中, i,j 是像素点，p_info中的点是百分比
STATIC INT_T __is_in_multi_zone(INT_T x, INT_T y)
{
    INT_T x1 = x * 100 / g_motion_spec.frame_w;
    INT_T y1 = y * 100 / g_motion_spec.frame_h;
    TUYA_MULTI_ZONE_INFO_T *p_info = &g_motion_spec.multi_zone;
    INT_T num = 0;
    for (num = 0; num < p_info->num; num++) {
        if (x1 >= p_info->zone[num].x && x1 <= p_info->zone[num].x + p_info->zone[num].width &&
            y1 >= p_info->zone[num].y &&
            y1 <= p_info->zone[num].y + p_info->zone[num].height) {
            return 1;
        }
    }
    return 0;
}

/* 判断一个点是否在多边形中, i,j 是像素点.
   多边形填充规则“None Zero Mode”:判断一个点是否在多边形内的规则.从这个点引出一根“射线”，与多边形的任意若干条边相交，
   计数初始化为0，若相交处被多边形的边从左到右切过，计数+1，若相交处被多边形的边从右到左切过，计数-1，最后检查计数，
   如果是0，点在多边形外，如果非0，点在多边形内。自测过3~8边形。允许边自相交 */
STATIC INT_T __is_in_polygon(INT_T x, INT_T y, TUYA_MD_POLYGON_T *p_polygon)
{
    if (p_polygon == NULL){
        return 0;
    }

    INT_T j = p_polygon->num - 1;
    INT_T k = 0;
    INT_T zero_state = 0;

    TUYA_MD_POINT_T point = {0};
    TUYA_MD_POINT_T point_k = {0};
    TUYA_MD_POINT_T point_j = {0};
    point.x = x * 100 / g_motion_spec.frame_w;
    point.y = y * 100 / g_motion_spec.frame_h;

    for (k = 0; k < p_polygon->num; k++) {
        point_k = p_polygon->point[k];
        point_j = p_polygon->point[j];
        if (((point_k.y > point.y) != (point_j.y > point.y)) && (point.x < (point_j.x - point_k.x) * (point.y - point_k.y) / (point_j.y - point_k.y) + point_k.x)) {
            if (point_k.y > point_j.y) {
                zero_state++;
            } else {
                zero_state--;
            }
        }
        j = k;
    }

    if (zero_state != 0){
        return 1;
    }
    return 0;
}

// 动检检测
OPERATE_RET Tuya_Ipc_Frame_Diff(UCHAR_T *srcImg, INT_T *motion_flag, tuya_motion_tracking_spec tempcfg)
{
    INT_T j = 0;
    INT_T i = 0;
    INT_T k = 0;
    INT_T buff_size = tempcfg.frame_w * tempcfg.frame_h;
    INT_T counting = 0;

    UCHAR_T *pPre = NULL;
    UCHAR_T *pCurr = NULL;
    UCHAR_T *pSeg = NULL;

    *motion_flag = 0;

    if (g_motion_ctrl.is_first) {
        g_motion_ctrl.is_first = 0;
        memcpy(tempcfg.pre_frame, srcImg, buff_size);
        memcpy(tempcfg.curr_frame, srcImg, buff_size);
        return OPRT_OK;
    } else {
        // cp current frame
        memcpy(tempcfg.pre_frame, tempcfg.curr_frame, buff_size);
        memcpy(tempcfg.curr_frame, srcImg, buff_size);

        pPre = tempcfg.pre_frame + (tempcfg.roi.y_percent * tempcfg.frame_h / 100) * tempcfg.frame_w;
        pCurr = tempcfg.curr_frame + (tempcfg.roi.y_percent * tempcfg.frame_h / 100) * tempcfg.frame_w;
        pSeg = tempcfg.seg + (tempcfg.roi.y_percent * tempcfg.frame_h / 100) * tempcfg.frame_w;

        switch (tempcfg.rect_type) {
        case SINGLE: {
            int j_start = tempcfg.roi.y_percent * tempcfg.frame_h / 100;
            int j_end = (tempcfg.roi.y_percent + tempcfg.roi.height_percent) * tempcfg.frame_h / 100;
            int i_start = tempcfg.roi.x_percent * tempcfg.frame_w / 100;
            int i_end = (tempcfg.roi.x_percent + tempcfg.roi.width_percent) * tempcfg.frame_w / 100;
            for (j = j_start; j < j_end; j++) {
                for (i = i_start; i < i_end; i++) {
                    // filter border
                    if (j == 0 || i == 0 || i == tempcfg.frame_w - 1 || j == tempcfg.frame_h - 1) {
                        pSeg[i] = 0;
                    } else {

                        // do thres
                        if (abs(pPre[i] - pCurr[i]) < tempcfg.y_thd) {
                            pSeg[i] = 0;
                        } else {
                            pSeg[i] = 255;
                            counting++;
                        }
                    }
                }
                pPre += tempcfg.frame_w;
                pCurr += tempcfg.frame_w;
                pSeg += tempcfg.frame_w;
            }
            break;
        }
        case MULTI: {
            if (tempcfg.motion_region.region_num == 0 ||
                tempcfg.motion_region.region_num == tempcfg.motion_region.rows * tempcfg.motion_region.cols) {
                for (j = 0; j < tempcfg.frame_h; j++) {
                    for (i = 0; i < tempcfg.frame_w; i++) {
                        // filter border
                        if (j == 0 || i == 0 || i == tempcfg.frame_w - 1 || j == tempcfg.frame_h - 1) {
                            pSeg[i] = 0;
                        } else {
                            pSeg[i] = abs(pPre[i] - pCurr[i]);

                            // do thres
                            if (pSeg[i] < tempcfg.y_thd) {
                                pSeg[i] = 0;
                            } else {
                                pSeg[i] = 255;
                                counting++;
                            }
                        }
                    }
                    pPre += tempcfg.frame_w;
                    pCurr += tempcfg.frame_w;
                    pSeg += tempcfg.frame_w;
                }
            } else {
                for (k = 0; k < tempcfg.motion_region.region_num; k++) {
                    // 偏移到rect左上角
                    pPre = tempcfg.curr_frame +
                           tempcfg.motion_region.region_list[k].top * tempcfg.col_step * tempcfg.frame_w +
                           tempcfg.motion_region.region_list[k].left * tempcfg.row_step;
                    pCurr = tempcfg.pre_frame +
                            tempcfg.motion_region.region_list[k].top * tempcfg.col_step * tempcfg.frame_w +
                            tempcfg.motion_region.region_list[k].left * tempcfg.row_step;
                    pSeg = tempcfg.seg + tempcfg.motion_region.region_list[k].top * tempcfg.col_step * tempcfg.frame_w +
                           tempcfg.motion_region.region_list[k].left * tempcfg.row_step;
                    for (j = 0; j < tempcfg.col_step; j++) {
                        for (i = 0; i < tempcfg.row_step; i++) {
                            // filter border
                            if (j == 0 || i == 0 || i == tempcfg.frame_w - 1 || j == tempcfg.frame_h - 1) {
                                pSeg[i] = 0;
                            } else {
                                pSeg[i] = abs(pPre[i] - pCurr[i]);

                                // do thres
                                if (pSeg[i] < tempcfg.y_thd) {
                                    pSeg[i] = 0;
                                } else {
                                    pSeg[i] = 255;
                                    counting++;
                                }
                            }
                        }
                        pPre += tempcfg.frame_w;
                        pCurr += tempcfg.frame_w;
                        pSeg += tempcfg.frame_w;
                    }
                }
            }
            break;
        }
        case MULTI_ZONE:
        case MULTI_POLYGON:
        {
            pPre = tempcfg.pre_frame;
            pCurr = tempcfg.curr_frame;
            pSeg = tempcfg.seg;
            for (j = 0; j < tempcfg.frame_h; j++) {
                for (i = 0; i < tempcfg.frame_w; i++) {
                    // 非有效区域设置为0
                    if (tempcfg.p_coverage_frame[tempcfg.frame_w*j+i] == 0){
                        pSeg[i] = 0;
                    } else {
                        pSeg[i] = abs(pPre[i] - pCurr[i]);
                        // do thres                            // 两帧对应位置的值，相差30以上为0xff 否则为0
                        if (pSeg[i] < tempcfg.y_thd) {
                            pSeg[i] = 0;
                        } else {
                            pSeg[i] = 255;
                            counting++;
                        }
                    }
                }
                pPre += tempcfg.frame_w;
                pCurr += tempcfg.frame_w;
                pSeg += tempcfg.frame_w;
            }
            break;
        }
        }

        if (tempcfg.sensitivity == 1) {
            if (counting * 10000 / buff_size > 320) { // 千分之32，即460个像素变化（160*90）
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 2) {
            if (counting * 10000 / buff_size > 280) {
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 3) {
            if (counting * 10000 / buff_size > 220) {
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 4) {
            if (counting * 10000 / buff_size > 140) {
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 5) {
            if (counting * 10000 / buff_size > 70) {
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 6) {
            if (counting * 10000 / buff_size > 35) {
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 7) {
            if (counting * 10000 / buff_size > 20) { // 千分之2，即28个像素变化（160*90）
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 8) {       // 注意osd需要涂黑或隐藏掉，不然osd变化会引起误报, osd 变化影响一般为5/10000, 最多不超过20/10000
            if (counting * 10000 / buff_size > 15) { // 千分之1.5，建议yuv分辨率大于320*180，检测距离10~20m
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 9) {
            if (counting * 10000 / buff_size > 10) { // 千分之1，建议yuv分辨率大于320*180，检测距离15~25m
                *motion_flag = 1;
            }
        } else if (tempcfg.sensitivity == 10) {
            if (counting * 10000 / buff_size > 5) { // 千分之0.5，建议yuv分辨率大于320*180，检测距离20~30m
                *motion_flag = 1;
            }
        }
    }

    if (*motion_flag == 0) {
        g_motion_ctrl.md_counting = 0;
        time_t curr_time;
        time(&curr_time);

        if (g_motion_ctrl.md_no_motion_timeout < NO_MOTION_TIMEOUT) {
            // PR_DEBUG("g_motion_ctrl.md_no_motion_timeout = %d \n", g_motion_ctrl.md_no_motion_timeout);
            g_motion_ctrl.md_no_motion_timeout = (int)difftime(curr_time, g_motion_ctrl.latest_motion_time);
        }
    } else {
        time(&g_motion_ctrl.latest_motion_time);
        if (g_motion_ctrl.md_no_motion_timeout >= NO_MOTION_TIMEOUT) {
            g_motion_ctrl.md_counting++;
            if (g_motion_ctrl.md_counting == MULTI_FRAME_FILTER) {
                // PR_DEBUG("md_counting = %d \n", g_motion_ctrl.md_counting);
                g_motion_ctrl.md_no_motion_timeout = 0;
                g_motion_ctrl.md_counting = 0;
            } else {
                *motion_flag = 0;
            }
        }
    }

    pPre = NULL;
    pCurr = NULL;
    pSeg = NULL;

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_motion(UCHAR_T *srcImg, INT_T *motion_flag, TUYA_POINT_T *motion_point)
{
    OPERATE_RET ret = -1;

    if (g_motion_spec.is_init == 0){
        printf("motion not init\n");
        return OPRT_INVALID_PARM;
    }

    tal_mutex_lock(lock);

    tuya_motion_tracking_spec tempcfg = g_motion_spec;

    tal_mutex_unlock(lock);
    if (srcImg == NULL || tempcfg.pre_frame == NULL || tempcfg.curr_frame == NULL || tempcfg.seg == NULL ||
        tempcfg.backgroundImg == NULL) {
        return OPRT_INVALID_PARM;
    }

    // detect
    ret = Tuya_Ipc_Frame_Diff(srcImg, motion_flag, tempcfg);

    if (ret < 0) {
        return ret;
    }

    // 移动追踪, 求中心点
    if (tempcfg.tracking_enable && (*motion_flag)) {
        Tuya_Ipc_IMP_Dilation(tempcfg.seg, tempcfg.frame_w, tempcfg.frame_h, tempcfg.seg);
        // find conn reg
        motion_point->x = 0;
        motion_point->y = 0;
        ret = Tuya_Ipc_Max_Conn_Reg(motion_point, tempcfg);
        if (ret < 0) {
            return ret;
        }
    }

    return OPRT_OK;
}
