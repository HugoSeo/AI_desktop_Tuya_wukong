#include "sndx.h"
#include "sndx_typedef.h"
#include "tal_log.h"
#include "wukong_kws.h"

INT_T SNDX_kws_create(WUKONG_KWS_CTX_T *ctx)
{
    INT_T nRet = 0;

	nRet = sndx_asr_init(450);
	if ( nRet != SNDX_ASR_NO_ERROR)	{
		TAL_PR_ERR("asr init error: %d\n", nRet);
		return nRet;
	}

    return OPRT_OK;
}

INT_T SNDX_kws_detect(WUKONG_KWS_CTX_T *ctx, UINT8_T *data, UINT32_T datalen)
{
    INT_T rt = -1;
    WUKONG_KWS_INDEX_E kws_idx = 0;

    if ((datalen % 2) != 0) {
        TAL_PR_DEBUG("audio data alignment error\n");
    }

    INT_T id = sndx_asr_process((short *)data, datalen / 2);
    if (id < 0) {
        TAL_PR_DEBUG("asr process error: %d\n", id);
    }

    if (1001 == id) {
        kws_idx = WUKONG_KWS_NIHAOTUYA;
        TAL_PR_DEBUG("sndx_WakeWord -> 你好涂鸦\n");
    } else if (1002 == id) {
        TAL_PR_DEBUG("sndx_WakeWord -> heytuya\n");
        kws_idx = WUKONG_KWS_HEYTUYA;
    }

    if (kws_idx) {
        wukong_kws_event(kws_idx);
        rt = OPRT_OK;
    }

    return rt;
}

INT_T SNDX_kws_reset(WUKONG_KWS_CTX_T *ctx)
{
    return OPRT_OK;
}

INT_T SNDX_kws_deinit(WUKONG_KWS_CTX_T *ctx)
{
    sndx_asr_release();
    return OPRT_OK;
}
