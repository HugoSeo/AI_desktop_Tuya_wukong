/**
 * @file wukong_audio_pipeline.h
 * @brief 采集 pipeline 对内接口：采集(ADC/DMIC) -> ring buffer -> process task -> frontend。
 *        处理后的 mono 帧经 output_cb 交付消费侧(recorder)；消费侧不感知上游麦克风拓扑。
 * @note 仅供 src/wukong/audio/input/ 内部使用
 */

#ifndef __WUKONG_AUDIO_PIPELINE_H__
#define __WUKONG_AUDIO_PIPELINE_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

//! pipeline 处理后 mono 帧的交付回调（board 侧传入 __audio_frame_put）
typedef INT_T (*WUKONG_AUDIO_PIPELINE_OUTPUT_CB)(UINT8_T *buf, UINT32_T len);

typedef struct {
    UINT32_T sample_rate;       //!< 采样率
    UINT8_T  sample_bits;       //!< 位宽
    UINT8_T  channel;           //!< 输出通道数（recorder 上行，SPRS 强制 1）
    UINT16_T vad_active_ms;     //!< 透传 wukong_audio_frontend_init
    UINT16_T vad_off_ms;        //!< 同上
    WUKONG_AUDIO_PIPELINE_OUTPUT_CB output_cb;  //!< 处理后 mono 帧交付回调，必填
} WUKONG_AUDIO_PIPELINE_CFG_T;

/** @brief 初始化采集 pipeline（rb/ADC/DMIC/frontend/process task），成功后采集已启动
 *  @return OPRT_OK on success；失败时内部已自清理 */
OPERATE_RET wukong_audio_pipeline_init(WUKONG_AUDIO_PIPELINE_CFG_T *cfg);

/** @brief 停采集、等 process task 确认退出后释放全部 pipeline 资源；幂等 */
OPERATE_RET wukong_audio_pipeline_deinit(VOID);

/** @brief 设置采集音量（ADC 通路） */
OPERATE_RET wukong_audio_pipeline_set_vol(UINT8_T volume);

#ifdef __cplusplus
}
#endif

#endif /* __WUKONG_AUDIO_PIPELINE_H__ */
