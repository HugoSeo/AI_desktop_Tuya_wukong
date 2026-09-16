#ifndef __UI_PAGE_RECORDING_TRANSCRIBE_H__
#define __UI_PAGE_RECORDING_TRANSCRIBE_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 进入转写页前由调用方（录音列表的播放卡 AI 图标）设置目标条目 id，
 * 然后 ui_route_push(UI_PAGE_RECORDING_TRANSCRIBE)。 */
void ui_page_recording_transcribe_set_target(int id);

#ifdef __cplusplus
}
#endif

#endif /* __UI_PAGE_RECORDING_TRANSCRIBE_H__ */
