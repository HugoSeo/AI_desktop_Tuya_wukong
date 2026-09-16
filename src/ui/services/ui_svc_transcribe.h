#ifndef __UI_SVC_TRANSCRIBE_H__
#define __UI_SVC_TRANSCRIBE_H__

#include <stdint.h>
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 云端转录工作流服务。
 *
 * 职责：上传录音到 AI agent、触发云端转写、轮询取结果、下载转写/总结文本。
 * 数据所有权归 ui_svc_recording（转写字段与 JSON 持久化），本服务只驱动工作流，
 * 经 ui_svc_recording 的领域 API 读写数据。
 *
 * 线程模型：
 *   - 上传泵跑在 WORKQ_SYSTEM（自驱动分块），不占 UI 线程。
 *   - 轮询为常驻线程，平时阻塞在信号量上；上传完成时被唤醒，无待处理条目时回到
 *     阻塞，ui_svc_transcribe_deinit() 可干净停止。
 *   - 状态变化经 ui_app_async_call 编组到 UI 线程，触发单槽回调。
 */

/* 转写阶段（派生量：由 ui_svc_recording 的转写态 + 上传进行态合成） */
typedef enum {
    UI_TRANSCRIBE_PHASE_UNAVAILABLE = 0, /* md5 全零，无法转写 */
    UI_TRANSCRIBE_PHASE_NOT_UPLOADED,    /* 未上传 */
    UI_TRANSCRIBE_PHASE_UPLOADING,       /* 上传中 */
    UI_TRANSCRIBE_PHASE_PROCESSING,      /* 已上传，云端处理中 */
    UI_TRANSCRIBE_PHASE_DONE,            /* 完成，结果可读 */
    UI_TRANSCRIBE_PHASE_FAILED,          /* 失败 */
} ui_transcribe_phase_t;

typedef struct {
    int                   id;             /* 目标条目 id */
    ui_transcribe_phase_t phase;
    int                   upload_percent; /* UPLOADING 时 0-100，其余为 -1 */
} ui_transcribe_status_snapshot_t;

/* 状态回调；在 UI 线程触发（单槽位，NULL 注销）。 */
typedef void (*ui_svc_transcribe_cb_t)(const ui_transcribe_status_snapshot_t *st);

/* ---- 生命周期（ui_services_init / deinit 调用）---- */
void        ui_svc_transcribe_init(void);
void        ui_svc_transcribe_deinit(void);

/* ---- 转写页订阅 ---- */
void        ui_svc_transcribe_set_cb(ui_svc_transcribe_cb_t cb); /* NULL 注销 */
void        ui_svc_transcribe_get_status(int id, ui_transcribe_status_snapshot_t *out);

/* ---- 触发上传（同时也是 FAILED 重试入口）----
 * md5 不可用返回 OPRT_NOT_SUPPORTED；已有上传在进行返回 OPRT_COM_ERROR。 */
OPERATE_RET ui_svc_transcribe_upload(int id);

/* 释放上传期间持有的 RECORD 设备模式，恢复到进入转写页前的模式。
 * 由转写页在 on_destroy（离开页面）时调用。
 * 之所以不在上传结束就恢复：云端那一轮的响应在 input_stop 后约 1~2s 才回来，
 * 必须仍处于 RECORD 语境才会静默完成转写；过早恢复会让响应被闲聊 agent 接管
 * 并打断转写。未持有时为 no-op。 */
void        ui_svc_transcribe_release_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_SVC_TRANSCRIBE_H__ */
