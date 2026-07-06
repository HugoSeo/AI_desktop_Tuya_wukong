/**
 * @file tuya_app_gui_fs_path.c
 * @author www.tuya.com
 * @brief tuya_app_gui_fs_path module is used to 
 * @version 0.1
 * @date 2022-10-28
 *
 * @copyright Copyright (c) tuya.inc 2022
 *
 */

#include <stdio.h>
#include "tuya_app_gui_fs_path.h"
#include "tuya_app_gui_core_config.h"

STATIC TUYA_GUI_FS_PINPONG_E gui_fs_path_type = TUYA_GUI_FS_PP_MAIN;
STATIC TUYA_GUI_LFS_PARTITION_TYPE_E gui_lfs_part_type = GUI_DEV_FS_PARTITION;

/**
 * @brief: 设置文件系统分区类型
 * @desc: 设置文件系统分区类型
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET tuya_app_gui_set_lfs_partiton_type(TUYA_GUI_LFS_PARTITION_TYPE_E partition_type)
{
    OPERATE_RET rt = OPRT_OK;

    if (partition_type == TUYA_GUI_LFS_SPI_FLASH || partition_type == TUYA_GUI_LFS_QSPI_FLASH || partition_type == TUYA_GUI_LFS_SD) {
        gui_lfs_part_type = partition_type;
        TY_GUI_LOG_PRINT("%s:%d=================================part type '%d'\r\n", __func__, __LINE__, gui_lfs_part_type);
    }
    else {
        TY_GUI_LOG_PRINT("%s:%d=================================err part type '%d', use default qspi-flash!\r\n", __func__, __LINE__, partition_type);
        rt = OPRT_INVALID_PARM;
    }
    return rt;
}

/**
 * @brief: 获取文件系统分区类型
 * @desc: 获取文件系统分区类型
 * @return 执行结果,文件系统分区类型
 * @note
 */
TUYA_GUI_LFS_PARTITION_TYPE_E tuya_app_gui_get_lfs_partiton_type(VOID_T)
{
    return gui_lfs_part_type;
}

/**
 * @brief: 设置文件系统路径类型
 * @desc: 设置文件系统保存路径类型
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET tuya_app_gui_set_path_type(TUYA_GUI_FS_PINPONG_E path_type)
{
    OPERATE_RET rt = OPRT_OK;

    if (path_type == TUYA_GUI_FS_PP_MAIN || path_type == TUYA_GUI_FS_PP_EXT) {
        gui_fs_path_type = path_type;
        TY_GUI_LOG_PRINT("%s:%d=================================path type '%d'\r\n", __func__, __LINE__, gui_fs_path_type);
    }
    else {
        TY_GUI_LOG_PRINT("%s:%d=================================err path type '%d', set to default main!\r\n", __func__, __LINE__, path_type);
        gui_fs_path_type = TUYA_GUI_FS_PP_MAIN;         //set default path as main
        rt = OPRT_INVALID_PARM;
    }
    return rt;
}

/**
 * @brief: 获取文件系统路径类型
 * @desc: 获取文件系统路径类型
 * @return 执行结果,文件系统路径类型
 * @note
 */
TUYA_GUI_FS_PINPONG_E tuya_app_gui_get_path_type(VOID_T)
{
    return gui_fs_path_type;
}

/**
 * @brief: 获取文件系统挂载点
 */
CONST CHAR_T *tuya_app_gui_get_disk_mount_path(VOID_T)
{
    return GUI_DEV_FS_LETTER;
}

/**
 * @brief: 获取文件系统顶层目录名
 */
CONST CHAR_T *tuya_app_gui_get_top_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return GUI_TOP_LEVEL_DIR;
    else
        return GUI_TOP_LEVEL_TMP_DIR;
}

/**
 * @brief: 获取文件系统语言配置目录名
 */
CONST CHAR_T *tuya_app_gui_get_data_file_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_DATA_FILE_PATH;
    else
        return RES_DATA_FILE_TMP_PATH;
}

/**
 * @brief: 获取文件系统字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_font_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_FONT_PATH;
    else
        return RES_FONT_TMP_PATH;
}

/**
 * @brief: 获取文件系统音乐目录名
 */
CONST CHAR_T *tuya_app_gui_get_music_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_MUSIC_PATH;
    else
        return RES_MUSIC_TMP_PATH;
}

/**
 * @brief: 根据文件名获取文件系统音乐完整路径名
 */
CHAR_T *tuya_app_gui_get_music_full_path(IN CHAR_T *music_name)
{
    STATIC CHAR_T full_music_path[UI_FILE_PATH_MAX_LEN] = {0};

    memset(full_music_path, 0, UI_FILE_PATH_MAX_LEN);
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN) {
        snprintf(full_music_path, UI_FILE_PATH_MAX_LEN, "%s%s", RES_MUSIC_PATH, music_name);
    }
    else {
        snprintf(full_music_path, UI_FILE_PATH_MAX_LEN, "%s%s", RES_MUSIC_TMP_PATH, music_name);
    }
    return full_music_path;
}

/**
 * @brief: 获取文件系统图片目录名
 */
