#ifndef __IVW_MODULE_H__
#define __IVW_MODULE_H__

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>

#ifdef __cplusplus
extern "C" {
#endif

// IVW error codes
#define IVW_OK                      (0)
#define IVW_ERROR_PARAM             (-1)
#define IVW_ERROR_INIT              (-2)
#define IVW_ERROR_NOT_INIT          (-3)
#define IVW_ERROR_CREATE            (-4)
#define IVW_ERROR_START             (-5)
#define IVW_ERROR_WRITE             (-6)

// IVW callback function type
typedef int (*ivw_wakeup_callback_t)(const char* param, void* userparam);

// IVW configuration structure
typedef struct {
    const char* mlp_res_data;      // MLP资源数据指针
    size_t mlp_res_size;            // MLP资源数据大小
    const char* keyword_res_data;   // 关键词资源数据指针
    size_t keyword_res_size;        // 关键词资源数据大小
    ivw_wakeup_callback_t wakeup_callback;
    void* user_param;
} ivw_config_t;

/**
 * @brief Initialize IVW module
 * @param config IVW configuration
 * @return IVW_OK on success, error code on failure
 */
int ivw_module_init(const ivw_config_t* config);

/**
 * @brief Deinitialize IVW module
 * @return IVW_OK on success, error code on failure
 */
int ivw_module_deinit(void);

/**
 * @brief Check if IVW module is initialized
 * @return 1 if initialized, 0 if not
 */
int ivw_module_is_initialized(void);

/**
 * @brief Write audio data to IVW engine for wake word detection
 * @param audio_data Preprocessed audio data (already applied gain if needed)
 * @param byte_size Size of audio data in bytes
 * @return IVW_OK on success, error code on failure
 */
int ivw_module_process_audio(const int16_t* audio_data, int byte_size);


const char* ivw_module_get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* __IVW_MODULE_H__ */