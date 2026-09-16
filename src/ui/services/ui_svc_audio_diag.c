#include "ui_svc_audio_diag.h"
#include "ui_svc_fs.h"
#include "audio_dump.h"
#include "uni_log.h"             /* PR_ERR (matches the other ui_svc_* services) */
#include "tal_workq_service.h"   /* WORKQ_SYSTEM, tal_workq_schedule */
#include <string.h>

/* Worker (WORKQ_SYSTEM): does the actual channel switch off the UI thread.
 * audio_dump_set_channel() can block on SD I/O (mkdir + flush of up to 5×1MB
 * buffers when leaving the SDCARD channel), so it must not run on the UI thread.
 * @arg carries the channel value directly (uintptr_t-in-pointer, not malloc'd —
 * the cb never frees it). For SDCARD the dir is (re)built here so the mkdir in
 * ui_fs_path() also stays off the UI thread. */
static void __audio_diag_apply_cb(void *arg)
{
    uint8_t ch = (uint8_t)(uintptr_t)arg;

    if (ch == AUDIO_DUMP_CH_SDCARD) {
        char dir[128];
        if (!ui_fs_ready()) {
            PR_ERR("audio_diag: fs not ready, cannot dump to SD");
            return;
        }
        /* /sdcard/tuyaos/audio_dump/ — ui_fs_path 递归建目录；name=NULL 取目录路径 */
        if (ui_fs_path(dir, sizeof(dir), "audio_dump", NULL) != OPRT_OK) {
            PR_ERR("audio_diag: build sd dir failed");
            return;
        }
        /* ui_fs_path 返回的目录不含末尾 '/'，后端按 "<dir><name>.pcm" 拼接，补一个 '/' */
        size_t n = strlen(dir);
        if (n + 1 < sizeof(dir) && (n == 0 || dir[n - 1] != '/')) {
            dir[n] = '/';
            dir[n + 1] = '\0';
        }
        audio_dump_set_channel(AUDIO_DUMP_CH_SDCARD, dir);
    } else {
        audio_dump_set_channel((AUDIO_DUMP_CHANNEL_E)ch, NULL);
    }
}

void ui_svc_audio_diag_channel_set(uint8_t ch)
{
    /* Offload to WORKQ_SYSTEM — the apply touches the SD card and must not block
     * the UI thread (called from the page's on_leave). */
    if (tal_workq_schedule(WORKQ_SYSTEM, __audio_diag_apply_cb,
                           (void *)(uintptr_t)ch) != OPRT_OK) {
        PR_ERR("audio_diag: schedule channel apply failed");
    }
}

uint32_t ui_svc_audio_diag_caps(void)
{
    return audio_dump_channel_caps();
}

bool ui_svc_audio_diag_available(void)
{
    /* 仅 OFF 可用 => 没有任何真实通道编译进来 => 不显示入口 */
    return audio_dump_channel_caps() != (1u << AUDIO_DUMP_CH_OFF);
}

uint8_t ui_svc_audio_diag_channel_get(void)
{
    return (uint8_t)audio_dump_get_channel();
}
