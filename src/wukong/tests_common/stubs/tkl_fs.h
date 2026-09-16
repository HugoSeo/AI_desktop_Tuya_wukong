/*
 * Host-test stub of the tkl_fs adapter header: just the subset the storage
 * record layer (wukong_storage_record.c) uses. Signatures mirror the vendor
 * tkl_fs.h; the POSIX-backed implementation lives in
 * tests_common/stub_tkl_fs.c.
 */
#ifndef __TKL_FS_H__
#define __TKL_FS_H__

#include "tuya_cloud_types.h"

typedef VOID_T *TUYA_FILE;
typedef VOID_T *TUYA_DIR;
typedef VOID_T *TUYA_FILEINFO;

INT_T tkl_fs_mkdir_r(CONST CHAR_T *path);
INT_T tkl_fs_remove(CONST CHAR_T *path);
INT_T tkl_fs_rename(CONST CHAR_T *path_old, CONST CHAR_T *path_new);

TUYA_FILE tkl_fopen(CONST CHAR_T *path, CONST CHAR_T *mode);
INT_T tkl_fclose(TUYA_FILE file);
INT_T tkl_fread(VOID_T *buf, INT_T bytes, TUYA_FILE file);
INT_T tkl_fwrite(VOID_T *buf, INT_T bytes, TUYA_FILE file);
INT_T tkl_fsync(INT_T fd);
INT_T tkl_fgetsize(CONST CHAR_T *filepath);
INT_T tkl_fileno(TUYA_FILE file);

INT_T tkl_dir_open(CONST CHAR_T *path, TUYA_DIR *dir);
INT_T tkl_dir_close(TUYA_DIR dir);
INT_T tkl_dir_read(TUYA_DIR dir, TUYA_FILEINFO *info);
INT_T tkl_dir_name(TUYA_FILEINFO info, CONST CHAR_T **name);
INT_T tkl_dir_is_regular(TUYA_FILEINFO info, BOOL_T *is_regular);
INT_T tkl_dir_is_directory(TUYA_FILEINFO info, BOOL_T *is_dir);

#endif /* __TKL_FS_H__ */
