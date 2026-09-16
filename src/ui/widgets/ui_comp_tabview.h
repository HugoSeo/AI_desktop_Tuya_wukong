#ifndef __UI_COMP_TABVIEW_H__
#define __UI_COMP_TABVIEW_H__

#include "lvgl.h"

/**
 * @brief Creation properties for the shared styled tabview.
 *
 * The returned object is a native LVGL tabview. Callers keep using the
 * lv_tabview_* APIs to add, rename, query, and select tabs.
 */
typedef struct {
    lv_dir_t tab_pos;
    lv_coord_t tab_size;
} ui_comp_tabview_props_t;

/**
 * @brief Create a native LVGL tabview with the shared segmented-pill style.
 *
 * The component owns no resources outside the LVGL object tree, so deleting
 * the parent is sufficient for cleanup.
 *
 * @param parent Parent LVGL object.
 * @param props  Read-only position and size properties.
 * @return Native lv_tabview object, or NULL for invalid arguments.
 */
lv_obj_t *ui_comp_tabview_create(lv_obj_t *parent,
                                 const ui_comp_tabview_props_t *props);

#endif /* __UI_COMP_TABVIEW_H__ */
