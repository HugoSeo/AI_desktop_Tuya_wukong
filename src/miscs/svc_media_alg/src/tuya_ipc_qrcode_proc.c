/*********************************************************************************
* Copyright(C),2019, TUYA www.tuya.com

* FileName:		tuya_ipc_qrcode_proc.c
* Note		
* Version		V1.0.0
* Data			2019.04
**********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "tuya_ipc_qrcode_proc.h"
#include "tuya_ipc_img_proc.h"
#include "tal_memory.h"

void Tuya_Ipc_QRCode_Binarization(unsigned char *srcImg, int width, int height, int thres)
{
    int i, j;

    for (j = 1; j < height - 1; j++)
    {
        for (i = 1; i < width - 1; i++)
        {

            if (srcImg[j*width + i] < thres)
            {
                srcImg[j*width + i] = 0;

            }
            else
            {
                srcImg[j*width + i] = 255;
            }
        }
    }
}


void Tuya_Ipc_QRCode_Erosion(unsigned char *srcImg, int width, int height, unsigned char *dstImg)
{

    int mid = (3 + 1) / 2 - 1;
    int i = mid;
    int j = mid;
    int m = 0;
    int n = 0;
    unsigned char val = 0;

    for (i = mid; i < height - mid - 1; i++)
    {
        for (j = mid; j < width - mid - 1; j++)
        {
            for (m = 0; m < 3; m++)
            {
                for (n = 0; n < 3; n++)
                {
                    if (m == mid && n == mid)continue;
                    val &= srcImg[(i + m) * width + j + n];
                }
            }

            srcImg[i * width + j] = val;
            val = 255;
        }
    }
}

OPERATE_RET tuya_ipc_qrcode_enhance(UCHAR_T *in_data, INT_T in_w, INT_T in_h,
                                    UCHAR_T **out_data, INT_T* out_w, INT_T* out_h, 
                                    INT_T binary_thres, BOOL_T scale_flag)
{
    if (in_data == NULL)
    {
        return OPRT_INVALID_PARM;
    }
    if ((in_w <= 0) || (in_h <= 0))
    {
        return OPRT_INVALID_PARM;
    }

    if (binary_thres < 100 || binary_thres > 150 || scale_flag < 0 || scale_flag > 1)
    {
        return OPRT_INVALID_PARM;
    }

    //scale
    INT_T j = 0;
    INT_T cut_w = (in_w - (in_w - in_h));
    INT_T cut_h = in_h;
    INT_T scale_size = 1;
    if (scale_flag)
    {
        scale_size=2;
    }
    

    TUYA_IMG_RESIZE_PARA_T paras;
    paras.src_width = cut_w;
    paras.src_height = cut_h;
    paras.dst_width = cut_w * scale_size;
    paras.dst_height = cut_h * scale_size;
    paras.resize_type = CUBIC;
    paras.img_type = Y;

    UCHAR_T* y8data_cut = (UCHAR_T*)tal_malloc(cut_w * cut_h);
    UCHAR_T* y8_out = (UCHAR_T*)tal_malloc(paras.dst_width * paras.dst_height);

    while (j < in_h) {
        memcpy(y8data_cut + j*cut_w, in_data + j*in_w + (in_w - in_h) / 2, cut_w);
        j++;
    }
    //resize
    if (!scale_flag)
    {
        memcpy(y8_out, y8data_cut, paras.dst_width * paras.dst_height);
    }
    else
    {
        tuya_ipc_img_resize(y8data_cut, paras, y8_out);
    }

    //binary & erosion
    Tuya_Ipc_QRCode_Binarization(y8_out, paras.dst_width, paras.dst_height, binary_thres);
    Tuya_Ipc_QRCode_Erosion(y8_out, paras.dst_width, paras.dst_height, y8_out);

    *out_data = y8_out;
    *out_w = paras.dst_width;
    *out_h = paras.dst_height;

    if (y8data_cut!=NULL)
    {
        tal_free(y8data_cut);
        y8data_cut = NULL;
    }

    return 0;
}