CONST CHAR_T *tuya_app_gui_get_picture_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_PIC_PATH;
    else
        return RES_PIC_TMP_PATH;
}

/**
 * @brief: 根据文件名获取文件系统图片完整路径名
 */
CHAR_T *tuya_app_gui_get_picture_full_path(IN CHAR_T *pic_name)
{
    STATIC CHAR_T full_pic_path[UI_FILE_PATH_MAX_LEN] = {0};

    memset(full_pic_path, 0, UI_FILE_PATH_MAX_LEN);
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN) {
        snprintf(full_pic_path, UI_FILE_PATH_MAX_LEN, "%s%s", RES_PIC_PATH, pic_name);
    }
    else {
        snprintf(full_pic_path, UI_FILE_PATH_MAX_LEN, "%s%s", RES_PIC_TMP_PATH, pic_name);
    }
    return full_pic_path;
}

/**
 * @brief: 获取文件系统版本信息目录名
 */
CONST CHAR_T *tuya_app_gui_get_version_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_VERSION;
    else
        return RES_TMP_VERSION;
}

/**
 * @brief: 获取文件系统中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_ch_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_CH;
    else
        return UI_DISPLAY_FILE_TMP_PATH_CH;
}

/**
 * @brief: 获取文件系统英文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_en_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_EN;
    else
        return UI_DISPLAY_FILE_TMP_PATH_EN;
}

/**
 * @brief: 获取文件系统韩文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_kr_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_KR;
    else
        return UI_DISPLAY_FILE_TMP_PATH_KR;
}

/**
 * @brief: 获取文件系统日文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_jp_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_JP;
    else
        return UI_DISPLAY_FILE_TMP_PATH_JP;
}

/**
 * @brief: 获取文件系统法文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_fr_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_FR;
    else
        return UI_DISPLAY_FILE_TMP_PATH_FR;
}

/**
 * @brief: 获取文件系统德文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_de_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_DE;
    else
        return UI_DISPLAY_FILE_TMP_PATH_DE;
}

/**
 * @brief: 获取文件系统俄文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_ru_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_RU;
    else
        return UI_DISPLAY_FILE_TMP_PATH_RU;
}

/**
 * @brief: 获取文件系统印度文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_in_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_IN;
    else
        return UI_DISPLAY_FILE_TMP_PATH_IN;
}

/**
 * @brief: 获取文件系统所有集中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_all_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_PATH_ALL;
    else
        return UI_DISPLAY_FILE_TMP_PATH_ALL;
}

/****************************************************获取未使用目录相关信息**************************************************/
/**
 * @brief: 获取未使用的文件系统顶层目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_top_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return GUI_TOP_LEVEL_TMP_DIR;
    else
        return GUI_TOP_LEVEL_DIR;
}

/**
 * @brief: 获取未使用的文件系统语言配置目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_data_file_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_DATA_FILE_TMP_PATH;
    else
        return RES_DATA_FILE_PATH;
}

/**
 * @brief: 获取未使用的文件系统字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_font_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_FONT_TMP_PATH;
    else
        return RES_FONT_PATH;
}

/**
 * @brief: 获取未使用的文件系统音乐目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_music_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_MUSIC_TMP_PATH;
    else
        return RES_MUSIC_PATH;
}

/**
 * @brief: 获取未使用的文件系统图片目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_picture_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_PIC_TMP_PATH;
    else
        return RES_PIC_PATH;
}

/**
 * @brief: 获取未使用的文件系统版本信息目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_version_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return RES_TMP_VERSION;
    else
        return RES_VERSION;
}

/**
 * @brief: 获取未使用的文件系统中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_ch_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_CH;
    else
        return UI_DISPLAY_FILE_PATH_CH;
}

/**
 * @brief: 获取未使用的文件系统英文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_en_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_EN;
    else
        return UI_DISPLAY_FILE_PATH_EN;
}

/**
 * @brief: 获取未使用的文件系统韩文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_kr_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_KR;
    else
        return UI_DISPLAY_FILE_PATH_KR;
}

/**
 * @brief: 获取未使用的文件系统日文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_jp_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_JP;
    else
        return UI_DISPLAY_FILE_PATH_JP;
}

/**
 * @brief: 获取未使用的文件系统法文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_fr_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_FR;
    else
        return UI_DISPLAY_FILE_PATH_FR;
}

/**
 * @brief: 获取未使用的文件系统德文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_de_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_DE;
    else
        return UI_DISPLAY_FILE_PATH_DE;
}

/**
 * @brief: 获取未使用的文件系统俄文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_ru_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_RU;
    else
        return UI_DISPLAY_FILE_PATH_RU;
}

/**
 * @brief: 获取未使用的文件系统印度文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_in_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_IN;
    else
        return UI_DISPLAY_FILE_PATH_IN;
}

/**
 * @brief: 获取未使用的文件系统所有集中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_all_path(VOID_T)
{
    if (gui_fs_path_type == TUYA_GUI_FS_PP_MAIN)
        return UI_DISPLAY_FILE_TMP_PATH_ALL;
    else
        return UI_DISPLAY_FILE_PATH_ALL;
}
/****************************************************获取未使用目录相关信息**************************************************/