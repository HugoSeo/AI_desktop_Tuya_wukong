/*
 * lv_littlefs.c
 * Copyright (C) 2024 cc <cc@tuya>
 *
 * Distributed under terms of the MIT license.
 */
#include "lv_littlefs.h"

#if 0
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include <os/os.h>
#include <common/bk_include.h>
#include "bk_posix.h"
#include "driver/flash_partition.h"


void lv_lfs_init(void)
{
    // CPU0 mount
    return;

    struct bk_little_fs_partition partition;
    char *fs_name = NULL;
    bk_logic_partition_t *pt = bk_flash_partition_get_info(BK_PARTITION_USR_CONFIG);
    int ret, retry = 0;

    fs_name = "littlefs";
    partition.part_type = LFS_FLASH;
    partition.part_flash.start_addr = pt->partition_start_addr;
    partition.part_flash.size = pt->partition_length;
    partition.mount_path = "/";

    do {
        ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);
        if (ret == BK_OK) {
            break;
        }
        retry++;
        tkl_system_sleep(50);
    } while ((ret != BK_OK) && (retry < 3));

    TY_GUI_LOG_PRINT("mount littlefs %s\r\n", (ret == BK_OK)? "ok": "failed");

    return ret;
}

void lv_lfs_deinit(void)
{
    // CPU0 mount
    return;

    umount("/");
}

#endif

