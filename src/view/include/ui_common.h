/**
 * @file ui_common.h
 * @brief Umbrella header: pulls in shared types and every UI module API
 *
 * Kept as a single include for legacy .c files that don't yet narrow
 * their dependencies. New code should prefer the per-module header
 * (e.g. include "ui_chat.h" instead of "ui_common.h") for tighter
 * dependency boundaries.
 *
 * @version 1.0
 * @date 2025-04-02
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __UI_COMMON_H__
#define __UI_COMMON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "tuya_ai_display.h"
#include "wukong_picture.h"
#include "lvgl.h"
#include "uni_log.h"

#include "ui_theme.h"
#include "ui_nav.h"
#include "ui_action.h"
#include "ui_startup.h"
#include "ui_home.h"
#include "ui_chat.h"
#include "ui_camera.h"
#include "ui_album.h"
#include "ui_album_grid.h"
#include "ui_device_mode.h"
#include "ui_control_center.h"
#include "ui_record.h"
#include "ui_record_list.h"
#include "ui_music.h"
#include "ui_music_list.h"
#include "ui_call.h"
#include "ui_detection.h"
#include "ui_settings.h"

#ifdef __cplusplus
}
#endif

#endif /* __UI_COMMON_H__ */
