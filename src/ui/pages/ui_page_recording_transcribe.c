#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_comp_btn.h"
#include "ui_svc_transcribe.h"
#include "ui_svc_recording.h"
#include "ui_page_ids.h"
#include "ui_page_recording_transcribe.h"

#include "tal_memory.h"
#include "tkl_fs.h"
#include "uni_log.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(icon_back_24_24);

#define TR_STATUSBAR_H     32
#define TR_TITLEBAR_H      48
#define TR_PAGEBAR_H       52
#define TR_READ_MAX        (16 * 1024)   /* 阅读卡单次读入上限（UTF-8，超出截断） */
#define TR_NOTICE_RESERVE  96            /* 末页截断提示预留字节（含 "\n\n" 前缀） */

/* ---- 页面控件 ---- */
static lv_obj_t *s_screen   = NULL;
static lv_obj_t *s_content  = NULL;  /* 标题栏下方的内容区（flex 列） */

static lv_obj_t *s_action_btn = NULL; /* 上传并转写 / 重试 */
static lv_obj_t *s_status_lbl = NULL; /* 转写中... / 转写失败 / 无法转写 / 暂无内容 */
static lv_obj_t *s_spinner    = NULL; /* 处理中转圈 */
static lv_obj_t *s_prog_bar   = NULL; /* 上传进度 */
static lv_obj_t *s_prog_lbl   = NULL;

static lv_obj_t *s_tab_cont   = NULL; /* 阅读卡：tab 行 */
static lv_obj_t *s_tab_btn[2] = { NULL, NULL };  /* 0=转写 1=总结 */
static lv_obj_t *s_text_cont  = NULL; /* 文本卡（不可滚动，显示当前页） */
static lv_obj_t *s_text_lbl   = NULL;

static lv_obj_t *s_pagebar    = NULL; /* 底部页脚：[上一页] n/m [下一页] */
static lv_obj_t *s_prev_btn   = NULL;
static lv_obj_t *s_page_lbl   = NULL;
static lv_obj_t *s_next_btn   = NULL;

/* ---- 状态 ---- */
static int                   s_target_id = -1;
static ui_transcribe_phase_t s_cur_phase = UI_TRANSCRIBE_PHASE_NOT_UPLOADED;
static ui_rec_file_kind_t    s_cur_kind  = UI_REC_FILE_TRANSCRIBE;
static bool                  s_cb_attached = false;

/* ---- 分页状态（页面局部，随页面销毁/重建复位）---- */
static char     *s_doc_buf   = NULL;  /* 常驻文本缓冲（含截断提示），分页与切片都基于它 */
static uint32_t  s_doc_len   = 0;     /* strlen(s_doc_buf) */
static uint32_t *s_page_off  = NULL;  /* s_page_cnt+1 项：[i]=第 i 页起始字节偏移，末项=哨兵 s_doc_len */
static int       s_page_cnt  = 0;     /* 总页数 */
static int       s_page_cur  = 0;     /* 当前页 [0, s_page_cnt) */
static bool      s_truncated = false; /* 源文件 > TR_READ_MAX，被截断 */

/* ---- 前置声明 ---- */
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
static void back_cb(lv_event_t *e);
static void gesture_cb(lv_event_t *e);
static void action_cb(lv_event_t *e);
static void tab_cb(lv_event_t *e);
static void prev_cb(lv_event_t *e);
static void next_cb(lv_event_t *e);
static void on_transcribe_status(const ui_transcribe_status_snapshot_t *st);
static void render(ui_transcribe_phase_t phase, int pct);
static void reading_build(void);
static void reading_free(void);
static void reading_load(ui_rec_file_kind_t kind);
static void paginate(void);
static void show_page(int idx);
static void page_go(int idx);
static void pagebar_refresh(void);

void ui_page_recording_transcribe_set_target(int id) { s_target_id = id; }

/* --------------------------------------------------------------------------- */

