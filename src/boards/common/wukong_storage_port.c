/**
 * @file wukong_storage_port.c
 * @brief Board-level strong impl of wukong_storage_port_mkfs(): first-time
 *        littlefs format of a blank external QSPI flash (T5 / Beken).
 *
 * The vendor littlefs_mount() has no auto-mkfs, so a factory-fresh chip can
 * never mount until formatted once. Reference: vendor/T5/tuyaos/tuyaos_adapter/
 * src/test/test_littlefs.c and src/misc/fs_init.c. This app's active T5
 * sdkconfig does not set CONFIG_TUYA_USE_MTD (CONFIG_TUYA_QSPI_FLASH_TYPE=
 * "gd25q127c", a NOR QSPI part queried via tuya_qspi_device_query()), so only
 * the QSPI-NOR path is implemented here; add the CONFIG_TUYA_USE_MTD branch
 * (tuya_mtd_device_query()) if a future board config selects it.
 * Compiled to nothing unless WUKONG_STORAGE_EXT_FLASH is selected.
 *
 * @copyright Copyright (c) Tuya Inc.
 */
#include "wukong_storage.h"

#if defined(WUKONG_STORAGE_EXT_FLASH) && (WUKONG_STORAGE_EXT_FLASH == 1)

#include "tal_log.h"
#include "bk_posix.h"
#include <driver/qspi_flash_common.h>

OPERATE_RET wukong_storage_port_mkfs(VOID)
{
    struct bk_little_fs_partition partition = {0};

    qspi_driver_desc_t *qflash_dev = tuya_qspi_device_query(CONFIG_TUYA_QSPI_FLASH_TYPE);
    if (qflash_dev == NULL) {
        TAL_PR_ERR("storage mkfs: qspi flash %s not found", CONFIG_TUYA_QSPI_FLASH_TYPE);
        return OPRT_NOT_FOUND;
    }

    partition.part_type             = LFS_QSPI_FLASH;
    partition.part_flash.start_addr = 0;
    partition.part_flash.size       = qflash_dev->total_size;
    partition.part_flash.page_size  = qflash_dev->page_size;
    partition.part_flash.block_size = qflash_dev->block_size;
    partition.mount_path            = WUKONG_STORAGE_ROOT;

    TAL_PR_NOTICE("storage mkfs: littlefs on qspi flash, total=%u block=%u",
                  (unsigned)qflash_dev->total_size, (unsigned)qflash_dev->block_size);

    if (mkfs("PART_NONE", "littlefs", &partition) != 0) {
        TAL_PR_ERR("storage mkfs failed");
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

#endif /* WUKONG_STORAGE_EXT_FLASH */
