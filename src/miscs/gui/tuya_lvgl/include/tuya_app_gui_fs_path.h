#ifndef __GUI_TOP_PATH_H__
#define __GUI_TOP_PATH_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GUI_RESOURCE_MANAGMENT_VER_1_0      //家电GUI管理方案1.0,资源平台化

/**
 * @brief little fs partition type
 */
typedef BYTE_T TUYA_GUI_LFS_PARTITION_TYPE_E;
#define TUYA_GUI_LFS_SPI_FLASH_UNKNOWN  0               //表示未知
#define TUYA_GUI_LFS_SPI_FLASH          1               //表示内部flash: 从3.13.x的sdk开始不再支持片内文件系统!
#define TUYA_GUI_LFS_QSPI_FLASH         2               //表示外部qspi-flash
#define TUYA_GUI_LFS_SD                 3               //表示外部sd卡

/**
 * @brief fs ping-pong path define
 */
typedef enum {
    TUYA_GUI_FS_PP_UNKNOWN = 0,             //表示未知
    TUYA_GUI_FS_PP_MAIN,                    //表示 GUI_TOP_LEVEL_DIR
    TUYA_GUI_FS_PP_EXT,                     //表示 GUI_TOP_LEVEL_TMP_DIR
} TUYA_GUI_FS_PINPONG_E;

#define GUI_FS_PATH_INFO            "fspathtype"            //kv保存的文件路径类型信息

#define GUI_DEV_FS_PARTITION       TUYA_GUI_LFS_QSPI_FLASH  //文件系统所在位置:片内/片外flash等

/*********************************文件系统一级目录*********************************/
#define GUI_DEV_FS_LETTER       "/"                                        //文件系统盘指示符:片内/片外flash,sd卡等(暂时不要改动,目前和lvgl中letter保持一致!)
#define GUI_TOP_LEVEL_DIR       GUI_DEV_FS_LETTER"t5_fs/"                  //文件系统最上层目录
#define GUI_TOP_LEVEL_TMP_DIR   GUI_DEV_FS_LETTER"t5_fs_tmp/"              //文件系统最上层目录(临时下载文件保存的最顶层目录)

/*********************************文件系统二级目录*********************************/
#define RES_DATA_FILE_PATH          GUI_TOP_LEVEL_DIR"data_file/"
#define RES_FONT_PATH               GUI_TOP_LEVEL_DIR"font/"
#define RES_MUSIC_PATH              GUI_TOP_LEVEL_DIR"music/"
#define RES_PIC_PATH                GUI_TOP_LEVEL_DIR"picture/"
#define RES_VERSION                 GUI_TOP_LEVEL_DIR"version.inf"
//下载文件临时保存目录
#define RES_DATA_FILE_TMP_PATH      GUI_TOP_LEVEL_TMP_DIR"data_file/"
#define RES_FONT_TMP_PATH           GUI_TOP_LEVEL_TMP_DIR"font/"
#define RES_MUSIC_TMP_PATH          GUI_TOP_LEVEL_TMP_DIR"music/"
#define RES_PIC_TMP_PATH            GUI_TOP_LEVEL_TMP_DIR"picture/"
#define RES_TMP_VERSION             GUI_TOP_LEVEL_TMP_DIR"version.inf"

/*********************************文件系统三级目录*********************************/
#define UI_DISPLAY_NAME_CH      "ch"
#define UI_DISPLAY_NAME_EN      "en"
#define UI_DISPLAY_NAME_KR      "kr"
#define UI_DISPLAY_NAME_JP      "jp"
#define UI_DISPLAY_NAME_FR      "fr"
#define UI_DISPLAY_NAME_DE      "de"
#define UI_DISPLAY_NAME_RU      "ru"
#define UI_DISPLAY_NAME_IN      "in"
#define UI_DISPLAY_NAME_ALL     "all"           //所有集中语言配置目录名

#define CONCAT_STRINGS(a, b, c) a b c
#define UI_DISPLAY_FILE_PATH_CH             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_CH, "/")            //中文
#define UI_DISPLAY_FILE_PATH_EN             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_EN, "/")            //英文
#define UI_DISPLAY_FILE_PATH_KR             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_KR, "/")            //韩文
#define UI_DISPLAY_FILE_PATH_JP             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_JP, "/")            //日文
#define UI_DISPLAY_FILE_PATH_FR             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_FR, "/")            //法文
#define UI_DISPLAY_FILE_PATH_DE             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_DE, "/")            //德文
#define UI_DISPLAY_FILE_PATH_RU             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_RU, "/")            //俄文
#define UI_DISPLAY_FILE_PATH_IN             CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_IN, "/")            //印度文
#define UI_DISPLAY_FILE_PATH_ALL            CONCAT_STRINGS(RES_DATA_FILE_PATH, UI_DISPLAY_NAME_ALL, "/")            //所有集中语言
//下载文件临时保存目录
#define UI_DISPLAY_FILE_TMP_PATH_CH         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_CH, "/")            //中文
#define UI_DISPLAY_FILE_TMP_PATH_EN         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_EN, "/")            //英文
#define UI_DISPLAY_FILE_TMP_PATH_KR         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_KR, "/")            //韩文
#define UI_DISPLAY_FILE_TMP_PATH_JP         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_JP, "/")            //日文
#define UI_DISPLAY_FILE_TMP_PATH_FR         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_FR, "/")            //法文
#define UI_DISPLAY_FILE_TMP_PATH_DE         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_DE, "/")            //德文
#define UI_DISPLAY_FILE_TMP_PATH_RU         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_RU, "/")            //俄文
#define UI_DISPLAY_FILE_TMP_PATH_IN         CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_IN, "/")            //印度文
#define UI_DISPLAY_FILE_TMP_PATH_ALL        CONCAT_STRINGS(RES_DATA_FILE_TMP_PATH, UI_DISPLAY_NAME_ALL, "/")            //所有集中语言

