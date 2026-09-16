#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#include "tuya_app_config.h"   /* UI_WUKONG_PAGES */

/* Color */
#define LV_COLOR_DEPTH          16
#define LV_COLOR_16_SWAP        0

/* Memory */
#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   "tkl_memory.h"
#define LV_MEM_CUSTOM_ALLOC     tkl_system_malloc
#define LV_MEM_CUSTOM_FREE      tkl_system_free
#define LV_MEM_CUSTOM_REALLOC   tkl_system_realloc

/* Tick */
#define LV_TICK_CUSTOM              1
#define LV_TICK_CUSTOM_INCLUDE      "tkl_system.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (tkl_system_get_millisecond())

/* Display */
#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          480
#define LV_DPI_DEF              130

/* Drawing */
#define LV_DRAW_COMPLEX         1
#define LV_SHADOW_CACHE_SIZE    0
#define LV_IMG_CACHE_DEF_SIZE   0

/* GPU */
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_ARM2D        0

/* Layout */
#define LV_USE_FLEX             1
#define LV_USE_GRID             0

/* Logging */
#define LV_USE_LOG              0

/* Font rendering */
#define LV_USE_FONT_COMPRESSED  1

/* Fonts */
#define LV_FONT_MONTSERRAT_8    0
#define LV_FONT_MONTSERRAT_10   0
#define LV_FONT_MONTSERRAT_12   0
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   0
#define LV_FONT_MONTSERRAT_18   0
#define LV_FONT_MONTSERRAT_20   0
#define LV_FONT_MONTSERRAT_22   0
#define LV_FONT_MONTSERRAT_24   0   /* replaced by symbol-icon subset in assets/font/ (same
                                     * font name); never set back to 1 — duplicate symbol */
#define LV_FONT_MONTSERRAT_26   0
#define LV_FONT_MONTSERRAT_28   0   /* replaced by digits subset in assets/font/, see _24 */
#define LV_FONT_MONTSERRAT_30   0
#define LV_FONT_MONTSERRAT_32   0
#define LV_FONT_MONTSERRAT_34   0
#define LV_FONT_MONTSERRAT_36   0
#define LV_FONT_MONTSERRAT_38   0
#define LV_FONT_MONTSERRAT_40   0
#define LV_FONT_MONTSERRAT_42   0
#define LV_FONT_MONTSERRAT_44   0
#define LV_FONT_MONTSERRAT_46   0
#define LV_FONT_MONTSERRAT_48   0   /* replaced by digits subset in assets/font/, see _24 */
#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
/* Project default font: Alibaba PuHui Ti 3 Regular 18px (CJK + Latin).
 * CUSTOM_DECLARE makes the symbol visible to LVGL before LV_FONT_DEFAULT uses it.
 * The lv_font_montserrat_* below are NOT the LVGL built-ins (disabled above)
 * but the app's subsets in assets/font/: 48/28 = timer digits, 24 = the
 * player/file LV_SYMBOL icons. */
#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(AlibabaPuHuiTi3_Regular18) \
                                LV_FONT_DECLARE(lv_font_montserrat_48) \
                                LV_FONT_DECLARE(lv_font_montserrat_28) \
                                LV_FONT_DECLARE(lv_font_montserrat_24)
#define LV_FONT_DEFAULT         &AlibabaPuHuiTi3_Regular18
#else
/* Board-UI builds (UI_WUKONG_PAGES=n) don't compile assets/font/, so fall back
 * to the always-enabled LVGL built-in (see LV_FONT_MONTSERRAT_14 above). */
#define LV_FONT_DEFAULT         &lv_font_montserrat_14
#endif

/* Extra widgets */
#define LV_USE_ANIMIMG          0
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_COLORWHEEL       0
#define LV_USE_IMGBTN           0
#define LV_USE_KEYBOARD         0
#define LV_USE_LED              0
#define LV_USE_LIST             0
#define LV_USE_MENU             0
#define LV_USE_METER            0
#define LV_USE_MSGBOX           0
#define LV_USE_SPAN             0
#define LV_USE_SPINBOX          0
#define LV_USE_SPINNER          1
#define LV_USE_TABVIEW          1
#define LV_USE_TILEVIEW         0
#define LV_USE_WIN              0

/* Filesystem */
#define LV_USE_FS_STDIO         0
#define LV_USE_FS_POSIX         0

/* GIF decoder retired — board emotion UIs (ROBOT/EYES) migrated to lv_tef
 * (src/ui/widgets/lv_tef.[ch]); no lv_gif consumers remain in the repo. */
#define LV_USE_GIF              0

/* PNG decoder (lodepng) — EVB/EVB_PRO emoji images are PNG stored as
 * LV_IMG_CF_RAW_ALPHA; auto-registered by lv_extra_init() -> lv_png_init() */
#define LV_USE_PNG              1

/* QR code (qrcodegen, canvas-based) — activation short-URL page. Wukong pages
 * only: board UIs have no consumer, so keep qrcodegen out of their firmware.
 * Depends on LV_USE_CANVAS, which defaults to 1 in lv_conf_internal.h. */
#if defined(UI_WUKONG_PAGES) && UI_WUKONG_PAGES
#define LV_USE_QRCODE           1
#endif

/* Image font — EVB/EVB_PRO build their emoji font (font_emoji_64) from images
 * via lv_imgfont_create */
#define LV_USE_IMGFONT          1

/* Assert */
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

#endif /* LV_CONF_H */
