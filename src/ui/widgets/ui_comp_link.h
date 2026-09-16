#ifndef __UI_COMP_LINK_H__
#define __UI_COMP_LINK_H__

/**
 * @file ui_comp_link.h
 * @brief Clickable hyperlink label — coloured + underlined text that fires a
 *        callback on tap (e.g. the chat "查看图片" link for AI-generated images).
 *
 * Layer: widgets/ — depends only on LVGL + style. The optional argument is
 * copied into the widget (LVGL heap) and released on the LVGL delete event, so
 * callers may freely evict the link's parent without leaking; the stored copy is
 * handed back to @p cb on each click.
 */

#include "lvgl.h"
#include <stdint.h>

/** Invoked on click with the per-link argument copy (NULL when none was given). */
typedef void (*ui_link_cb_t)(void *arg);

/**
 * @brief Create a clickable hyperlink label in @p parent.
 * @param[in] parent   LVGL parent (e.g. a chat bubble content box)
 * @param[in] text     link caption (already i18n-resolved by the caller)
 * @param[in] cb       click callback; receives the argument copy
 * @param[in] arg      argument bytes to copy, or NULL
 * @param[in] arg_len  size of @p arg in bytes (0 when @p arg is NULL)
 * @return the label object, or NULL on failure
 */
lv_obj_t *ui_comp_link_create(lv_obj_t *parent, const char *text, ui_link_cb_t cb,
                              const void *arg, uint32_t arg_len);

#endif /* __UI_COMP_LINK_H__ */
