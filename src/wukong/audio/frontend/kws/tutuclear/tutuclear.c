#include "wukong_kws.h"
#include "tutu_typedef.h"
#include "tutuclear.h"
#include "tal_log.h"
#include <os/mem.h>

#define __EN_EXTERNALLY_ALLOCATION__ // enable this define for externally malloc TUTU objects
#define __EN_DIAGNOSTICS__ //enable this to have the proper internal returned diagnostical information
#define SUBFRAME_SZ_BYTE	    (320)
#define FRAME_SZ_BYTE           (SUBFRAME_SZ_BYTE *2)
#define MAX_MIC_SIZE            FRAME_SZ_BYTE * 10 * 5

extern UW32 TUTUClear_QueryMemSz(void);
extern W16 TUTUClear_Init(void *pTUTUClearObjectMem, void **pTUTUClearObject);
extern W16 TUTUClear_Release(void **pTUTUClearObject);
extern W16 TUTUClear_OneFrame(void *pTUTUClearObject, W16 *ptMIC,  W32 *pw32WakeWord);
extern void TUTUClear_SetWakeupThr(void *pTUTUClearObject,W16 Thr);
extern W16 TUTUClear_GetWakeupThr(void *pTUTUClearObject);

INT_T TUTUClear_kws_create(WUKONG_KWS_CTX_T *ctx)
{

    VOID        *pTUTUClearObject = NULL;
    VOID        *pExternallyAllocatedMem = NULL;
	/* =======================================================================
      1. memory allocation
	======================================================================== */
#ifdef __EN_EXTERNALLY_ALLOCATION__
	INT_T i = TUTUClear_QueryMemSz();
#ifdef TUYA_PSARM_SUPPORT
    pExternallyAllocatedMem = psram_malloc(i);
	TAL_PR_DEBUG("tutuClear PSTAM DM usage = %d bytes, addr = %p\n", i, pExternallyAllocatedMem);
#else
    pExternallyAllocatedMem = os_malloc(i);
	TAL_PR_DEBUG("tutuClear DM usage = %d bytes, addr = %p\n", i, pExternallyAllocatedMem);
#endif
#endif // __EN_EXTERNALLY_ALLOCATION__

	/* =======================================================================
	  2. wakeup state initialization
	======================================================================== */
    W16  TUTUClear_ret = TUTUClear_Init(pExternallyAllocatedMem,
						&pTUTUClearObject);
    if (TUTUClear_ret != TUTU_OK) {
        TAL_PR_DEBUG("Fail to do TUTUClear_Init %d.\n", TUTUClear_ret);
        return OPRT_MALLOC_FAILED;
    }

    ctx->handle    = pTUTUClearObject;
    ctx->priv_data = pExternallyAllocatedMem;

    return OPRT_OK;
}

INT_T TUTUClear_kws_detect(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen)
{
    INT_T rt = OPRT_COM_ERROR;
    INT_T i, frame_count = 0;
    INT_T w32WakeWord;
    SYS_TIME_T start, end;

    frame_count = datalen / FRAME_SZ_BYTE;

    // start = tkl_system_get_millisecond();
    //! copy 320 * 2 byte
    for (i = 0; i < frame_count; i++) { //accepting 20ms pcm stream
        TUTUClear_OneFrame(ctx->handle,  (W16*)(data + i * FRAME_SZ_BYTE), &w32WakeWord);
        if (w32WakeWord != 0) {     //check wakeup 
            if (1 == w32WakeWord) {
                TAL_PR_DEBUG("TUTUClear_WakeWord -> 你好涂鸦\n");
            } else if (2 == w32WakeWord) {
                TAL_PR_DEBUG("TUTUClear_WakeWord -> 小智同学\n");
            } else if (3 == w32WakeWord) {
                TAL_PR_DEBUG("TUTUClear_WakeWord -> heytuya\n");
            } else if (4 == w32WakeWord) {
                TAL_PR_DEBUG("TUTUClear_WakeWord -> hituya\n");
            } else {
                TAL_PR_DEBUG("TUTUClear_WakeWord -> unknow %d\n", w32WakeWord);
            }
            if (1 <= w32WakeWord && 4 >= w32WakeWord) {
                wukong_kws_event(w32WakeWord);
            }
            rt = OPRT_OK;
            break;
        }
    }

    // end = tkl_system_get_millisecond();
    // TAL_PR_DEBUG("TUTUClear_WakeWord fc = %d, time -> %dms\n", frame_count, (INT_T)(end - start));

    return rt;
}

INT_T TUTUClear_kws_reset(WUKONG_KWS_CTX_T *ctx)
{
    return TUTUClear_Init(ctx->priv_data, &ctx->handle);
}

INT_T TUTUClear_kws_deinit(WUKONG_KWS_CTX_T *ctx)
{
    if (ctx->handle) {
        TUTUClear_Release(&ctx->handle);
    }

    if (ctx->priv_data) {
        os_free(ctx->priv_data);
    }

    return OPRT_OK;
}