static void hide_all(void)
{
    if (s_action_btn) lv_obj_add_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
    if (s_status_lbl) lv_obj_add_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_spinner)    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    if (s_prog_bar)   lv_obj_add_flag(s_prog_bar, LV_OBJ_FLAG_HIDDEN);
    if (s_prog_lbl)   lv_obj_add_flag(s_prog_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_tab_cont)   lv_obj_add_flag(s_tab_cont, LV_OBJ_FLAG_HIDDEN);
    if (s_text_cont)  lv_obj_add_flag(s_text_cont, LV_OBJ_FLAG_HIDDEN);
    if (s_pagebar)    lv_obj_add_flag(s_pagebar, LV_OBJ_FLAG_HIDDEN);
}

static void show_status(ui_i18n_key_t key)
{
    if (!s_status_lbl) return;
    lv_label_set_text(s_status_lbl, ui_i18n_text(key));
    lv_obj_clear_flag(s_status_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* 截掉结尾可能不完整的 UTF-8 多字节序列（截断点常落在多字节字中间），返回新长度。 */
static int utf8_trim(const uint8_t *buf, int len)
{
    int t = len;
    while (t > 0 && (buf[t - 1] & 0xC0) == 0x80) t--;   /* 跳过续字节 */
    if (t > 0) {
        unsigned char lead = buf[t - 1];
        int need = (lead < 0x80)            ? 1 :
                   ((lead & 0xE0) == 0xC0)  ? 2 :
                   ((lead & 0xF0) == 0xE0)  ? 3 :
                   ((lead & 0xF8) == 0xF0)  ? 4 : 1;
        if (len - (t - 1) < need) return t - 1;   /* 末字符不完整：丢弃其起始字节 */
    }
    return len;
}

static void reading_free(void)
{
    if (s_doc_buf)  { tal_free(s_doc_buf);  s_doc_buf = NULL; }
    if (s_page_off) { tal_free(s_page_off); s_page_off = NULL; }
    s_doc_len = 0;
    s_page_cnt = 0;
    s_page_cur = 0;
    s_truncated = false;
}

/* 按可用性显隐两个 tab，并高亮当前 s_cur_kind 对应的 tab（主色底+白字），
 * 未激活的用卡片底+次文字色。 */
static void tabs_refresh(void)
{
    const ui_i18n_key_t keys[2]  = { UI_TEXT_TRANSCRIBE_TAB_TEXT, UI_TEXT_TRANSCRIBE_TAB_SUMMARY };
    const ui_rec_file_kind_t k[2] = { UI_REC_FILE_TRANSCRIBE, UI_REC_FILE_SUMMARY };
    int i = 0;

    for (i = 0; i < 2; i++) {
        lv_obj_t *btn = s_tab_btn[i];
        if (!btn) continue;
        if (ui_svc_recording_has_file(s_target_id, k[i])) lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
        else                                              lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);

        ui_comp_btn_set_text(btn, ui_i18n_text(keys[i]));

        bool active = (k[i] == s_cur_kind);
        /* 本地样式覆盖组件的填充样式，优先级更高。 */
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(btn, active ? UI_COLOR_PRIMARY : UI_COLOR_BG_CARD, 0);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, active ? lv_color_white() : UI_COLOR_TEXT_SEC, 0);
        }
    }
}

/* 读取一个结果文件到常驻缓冲（截断到 TR_READ_MAX，超出在末尾追加截断提示），
 * 随后在容器真实尺寸下分页并显示首页。失败/空时显示「暂无内容」。 */
static void reading_load(ui_rec_file_kind_t kind)
{
    TUYA_FILE fp = NULL;
    uint32_t size = 0;

    reading_free();
    if (!s_text_lbl || !s_text_cont) return;

    if (ui_svc_recording_open_file(s_target_id, kind, &fp, &size) == OPRT_OK) {
        uint32_t want = (size > TR_READ_MAX) ? TR_READ_MAX : size;
        s_truncated = (size > TR_READ_MAX);
        char *buf = (char *)tal_malloc(want + TR_NOTICE_RESERVE + 1);
        if (buf) {
            int got = ui_svc_recording_read_at(fp, 0, buf, want);
            ui_svc_recording_close_file(fp);
            fp = NULL;
            if (got > 0) {
                got = utf8_trim((const uint8_t *)buf, got);   /* 防止末尾半个多字节字 */
                buf[got] = '\0';
                if (s_truncated) {
                    /* 截断提示并入缓冲，让它随分页自然落到末页（或独占一页），
                     * 避免硬塞最后一页导致溢出裁切。 */
                    snprintf(buf + got, TR_NOTICE_RESERVE + 1, "\n\n%s",
                             ui_i18n_text(UI_TEXT_TRANSCRIBE_TRUNCATED));
                }
                s_doc_buf = buf;
                s_doc_len = (uint32_t)strlen(buf);
                if (size > TR_READ_MAX) {
                    PR_WARN("transcribe page: text truncated %u/%u bytes",
                            (unsigned)TR_READ_MAX, (unsigned)size);
                }
            } else {
                tal_free(buf);
            }
        }
    }
    if (fp) ui_svc_recording_close_file(fp);

    /* 文本卡此刻已可见，先让布局生效拿到真实尺寸，再分页（RULES §9）。 */
    lv_obj_update_layout(s_screen);
    paginate();
    show_page(0);
}

static void reading_build(void)
{
    bool has_t = ui_svc_recording_has_file(s_target_id, UI_REC_FILE_TRANSCRIBE);
    /* 默认选可用的：优先转写，否则总结。 */
    s_cur_kind = has_t ? UI_REC_FILE_TRANSCRIBE : UI_REC_FILE_SUMMARY;
    tabs_refresh();
    reading_load(s_cur_kind);
}

/* 按文本卡的真实可用尺寸，用与 label 一致的字体/宽度/换行规则逐行走，
 * 每 lpp=floor(可用高/行高) 行切一页，建立「页→字节偏移」表。 */
static void paginate(void)
{
    if (s_page_off) { tal_free(s_page_off); s_page_off = NULL; }
    s_page_cnt = 0;
    s_page_cur = 0;

    if (!s_text_cont || !s_text_lbl || !s_doc_buf || s_doc_len == 0) return;

    lv_coord_t cont_w = lv_obj_get_content_width(s_text_cont);
    lv_coord_t cont_h = lv_obj_get_content_height(s_text_cont);
    if (cont_w <= 0 || cont_h <= 0) return;   /* 布局未就绪 */

    const lv_font_t *font   = lv_obj_get_style_text_font(s_text_lbl, LV_PART_MAIN);
    lv_coord_t letter_space = lv_obj_get_style_text_letter_space(s_text_lbl, LV_PART_MAIN);
    lv_coord_t line_space   = lv_obj_get_style_text_line_space(s_text_lbl, LV_PART_MAIN);
    lv_coord_t step = (lv_coord_t)lv_font_get_line_height(font) + line_space;
    /* N 行总高 = N*line_h + (N-1)*line_space = N*step - line_space ≤ cont_h */
    int lpp = (step > 0) ? (int)((cont_h + line_space) / step) : 1;
    if (lpp < 1) lpp = 1;

    /* pass 1：数总行数（换行规则与 label 渲染一致） */
    int total_lines = 0;
    const char *p = s_doc_buf;
    while (*p) {
        uint32_t ll = _lv_txt_get_next_line(p, font, letter_space, cont_w, NULL, LV_TEXT_FLAG_NONE);
        if (ll == 0) break;
        p += ll;
        total_lines++;
    }
    if (total_lines == 0) return;

    int pages = (total_lines + lpp - 1) / lpp;
    s_page_off = (uint32_t *)tal_malloc((size_t)(pages + 1) * sizeof(uint32_t));
    if (!s_page_off) return;

    /* pass 2：每 lpp 行记一个页首偏移 */
    p = s_doc_buf;
    int line = 0, pg = 0;
    s_page_off[0] = 0;
    while (*p) {
        uint32_t ll = _lv_txt_get_next_line(p, font, letter_space, cont_w, NULL, LV_TEXT_FLAG_NONE);
        if (ll == 0) break;
        p += ll;
        line++;
        if ((line % lpp) == 0) {
            pg++;
            if (pg < pages) s_page_off[pg] = (uint32_t)(p - s_doc_buf);
        }
    }
    s_page_off[pages] = s_doc_len;   /* 哨兵 */
    s_page_cnt = pages;
}

/* 显示第 idx 页（clamp）。空/无表时显示「暂无内容」。 */
static void show_page(int idx)
{
    if (!s_text_lbl) return;

    if (s_page_cnt <= 0 || !s_page_off || !s_doc_buf) {
        lv_label_set_text(s_text_lbl, ui_i18n_text(UI_TEXT_TRANSCRIBE_EMPTY));
        s_page_cur = 0;
        pagebar_refresh();
        return;
    }
    if (idx < 0) idx = 0;
    if (idx > s_page_cnt - 1) idx = s_page_cnt - 1;
    s_page_cur = idx;

    uint32_t start = s_page_off[idx];
    uint32_t end   = s_page_off[idx + 1];

    /* 原地临时截断：lv_label_set_text 会把文本拷进自有缓冲，改回即可，零额外分配。
     * end 始终落在行首（字符边界），临时置 '\0' 不会切坏多字节字。 */
    char saved = s_doc_buf[end];
    s_doc_buf[end] = '\0';
    lv_label_set_text(s_text_lbl, s_doc_buf + start);
    s_doc_buf[end] = saved;

    pagebar_refresh();
}

/* clamp 后翻到 idx 页；与当前页相同则不重绘（边界 swipe/点击为无副作用 no-op）。 */
static void page_go(int idx)
{
    if (s_page_cnt <= 0) return;
    if (idx < 0) idx = 0;
    if (idx > s_page_cnt - 1) idx = s_page_cnt - 1;
    if (idx == s_page_cur) return;
    show_page(idx);
}

/* 更新 n/m 指示，按边界置灰 prev/next；空或单页时隐藏整个页脚。 */
static void pagebar_refresh(void)
{
    if (!s_pagebar) return;
    if (s_page_cnt <= 1) {
        lv_obj_add_flag(s_pagebar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_pagebar, LV_OBJ_FLAG_HIDDEN);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d/%d", s_page_cur + 1, s_page_cnt);
    lv_label_set_text(s_page_lbl, buf);

    ui_comp_btn_set_enabled(s_prev_btn, s_page_cur > 0);
    ui_comp_btn_set_enabled(s_next_btn, s_page_cur < s_page_cnt - 1);
}

/* phase → 视图。UPLOADING 复用 bar 只更新值；其余隐藏全部再显相应控件。 */
static void render(ui_transcribe_phase_t phase, int pct)
{
    if (!s_content) return;

    /* 上传进度高频更新：同处 UPLOADING 仅刷新数值。 */
    if (phase == UI_TRANSCRIBE_PHASE_UPLOADING && s_cur_phase == UI_TRANSCRIBE_PHASE_UPLOADING) {
        int v = (pct < 0) ? 0 : (pct > 100 ? 100 : pct);
        if (s_prog_bar) lv_bar_set_value(s_prog_bar, v, LV_ANIM_OFF);
        if (s_prog_lbl) lv_label_set_text_fmt(s_prog_lbl, "%s %d%%",
                                              ui_i18n_text(UI_TEXT_TRANSCRIBE_UPLOADING), v);
        return;
    }

    /* 已在 DONE：不重建阅读卡，保留用户当前选择的 tab 与翻到的页码
     * （否则重复的状态回调会把它刷回「转写结果」第 1 页）。 */
    if (phase == UI_TRANSCRIBE_PHASE_DONE && s_cur_phase == UI_TRANSCRIBE_PHASE_DONE) {
        return;
    }

    hide_all();

    switch (phase) {
    case UI_TRANSCRIBE_PHASE_UNAVAILABLE:
        show_status(UI_TEXT_TRANSCRIBE_UNAVAILABLE);
        break;
    case UI_TRANSCRIBE_PHASE_NOT_UPLOADED:
        ui_comp_btn_set_text(s_action_btn, ui_i18n_text(UI_TEXT_TRANSCRIBE_UPLOAD));
        lv_obj_clear_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    case UI_TRANSCRIBE_PHASE_UPLOADING: {
        int v = (pct < 0) ? 0 : (pct > 100 ? 100 : pct);
        if (s_prog_bar) { lv_bar_set_value(s_prog_bar, v, LV_ANIM_OFF); lv_obj_clear_flag(s_prog_bar, LV_OBJ_FLAG_HIDDEN); }
        if (s_prog_lbl) {
            lv_label_set_text_fmt(s_prog_lbl, "%s %d%%", ui_i18n_text(UI_TEXT_TRANSCRIBE_UPLOADING), v);
            lv_obj_clear_flag(s_prog_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        break;
    }
    case UI_TRANSCRIBE_PHASE_PROCESSING:
        if (s_spinner) lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        show_status(UI_TEXT_TRANSCRIBE_PROCESSING);
        break;
    case UI_TRANSCRIBE_PHASE_DONE:
        if (s_tab_cont)  lv_obj_clear_flag(s_tab_cont, LV_OBJ_FLAG_HIDDEN);
        if (s_text_cont) lv_obj_clear_flag(s_text_cont, LV_OBJ_FLAG_HIDDEN);
        /* 页脚显隐交给 pagebar_refresh（单页隐藏）；此处不强制显示。 */
        reading_build();
        break;
    case UI_TRANSCRIBE_PHASE_FAILED:
        show_status(UI_TEXT_TRANSCRIBE_FAILED);
        ui_comp_btn_set_text(s_action_btn, ui_i18n_text(UI_TEXT_TRANSCRIBE_RETRY));
        lv_obj_clear_flag(s_action_btn, LV_OBJ_FLAG_HIDDEN);
        break;
    default:
        break;
    }
    s_cur_phase = phase;
}

/* --------------------------------------------------------------------------- */

static void action_cb(lv_event_t *e)
{
    (void)e;
    /* NOT_UPLOADED 上传 / FAILED 重试 都走整条上传流程。 */
    ui_svc_transcribe_upload(s_target_id);
}

static void tab_cb(lv_event_t *e)
{
    ui_rec_file_kind_t kind = (ui_rec_file_kind_t)(uintptr_t)lv_event_get_user_data(e);
    if (kind == s_cur_kind) return;
    s_cur_kind = kind;
    tabs_refresh();      /* 移动激活高亮 */
    reading_load(kind);  /* 重新载入+分页，复位到第 1 页 */
}

static void prev_cb(lv_event_t *e)
{
    (void)e;
    page_go(s_page_cur - 1);
}

static void next_cb(lv_event_t *e)
{
    (void)e;
    page_go(s_page_cur + 1);
}

static void on_transcribe_status(const ui_transcribe_status_snapshot_t *st)
{
    if (!s_screen || ui_route_current() != UI_PAGE_RECORDING_TRANSCRIBE) return;
    if (st == NULL || st->id != s_target_id) return;
    render(st->phase, st->upload_percent);
}

/* --------------------------------------------------------------------------- */

static void on_create(void *parent)
{
    (void)parent;

    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_set_flex_flow(s_screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(s_screen, ui_adapt(TR_STATUSBAR_H), 0);

    /* 标题栏 */
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(TR_TITLEBAR_H));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

    lv_obj_t *title = lv_label_create(titlebar);
    lv_label_set_text(title, ui_i18n_text(UI_TEXT_TRANSCRIBE_TITLE));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* 内容区：居中 flex 列 */
    s_content = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_width(s_content, LV_PCT(100));
    lv_obj_set_flex_grow(s_content, 1);
    lv_obj_set_flex_flow(s_content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_content, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(s_content, ui_adapt(UI_SPACE_LG), 0);
    lv_obj_set_style_pad_row(s_content, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);

    /* 动作按钮（上传 / 重试） */
    s_action_btn = ui_comp_btn_create_styled(s_content, ui_i18n_text(UI_TEXT_TRANSCRIBE_UPLOAD),
                                             UI_COMP_BTN_PRIMARY, action_cb);

    /* 状态文本 */
    s_status_lbl = lv_label_create(s_content);
    lv_label_set_long_mode(s_status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_status_lbl, LV_PCT(90));
    lv_obj_set_style_text_align(s_status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_status_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_label_set_text(s_status_lbl, "");

    /* 处理中转圈 */
    s_spinner = lv_spinner_create(s_content, 1000, 60);
    lv_obj_set_size(s_spinner, ui_adapt(48), ui_adapt(48));

    /* 上传进度条 + 文本 */
    s_prog_bar = lv_bar_create(s_content);
    lv_obj_set_size(s_prog_bar, LV_PCT(80), ui_adapt(8));
    lv_bar_set_range(s_prog_bar, 0, 100);
    lv_bar_set_value(s_prog_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_prog_bar, UI_COLOR_PRIMARY, LV_PART_INDICATOR);

    s_prog_lbl = lv_label_create(s_content);
    lv_obj_set_style_text_color(s_prog_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_label_set_text(s_prog_lbl, "");

    /* 阅读卡：tab 行 */
    s_tab_cont = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_tab_cont);
    lv_obj_set_size(s_tab_cont, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_tab_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_tab_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_tab_cont, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(s_tab_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* 传 NULL cb 给组件，自行绑定带 kind user_data 的 handler（避免双重绑定）。 */
    s_tab_btn[0] = ui_comp_btn_create_styled(s_tab_cont, ui_i18n_text(UI_TEXT_TRANSCRIBE_TAB_TEXT),
                                             UI_COMP_BTN_SECONDARY, NULL);
    lv_obj_add_event_cb(s_tab_btn[0], tab_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)UI_REC_FILE_TRANSCRIBE);
    s_tab_btn[1] = ui_comp_btn_create_styled(s_tab_cont, ui_i18n_text(UI_TEXT_TRANSCRIBE_TAB_SUMMARY),
                                             UI_COMP_BTN_SECONDARY, NULL);
    lv_obj_add_event_cb(s_tab_btn[1], tab_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)UI_REC_FILE_SUMMARY);

    /* 阅读卡：文本卡（不可滚动，仅显示当前页；翻页由 swipe / 页脚控制） */
    s_text_cont = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_text_cont);
    lv_obj_set_width(s_text_cont, LV_PCT(100));
    lv_obj_set_flex_grow(s_text_cont, 1);
    lv_obj_set_style_bg_color(s_text_cont, UI_COLOR_BG_CARD, 0);
    lv_obj_set_style_bg_opa(s_text_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_text_cont, ui_adapt(UI_RADIUS_MD), 0);
    lv_obj_set_style_pad_all(s_text_cont, ui_adapt(UI_SPACE_MD), 0);
    lv_obj_clear_flag(s_text_cont, LV_OBJ_FLAG_SCROLLABLE);

    s_text_lbl = lv_label_create(s_text_cont);
    lv_label_set_long_mode(s_text_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_text_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(s_text_lbl, UI_COLOR_TEXT, 0);
    lv_label_set_text(s_text_lbl, "");

    /* 底部页脚：[上一页]  n/m  [下一页] */
    s_pagebar = lv_obj_create(s_content);
    lv_obj_remove_style_all(s_pagebar);
    lv_obj_set_size(s_pagebar, LV_PCT(100), ui_adapt(TR_PAGEBAR_H));
    lv_obj_set_style_bg_opa(s_pagebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_pagebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_pagebar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_pagebar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_pagebar, ui_adapt(UI_SPACE_LG), 0);

    s_prev_btn = ui_comp_btn_create_styled(s_pagebar, ui_i18n_text(UI_TEXT_PREV_PAGE),
                                           UI_COMP_BTN_TEXT, prev_cb);

    s_page_lbl = lv_label_create(s_pagebar);
    lv_label_set_text(s_page_lbl, "1/1");
    lv_obj_set_style_text_color(s_page_lbl, UI_COLOR_TEXT_SEC, 0);

    s_next_btn = ui_comp_btn_create_styled(s_pagebar, ui_i18n_text(UI_TEXT_NEXT_PAGE),
                                           UI_COMP_BTN_TEXT, next_cb);

    ui_svc_transcribe_set_cb(on_transcribe_status);
    s_cb_attached = true;

    /* 复位为非 UPLOADING 值，确保首次 render 走完整分支（避免上一实例残留
     * 的 s_cur_phase==UPLOADING 让新建的隐藏进度条走增量分支而不显示）。 */
    s_cur_phase = UI_TRANSCRIBE_PHASE_NOT_UPLOADED;
    {
        ui_transcribe_status_snapshot_t st;
        ui_svc_transcribe_get_status(s_target_id, &st);
        render(st.phase, st.upload_percent);
    }
}

static void on_enter(uint32_t dirty)
{
    if (!s_cb_attached) {
        ui_svc_transcribe_set_cb(on_transcribe_status);
        s_cb_attached = true;
    }
    /* 仅进入时补一次当前阶段（离开期间可能由轮询线程推进过）。运行中的阶段变化
     * 由 on_transcribe_status cb 驱动 render，无需逐 tick（每秒 SYSTEM）重绘。
     * 控件跨 leave/enter 持久，render 自身按 phase 切换显隐；同态 DONE 保留页码。 */
    if (ui_dirty_is_enter(dirty) && s_screen) {
        ui_transcribe_status_snapshot_t st;
        ui_svc_transcribe_get_status(s_target_id, &st);
        render(st.phase, st.upload_percent);
    }
}

static void on_leave(void)
{
    ui_svc_transcribe_set_cb(NULL);
    s_cb_attached = false;
}

static void on_destroy(void)
{
    ui_svc_transcribe_set_cb(NULL);
    s_cb_attached = false;
    reading_free();
    /* 离开转写页：恢复进入前的设备模式（上传期间持有的 RECORD）。 */
    ui_svc_transcribe_release_mode();
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL; s_content = NULL;
        s_action_btn = NULL; s_status_lbl = NULL; s_spinner = NULL;
        s_prog_bar = NULL; s_prog_lbl = NULL;
        s_tab_cont = NULL; s_tab_btn[0] = NULL; s_tab_btn[1] = NULL;
        s_text_cont = NULL; s_text_lbl = NULL;
        s_pagebar = NULL; s_prev_btn = NULL; s_page_lbl = NULL; s_next_btn = NULL;
    }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_route_pop();
}

/* 阅读态（DONE）：左滑=下一页、右滑=上一页（首页右滑 clamp 为 no-op），
 * 返回走标题栏返回键。非阅读态（上传/处理/失败）保留全局「右滑返回」。 */
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (s_cur_phase == UI_TRANSCRIBE_PHASE_DONE && s_page_cnt > 0) {
        if (dir == LV_DIR_LEFT)       page_go(s_page_cur + 1);
        else if (dir == LV_DIR_RIGHT) page_go(s_page_cur - 1);
        return;
    }

    if (dir == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());
        ui_route_pop();
    }
}

const ui_page_entry_t ui_page_recording_transcribe_entry = {
    .id = UI_PAGE_RECORDING_TRANSCRIBE,
    .name = "recording_transcribe",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