/*********************************文件路径及文件名长度定义*********************************/
#define UI_FILE_PATH_MAX_LEN            128
#define UI_FILE_NAME_MAX_LEN            32

/**
 * @brief: 设置文件系统分区类型
 * @desc: 设置文件系统分区类型
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET tuya_app_gui_set_lfs_partiton_type(TUYA_GUI_LFS_PARTITION_TYPE_E partition_type);

/**
 * @brief: 获取文件系统分区类型
 * @desc: 获取文件系统分区类型
 * @return 执行结果,文件系统分区类型
 * @note
 */
TUYA_GUI_LFS_PARTITION_TYPE_E tuya_app_gui_get_lfs_partiton_type(VOID_T);

/**
 * @brief: 设置文件系统路径类型
 * @desc: 设置文件系统保存路径类型
 * @return 执行结果,返回 OPRT_OK表示成功，返回 其他 则表示失败
 * @note
 */
OPERATE_RET tuya_app_gui_set_path_type(TUYA_GUI_FS_PINPONG_E path_type);

/**
 * @brief: 获取文件系统路径类型
 * @desc: 获取文件系统路径类型
 * @return 执行结果,文件系统路径类型
 * @note
 */
TUYA_GUI_FS_PINPONG_E tuya_app_gui_get_path_type(VOID_T);

/**
 * @brief: 获取文件系统挂载点
 */
CONST CHAR_T *tuya_app_gui_get_disk_mount_path(VOID_T);

/**
 * @brief: 获取文件系统顶层目录名
 */
CONST CHAR_T *tuya_app_gui_get_top_path(VOID_T);

/**
 * @brief: 获取文件系统语言配置目录名
 */
CONST CHAR_T *tuya_app_gui_get_data_file_path(VOID_T);

/**
 * @brief: 获取文件系统字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_font_path(VOID_T);

/**
 * @brief: 获取文件系统音乐目录名
 */
CONST CHAR_T *tuya_app_gui_get_music_path(VOID_T);

/**
 * @brief: 根据文件名获取文件系统音乐完整路径名
 */
CHAR_T *tuya_app_gui_get_music_full_path(IN CHAR_T *music_name);

/**
 * @brief: 获取文件系统图片目录名
 */
CONST CHAR_T *tuya_app_gui_get_picture_path(VOID_T);

/**
 * @brief: 根据文件名获取文件系统图片完整路径名
 */
CHAR_T *tuya_app_gui_get_picture_full_path(IN CHAR_T *pic_name);

/**
 * @brief: 获取文件系统版本信息目录名
 */
CONST CHAR_T *tuya_app_gui_get_version_path(VOID_T);

/**
 * @brief: 获取文件系统中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_ch_path(VOID_T);

/**
 * @brief: 获取文件系统英文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_en_path(VOID_T);

/**
 * @brief: 获取文件系统韩文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_kr_path(VOID_T);

/**
 * @brief: 获取文件系统日文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_jp_path(VOID_T);

/**
 * @brief: 获取文件系统法文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_fr_path(VOID_T);

/**
 * @brief: 获取文件系统德文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_de_path(VOID_T);

/**
 * @brief: 获取文件系统俄文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_ru_path(VOID_T);

/**
 * @brief: 获取文件系统印度文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_in_path(VOID_T);

/**
 * @brief: 获取文件系统所有集中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_lang_all_path(VOID_T);

/****************************************************获取未使用目录相关信息**************************************************/
/**
 * @brief: 获取未使用的文件系统顶层目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_top_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统语言配置目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_data_file_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_font_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统音乐目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_music_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统图片目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_picture_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统版本信息目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_version_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_ch_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统英文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_en_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统韩文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_kr_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统日文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_jp_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统法文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_fr_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统德文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_de_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统俄文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_ru_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统印度文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_in_path(VOID_T);

/**
 * @brief: 获取未使用的文件系统所有集中文字库目录名
 */
CONST CHAR_T *tuya_app_gui_get_unused_lang_all_path(VOID_T);

/****************************************************获取未使用目录相关信息**************************************************/

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif

