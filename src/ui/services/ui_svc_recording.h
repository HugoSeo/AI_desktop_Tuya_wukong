#ifndef __UI_SVC_RECORDING_H__
#define __UI_SVC_RECORDING_H__

#include <stdint.h>
#include "tuya_cloud_types.h"
#include "ty_cJSON.h"
#include "tkl_fs.h"           /* TUYA_FILE */
#include "svc_ai_player.h"   /* AI_PLAYER_STATE_T */

#ifdef __cplusplus
extern "C" {
#endif

/* md5 hex 串长度（不含结尾 NUL）；云端转写结果以音频 md5 为关联键 */
#define UI_REC_MD5_HEX_LEN 32

/* 转写态。持久化为整数，与旧 view 数据兼容（-1/0/1/2）。 */
typedef enum {
    UI_TRANSCRIBE_NOT_UPLOADED = -1, /* 未上传（默认 / legacy JSON 缺字段） */
    UI_TRANSCRIBE_PROCESSING   = 0,  /* 已上传，云端处理中 */
    UI_TRANSCRIBE_DONE         = 1,  /* 完成，结果文件已下载 */
    UI_TRANSCRIBE_FAILED       = 2,  /* 失败 */
} ui_transcribe_status_t;

/* 转写结果文件类型：原始转写文本 / 总结文本 */
typedef enum {
    UI_REC_FILE_TRANSCRIBE = 0,
    UI_REC_FILE_SUMMARY    = 1,
} ui_rec_file_kind_t;

/* 采集状态：无暂停态，启动后只能停止 */
typedef enum {
    UI_REC_CAP_IDLE = 0,
    UI_REC_CAP_RECORDING,
} ui_rec_cap_state_t;

/* 状态快照（标量为主，Cortex-M33 上单字赋值原子；跨线程更新经 ui_app_async_call 编组） */
typedef struct {
    ui_rec_cap_state_t cap_state;        /* 采集状态 */
    uint32_t           cap_elapsed_sec;  /* 已录时长(秒) */
    AI_PLAYER_STATE_T  play_state;       /* STOPPED/PLAYING/PAUSED（本地回放） */
    int                play_id;          /* 正在回放的条目 id，-1 无 */
} ui_recording_status_t;

/* 页面注册回调；状态变化时在 UI 线程触发（单槽位，NULL 注销） */
typedef void (*ui_svc_recording_cb_t)(const ui_recording_status_t *st);
/* 异步列表投递；UI 线程触发，接收方拥有 list（用完 ty_cJSON_Delete），NULL=空/失败 */
typedef void (*ui_svc_recording_list_cb_t)(ty_cJSON *list);

/* ---- 生命周期 ---- */
void ui_svc_recording_init(void);                       /* ui_services_init() 调用 */
void ui_svc_recording_set_cb(ui_svc_recording_cb_t cb); /* NULL 注销 */
void ui_svc_recording_get_status(ui_recording_status_t *out);

/* ---- 采集会话（绑定采集页生命周期）---- */
OPERATE_RET ui_svc_recording_capture_open(void);   /* 存当前模式→切 RECORD 模式→注册音频回调 */
void        ui_svc_recording_capture_close(void);  /* 停止保存→恢复原模式 */
OPERATE_RET ui_svc_recording_capture_start(void);  /* 开始（计时归零, 使能音频输入） */
void        ui_svc_recording_capture_stop(void);   /* 停止并落盘成一条记录 */
/* 不提供 pause/resume：启动后只能停止 */

/* ---- 列表 ---- */
void        ui_svc_recording_list_async(ui_svc_recording_list_cb_t cb); /* WORKQ 取→UI 投递 */
OPERATE_RET ui_svc_recording_remove_id(int id);    /* 删除条目+音频+转写结果文件 */

/* ---- 转写工作流领域接口 ----
 * 仅供 ui_svc_transcribe 调用。数据所有权单点归本服务（含转写字段与
 * recording_list.json 持久化），转写工作流不直接读写条目内存。下列接口
 * 内部自持表锁，可从轮询线程调用。 */

/* 收集待查转写结果的条目：transcribe_status==PROCESSING && md5 非全零 &&
 * 已过上传后冷却窗口。ids/md5_hex 至少各 20 槽，写入 *count 条。 */
OPERATE_RET ui_svc_recording_collect_pending(int *ids,
                                             char md5_hex[][UI_REC_MD5_HEX_LEN + 1],
                                             int *count);

/* 上传完成：置 PROCESSING、设上传后冷却 tick、落盘。 */
void        ui_svc_recording_mark_uploaded(int id);

/* 写回云端转写结果。内部按 md5 做 anti-ABA 校验（id 可回收复用）：若条目
 * md5 已变则丢弃。st==DONE 时记录两个结果文件 basename。会落盘。 */
OPERATE_RET ui_svc_recording_apply_transcribe_result(int id, const char *md5_hex,
                                                     ui_transcribe_status_t st,
                                                     const char *t_file,
                                                     const char *s_file);

/* 查询条目当前转写态；条目不存在返回 NOT_UPLOADED。 */
ui_transcribe_status_t ui_svc_recording_transcribe_status(int id);

/* md5 是否可用（非全零）；全零表示"待按需计算"（中断恢复的孤儿、或采集时
 * 哈希失败）。全零的条目仍可上传：上传泵会在读文件时顺带算出 md5 并写回，
 * 然后才进入 PROCESSING —— 因此上传触发后 md5 必已落定，不影响云端关联。 */
BOOL_T      ui_svc_recording_md5_available(int id);

/* 取上传所需信息：文件名 + 字节数 + md5 是否可用。条目不存在返回错误。 */
OPERATE_RET ui_svc_recording_get_upload_info(int id, char *name, uint32_t name_size,
                                             uint64_t *out_len, BOOL_T *out_md5_ok);

/* 写回条目 md5（上传泵搭便车算出后调用）；条目不存在返回错误，成功落盘。
 * md5 全零表示置为"待计算"，正常调用方传非零摘要。 */
OPERATE_RET ui_svc_recording_set_md5(int id, const uint8_t md5[16]);

/* 取条目显示名（供转写页标题）。 */
BOOL_T      ui_svc_recording_get_name(int id, char *buf, uint32_t size);

/* ---- 转写结果文本读取（供转写页阅读卡）---- */
BOOL_T      ui_svc_recording_has_file(int id, ui_rec_file_kind_t kind);
OPERATE_RET ui_svc_recording_open_file(int id, ui_rec_file_kind_t kind,
                                       TUYA_FILE *out_fp, uint32_t *out_size);
int         ui_svc_recording_read_at(TUYA_FILE fp, uint32_t offset, char *buf, uint32_t size);
void        ui_svc_recording_close_file(TUYA_FILE fp);

/* ---- 本地回放 ---- */
void        ui_svc_recording_play_id(int id);
void        ui_svc_recording_play_pause(void);
void        ui_svc_recording_play_resume(void);
void        ui_svc_recording_play_stop(void);
int         ui_svc_recording_play_current_id(void);
/* 字节级播放进度（datasink 读偏移 / 文件总长）。OPRT_OK 时 len>0 且 off<=len；
 * 无活动播放或总长未知时返回错误。CBR 采集流下按字节换算时间是精确的；
 * off 为解码器读位置，比实际出声超前约 2~3s（framebuf+解码缓冲）。 */
OPERATE_RET ui_svc_recording_play_progress_bytes(uint32_t *off, uint32_t *len);

#ifdef __cplusplus
}
#endif

#endif /* __UI_SVC_RECORDING_H__ */
