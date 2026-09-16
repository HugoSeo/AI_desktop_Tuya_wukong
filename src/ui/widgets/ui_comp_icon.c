#include "ui_comp_icon.h"

lv_obj_t *ui_comp_icon_create(lv_obj_t *parent, const void *src) {
    lv_obj_t *img = lv_img_create(parent);
    if (src) lv_img_set_src(img, src);
    return img;
}

void ui_comp_icon_set_src(lv_obj_t *icon, const void *src) {
    lv_img_set_src(icon, src);
}
