/*********************************************************************************
* Copyright(C),2019, TUYA www.tuya.com

* FileName:		tuya_ipc_img_proc.c
* Note		
* Version		V1.0.0
* Data			2019.04
**********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "tuya_ipc_img_proc.h"
#include "tal_memory.h" 
#define ABS(x) (x > 0 ? (x) : (-x))
#define clip(a, min, max) (a > max ? max : (a > min ? a : min))


typedef struct _IMG_SCALE_SPEC
{
    INT_T *T1;
    INT_T *T2;
    INT_T *T3;
    INT_T *T4;

    INT_T *SSx1;
    INT_T *SSy1;
    INT_T *SSx2;
    INT_T *SSy2;
    INT_T *SSx3;
    INT_T *SSy3;
    INT_T *SSx4;
    INT_T *SSy4;

}IMG_SCALE_SPEC;

STATIC IMG_SCALE_SPEC g_scale_spec;

FLOAT_T S(FLOAT_T x)
{
    if (ABS(x) < 1.0)
    {
        return (1.0 - 2.0*x*x + ABS(x*x*x));
    }
    else if (ABS(x) >= 1.0 && ABS(x) < 2)
    {
        return (4.0 - 8.0*ABS(x) + 5.0*x*x - ABS(x*x*x));
    }
    else
    {
        return 0;
    }
}

void Convolution(INT_T *A, INT_T *B, INT_T *C, INT_T *D)
{
    INT_T a, b, c, d;
    a = A[0] * B[0] + A[1] * B[4] + A[2] * B[8] + A[3] * B[12];
    b = A[0] * B[1] + A[1] * B[5] + A[2] * B[9] + A[3] * B[13];
    c = A[0] * B[2] + A[1] * B[6] + A[2] * B[10] + A[3] * B[14];
    d = A[0] * B[3] + A[1] * B[7] + A[2] * B[11] + A[3] * B[15];
    *D = a*C[0] + b*C[1] + c*C[2] + d*C[3];
}

void Cubic_Scale_Init(INT_T dst_width, INT_T dst_height)
{
    INT_T outWidth_cr = dst_width / 2;
    INT_T outHeight_cr  = dst_height / 2;

    g_scale_spec.T1 = (INT_T*)tal_malloc(outWidth_cr*sizeof(INT_T));
    g_scale_spec.T2 = (INT_T*)tal_malloc(outHeight_cr*sizeof(INT_T));

    g_scale_spec.T3 = (INT_T*)tal_malloc(dst_width*sizeof(INT_T));
    g_scale_spec.T4 = (INT_T*)tal_malloc(dst_height*sizeof(INT_T));

    g_scale_spec.SSx1 = (INT_T*)tal_malloc(dst_width*sizeof(INT_T));
    g_scale_spec.SSx2 = (INT_T*)tal_malloc(dst_width*sizeof(INT_T));
    g_scale_spec.SSx3 = (INT_T*)tal_malloc(dst_width*sizeof(INT_T));
    g_scale_spec.SSx4 = (INT_T*)tal_malloc(dst_width*sizeof(INT_T));
    g_scale_spec.SSy1 = (INT_T*)tal_malloc(dst_height*sizeof(INT_T));
    g_scale_spec.SSy2 = (INT_T*)tal_malloc(dst_height*sizeof(INT_T));
    g_scale_spec.SSy3 = (INT_T*)tal_malloc(dst_height*sizeof(INT_T));
    g_scale_spec.SSy4 = (INT_T*)tal_malloc(dst_height*sizeof(INT_T));
}

void Cubic_Scale_Release()
{
    if (g_scale_spec.T1 != NULL)
    {
        tal_free(g_scale_spec.T1);
        g_scale_spec.T1 = NULL;
    }
    if (g_scale_spec.T2 != NULL)
    {
        tal_free(g_scale_spec.T2);
        g_scale_spec.T2 = NULL;
    }
    if (g_scale_spec.T3 != NULL)
    {
        tal_free(g_scale_spec.T3);
        g_scale_spec.T3 = NULL;
    }
    if (g_scale_spec.T4 != NULL)
    {
        tal_free(g_scale_spec.T4);
        g_scale_spec.T4 = NULL;
    }
    if (g_scale_spec.SSx1 != NULL)
    {
        tal_free(g_scale_spec.SSx1);
        g_scale_spec.SSx1 = NULL;
    }
    if (g_scale_spec.SSx2 != NULL)
    {
        tal_free(g_scale_spec.SSx2);
        g_scale_spec.SSx2 = NULL;
    }
    if (g_scale_spec.SSx3 != NULL)
    {
        tal_free(g_scale_spec.SSx3);
        g_scale_spec.SSx3 = NULL;
    }
    if (g_scale_spec.SSx4 != NULL)
    {
        tal_free(g_scale_spec.SSx4);
        g_scale_spec.SSx4 = NULL;
    }
    if (g_scale_spec.SSy1 != NULL)
    {
        tal_free(g_scale_spec.SSy1);
        g_scale_spec.SSy1 = NULL;
    }
    if (g_scale_spec.SSy2 != NULL)
    {
        tal_free(g_scale_spec.SSy2);
        g_scale_spec.SSy2 = NULL;
    }
    if (g_scale_spec.SSy3 != NULL)
    {
        tal_free(g_scale_spec.SSy3);
        g_scale_spec.SSy3 = NULL;
    }
    if (g_scale_spec.SSy4 != NULL)
    {
        tal_free(g_scale_spec.SSy4);
        g_scale_spec.SSy4 = NULL;
    }
}

OPERATE_RET Cubic_Scale(UCHAR_T *srcImg, TUYA_IMG_RESIZE_PARA_T paras, UCHAR_T *dstImg)
{
	UCHAR_T *pYIn = NULL;
    UCHAR_T *pCbIn = NULL;
    UCHAR_T *pCrIn = NULL;
	UCHAR_T *pYOut = NULL;
    UCHAR_T *pCbOut = NULL;
    UCHAR_T *pCrOut = NULL;
    UCHAR_T *p1 = NULL;

    INT_T lineY, lineCb;
    lineY = paras.src_width;
    lineCb = paras.src_width / 2;

    FLOAT_T rx = (FLOAT_T)paras.dst_width / (FLOAT_T)paras.src_width;
    FLOAT_T ry = (FLOAT_T)paras.dst_height / (FLOAT_T)paras.src_height;
    INT_T x = 0, y = 0;
    INT_T yy = 0;
    INT_T outWidth_cr = paras.dst_width / 2;
    INT_T outHeight_cr = paras.dst_height / 2;
    INT_T srcWidth_cr = paras.src_width / 2;
    INT_T srcHeight_cr = paras.src_height / 2;

    FLOAT_T u, v;
    INT_T   AA[4], BB[16], CC[4], tt2;

	pYIn = srcImg;
	pYOut = dstImg;
    for (x = 0; x <paras.dst_width; x++)
    {
        u = (((FLOAT_T)x / rx) - (INT_T)((FLOAT_T)x / rx));
        g_scale_spec.T3[x] = clip((FLOAT_T)x / rx, 1, paras.src_width - 3);
        g_scale_spec.SSx1[x] = (INT_T)((FLOAT_T)(S(u + 1)*256.0));
        g_scale_spec.SSx2[x] = (INT_T)((FLOAT_T)(S(u)*256.0));
        g_scale_spec.SSx3[x] = (INT_T)((FLOAT_T)(S(u - 1)*256.0));
        g_scale_spec.SSx4[x] = (INT_T)((FLOAT_T)(S(u - 2)*256.0));
    }
    for (y = 0; y < paras.dst_height; y++)
    {
        v = (((FLOAT_T)y / ry) - (INT_T)((FLOAT_T)y / ry));
        g_scale_spec.T4[y] = clip((FLOAT_T)y / ry, 1, paras.src_height - 3);
        g_scale_spec.T4[y] *= lineY;
        g_scale_spec.SSy1[y] = (INT_T)((FLOAT_T)(S(v + 1)*256.0));
        g_scale_spec.SSy2[y] = (INT_T)((FLOAT_T)(S(v)*256.0));
        g_scale_spec.SSy3[y] = (INT_T)((FLOAT_T)(S(v - 1)*256.0));
        g_scale_spec.SSy4[y] = (INT_T)((FLOAT_T)(S(v - 2)*256.0));
    }
    for (x = 0; x < paras.dst_width / 2; x++)
    {
        g_scale_spec.T1[x] = clip((FLOAT_T)x / rx, 0, srcWidth_cr - 1);
    }
    for (y = 0; y < paras.dst_height / 2; y++)
    {
        g_scale_spec.T2[y] = clip((FLOAT_T)y / ry, 0, srcHeight_cr - 1);
        g_scale_spec.T2[y] *= lineCb;
    }
    for (y = 0; y < paras.dst_height; y++)
    {
        yy = g_scale_spec.T4[y];
        CC[0] = g_scale_spec.SSy1[y];
        CC[1] = g_scale_spec.SSy2[y];
        CC[2] = g_scale_spec.SSy3[y];
        CC[3] = g_scale_spec.SSy4[y];

        for (x = 0; x <paras.dst_width; x++)
        {
            p1 = &pYIn[yy + g_scale_spec.T3[x]];

            AA[0] = g_scale_spec.SSx1[x];
            AA[1] = g_scale_spec.SSx2[x];
            AA[2] = g_scale_spec.SSx3[x];
            AA[3] = g_scale_spec.SSx4[x];

            BB[0] = *(p1 - lineY - 1);
            BB[1] = *(p1 - 1);
            BB[2] = *(p1 + lineY - 1);
            BB[3] = *(p1 + lineY + lineY - 1);
            BB[4] = *(p1 - lineY);
            BB[5] = *p1;
            BB[6] = *(p1 + lineY);
            BB[7] = *(p1 + lineY + lineY);
            BB[8] = *(p1 - lineY + 1);
            BB[9] = *(p1 + 1);
            BB[10] = *(p1 + lineY + 1);
            BB[11] = *(p1 + lineY + lineY + 1);
            BB[12] = *(p1 - lineY + 2);
            BB[13] = *(p1 + 2);
            BB[14] = *(p1 + lineY + 2);
            BB[15] = *(p1 + lineY + lineY + 2);
            Convolution(AA, BB, CC, &tt2);
            pYOut[x] = clip((tt2 >> 16), 0, 255);
        }
        pYOut += paras.dst_width;
    }
    if (paras.img_type ==YUV)
    {
        pYIn = srcImg;
	    pYOut = dstImg;
		pCbIn = pYIn + paras.src_width * paras.src_height;
		pCrIn = pYIn + paras.src_width * paras.src_height * 5 / 4;
		pCbOut = pYOut + paras.dst_width * paras.dst_height;
		pCrOut = pYOut + paras.dst_width * paras.dst_height * 5 / 4;
        for (y = 0; y < outHeight_cr; y++)
        {
            tt2 = g_scale_spec.T2[y];
            for (x = 0; x < outWidth_cr; x++)
            {
                pCbOut[x] = pCbIn[tt2 + g_scale_spec.T1[x]];
                pCrOut[x] = pCrIn[tt2 + g_scale_spec.T1[x]];
            }
            pCbOut += paras.dst_width / 2;
            pCrOut += paras.dst_width / 2;
        }
    }
    return OPRT_OK;
}

OPERATE_RET Linear_Scale(UCHAR_T *in_data, TUYA_IMG_RESIZE_PARA_T paras, UCHAR_T *out_data)
{
    INT_T y = 0;
    INT_T x = 0;
    INT_T xx = 0;
    INT_T yy = 0;

    UCHAR_T *pSrc = NULL;
    UCHAR_T *pDst = NULL;
    UCHAR_T *pSrcU = NULL;
    UCHAR_T *pSrcV = NULL;
    UCHAR_T *pDstU = NULL;
    UCHAR_T *pDstV = NULL;

    FLOAT_T scale_w = (FLOAT_T)paras.dst_width / (FLOAT_T)paras.src_width;

    pSrc = in_data;
    pDst = out_data;
    pSrcU = in_data + paras.src_width * paras.src_height;
    pSrcV = in_data + paras.src_width * paras.src_height * 5 / 4;
    pDstU = out_data + paras.dst_width * paras.dst_height;
    pDstV = out_data + paras.dst_width * paras.dst_height * 5 / 4;

    for (y = 0; y < paras.dst_height; y++)
    {
        for (x = 0; x < paras.dst_width; x++)
        {
            xx = clip((int)((FLOAT_T)x / scale_w), 0, paras.src_width - 1);
            yy = clip((int)((FLOAT_T)y / scale_w), 0, paras.src_height - 1);
            pDst[y*paras.dst_width + x] = pSrc[yy * paras.src_width + xx];
        }
    }
    if (paras.img_type==YUV)
    {
        for (y = 0; y < paras.dst_height / 2; y++)
        {
            for (x = 0; x < paras.dst_width / 2; x++)
            {
                xx = clip((int)((FLOAT_T)x / scale_w), 0, paras.src_width / 2 - 1);
                yy = clip((int)((FLOAT_T)y / scale_w), 0, paras.src_height / 2 - 1);
                pDstU[y*paras.dst_width / 2 + x] = pSrcU[yy*paras.src_width / 2 + xx];
                pDstV[y*paras.dst_width / 2 + x] = pSrcV[yy*paras.src_width / 2 + xx];
            }
        }
    }
    return OPRT_OK;
}


OPERATE_RET tuya_ipc_img_resize(UCHAR_T *in_data, TUYA_IMG_RESIZE_PARA_T paras, UCHAR_T *out_data)
{
    if (paras.src_height <= 0
        || paras.dst_height <= 0
        || paras.src_width <= 0
        || paras.dst_width <= 0)
    {
        return OPRT_INVALID_PARM;
    }
    if (paras.src_height / paras.dst_height != paras.src_width / paras.dst_width)
    {
        return OPRT_INVALID_PARM;
    }
    if (paras.resize_type != CUBIC&&paras.resize_type != LINEAR)
    {
        return OPRT_INVALID_PARM;
    }
    if (in_data == NULL || out_data == NULL)
    {
        return OPRT_INVALID_PARM;
    }

    switch (paras.resize_type)
    {
    case LINEAR:
    {
                   Linear_Scale(in_data, paras, out_data);
                   break;
    }
    case CUBIC:
    {				
                  Cubic_Scale_Init(paras.dst_width, paras.dst_height);
                  Cubic_Scale(in_data, paras, out_data);
                  Cubic_Scale_Release();
                  break;
    }
    default:
        break;
    }

    return OPRT_OK;
}

OPERATE_RET tuya_ipc_img_convert_yuv4202bgr888(UCHAR_T *yImg, UCHAR_T *uImg, UCHAR_T *vImg, INT_T width,
    INT_T height, UCHAR_T *dstB, UCHAR_T *dstG, UCHAR_T *dstR)
{
    INT_T i, j;
    INT_T tmp;
    INT_T halfW;
    UCHAR_T *py = NULL;
    UCHAR_T *pu = NULL;
    UCHAR_T *pv = NULL;

    UCHAR_T *pdst_b = NULL;
    UCHAR_T *pdst_g = NULL;
    UCHAR_T *pdst_r = NULL;

    halfW = width >> 1;


    if ((NULL == dstB) || (NULL == dstG) || (NULL == dstR) || (NULL == yImg) || (NULL == uImg) || (NULL == vImg))
    {
        return OPRT_INVALID_PARM;
    }
    if ((width <= 0) || (height <= 0))
    {
        return OPRT_INVALID_PARM;
    }

    py = yImg;
    pu = uImg;
    pv = vImg;

    pdst_b = dstB;
    pdst_g = dstG;
    pdst_r = dstR;

    for (j = 0; j < height; j++)
    {
        for (i = 0; i < width; i++)
        {
            int k = i >> 1;

            tmp = (int)(py[i] + 1.772f * (pu[k] - 128) + 0.5f); // B
            if (tmp < 0)
            {
                tmp = 0;
            }
            else if (tmp > 255)
            {
                tmp = 255;
            }
            pdst_b[i] = tmp;

            tmp = (int)(py[i] - 0.34414f * (pu[k] - 128) - 0.71414f * (pv[k] - 128) + 0.5f); // G
            if (tmp < 0)
            {
                tmp = 0;
            }
            else if (tmp > 255)
            {
                tmp = 255;
            }
            pdst_g[i] = tmp;

            tmp = (int)(py[i] + 1.402f * (pv[k] - 128) + 0.5f); // R
            if (tmp < 0)
            {
                tmp = 0;
            }
            else if (tmp > 255)
            {
                tmp = 255;
            }
            pdst_r[i] = tmp;;
        }

        if ((j & 1) == 1)
        {
            pu += halfW;
            pv += halfW;
        }
        pdst_b += width;
        pdst_g += width;
        pdst_r += width;
        py += width;
    }
     return OPRT_OK;
}

OPERATE_RET tuya_ipc_img_draw_rect_yuv(UCHAR_T *yImg, INT_T width, INT_T height, TUYA_AI_SPEC_T ai_spec)
{
    INT_T j, k;
    UCHAR_T *py = NULL;

    if (NULL == yImg || ai_spec.rect == NULL)
    {
        return OPRT_INVALID_PARM;
    }
    if (width <= 0 || height <= 0 || ai_spec.rect[0].right >= width || ai_spec.rect[0].bottom >= height
                                  || ai_spec.rect[0].top <  0 || ai_spec.rect[0].left < 0)
    {
        return OPRT_INVALID_PARM;
    }


    for (k = 0; k < ai_spec.num; k++)
    {
        py = yImg + ai_spec.rect[k].top * width+ ai_spec.rect[k].left;

        for (j = 0; j < ai_spec.rect[k].right - ai_spec.rect[k].left; j++)
        {
            py[0] = 0;
            py ++;
        }
        for (j = 0; j < ai_spec.rect[k].bottom - ai_spec.rect[k].top; j++)
        {
            py[0] = 0;
            py += width;
        }
        for (j = 0; j < ai_spec.rect[k].right - ai_spec.rect[k].left; j++)
        {
            py[0] = 0;
            py --;
        }
        for (j = 0; j < ai_spec.rect[k].bottom - ai_spec.rect[k].top; j++)
        {
            py[0] = 0;
            py -= width;
        }
    }

    return OPRT_OK;
}


OPERATE_RET tuya_ipc_img_get_rect(UCHAR_T *srcImg, INT_T width, INT_T height, IMG_TYPE_E type, TUYA_AI_SPEC_T ai_spec, UCHAR_T *dstImg)
{
    if (type != Y && type != YUV)
    {
        return OPRT_INVALID_PARM;
    }
    if (NULL == srcImg || NULL == dstImg || ai_spec.rect == NULL)
    {
        return OPRT_INVALID_PARM;
    }
    if (width <= 0 || height <= 0 || ai_spec.rect[0].right >= width || ai_spec.rect[0].bottom >= height
        || ai_spec.rect[0].top < 0 || ai_spec.rect[0].left < 0)
    {
        return OPRT_INVALID_PARM;
    }

    INT_T j;
    INT_T i;
    INT_T dst_width, dst_height;
    UCHAR_T *srcY = NULL;
    UCHAR_T *dstY = NULL;

    UCHAR_T *srcUV = NULL;
    UCHAR_T *dstUV = NULL;

    for (i = 0; i < ai_spec.num; i++)
    {
        dst_width = 36;
        dst_height = 108;

        srcY = srcImg + ai_spec.rect[i].top * width;
        dstY = dstImg + i * dst_height * dst_width;

        if (type == YUV)
        {
            srcUV = srcY + width*height;
            dstUV = dstY + dst_width*dst_height;

            srcUV = srcUV + ai_spec.rect[i].top * width / 2;
        }


        for (j = 0; j < dst_height; j++)
        {
            memcpy(dstY + j*dst_width, srcY + j*width + ai_spec.rect[i].left, dst_width);
            if (type == YUV)
            {
                if (j < dst_height / 2)
                {
                    memcpy(dstUV + j*dst_width, srcUV + j * width + ai_spec.rect[i].left, dst_width);
                }
            }
        }
    }

    return OPRT_OK;
}


OPERATE_RET tuya_ipc_img_osd_ARGB1555(UCHAR_T *srcImg, INT_T width, INT_T height, UINT16_T *osdImg, TUYA_AI_RECT_T osdRect)
{
    if (NULL == srcImg || NULL == osdImg)
    {
        return OPRT_INVALID_PARM;
    }
    if (width <= 0 || height <= 0 || osdRect.left<0 || osdRect.top<0 || osdRect.right > width || osdRect.bottom > height)
    {
        return OPRT_INVALID_PARM;
    }

    INT_T i, j;
    INT_T osd_width, osd_height;

    UINT16_T *pOsd = NULL;
    UCHAR_T * pr = NULL;
    UCHAR_T * pg = NULL;
    UCHAR_T * pb = NULL;

    pOsd = osdImg;
    pr = srcImg + osdRect.top*width + osdRect.left;
    pg = pr + 1;
    pb = pr + 2;

    osd_width = osdRect.right - osdRect.left;
    osd_height = osdRect.bottom - osdRect.top;

    for (j = osdRect.top; j < osd_height; j++)
    {
        for (i = osdRect.left; i < osd_width; i++)
        {
            if (pOsd[0] >> 15)
            {
                pr[0] = (unsigned char)((pOsd[0] & 0x7c00) >> 7);
                pg[0] = (unsigned char)((pOsd[0] & 0x03e0) >> 2);
                pb[0] = (unsigned char)((pOsd[0] & 0x1F << 3));
            }
            pr += 3;
            pg += 3;
            pb += 3;
            pOsd += 1;
        }
        pr += 3 * (width - osd_width);
        pg += 3 * (width - osd_width);
        pb += 3 * (width - osd_width);
    }
    return OPRT_OK;
}
