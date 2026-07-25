#ifndef __TUYA_P2P_SDK_H__
#define __TUYA_P2P_SDK_H__

#include "tuya_ipc_media.h"
#include "tuya_ipc_media_stream.h"

/**
 * @brief Tuya IPC SDK variable structure
 * 
 * This structure encapsulates the main configuration and runtime variables
 * for the Tuya IPC (IP Camera) SDK. It contains media adapter configuration,
 * media information, and media stream variables required for P2P video streaming.
 * 
 * @struct tagTuyaIpcSdkVar
 * 
 * @var tagTuyaIpcSdkVar::media_var
 * Media adapter variables containing configuration for media handling
 * 
 * @var tagTuyaIpcSdkVar::media_info
 * IPC media information including codec, resolution, and streaming parameters
 * 
 * @var tagTuyaIpcSdkVar::media_adatper_info
 * Media stream adapter information for managing streaming sessions
 */
typedef struct tagTuyaIpcSdkVar
{
    TUYA_IPC_MEDIA_ADAPTER_VAR_T media_var;
    IPC_MEDIA_INFO_T media_info;
    MEDIA_STREAM_VAR_T media_adatper_info;
}TUYA_IPC_SDK_VAR_S;

/**
 * @brief Initialize the Tuya P2P SDK with the specified configuration
 * 
 * This function initializes the Tuya Peer-to-Peer SDK using the provided
 * SDK variable structure. It must be called before using any other P2P
 * SDK functions.
 * 
 * @param[in] sdk_var Pointer to the Tuya IPC SDK variable structure containing
 *                    initialization parameters and configuration settings
 * 
 * @return OPERATE_RET Operation result code, refer to tuya_error_code.h for details
 * 
 * @note This function should only be called once during application startup
 * @note The sdk_var structure must be properly initialized before calling this function
 */
OPERATE_RET tuya_p2p_sdk_init(TUYA_IPC_SDK_VAR_S *sdk_var);

/**
 * @brief Subscribe to IoT initialization event
 * 
 * This function subscribes to the IoT initialization complete event (EVENT_INIT).
 * When the Tuya IoT platform initialization finishes, the registered callback
 * (OnIotInited) will be invoked to handle skill enabling and cloud parameter
 * configuration.
 * 
 * @return OPERATE_RET Operation result code, refer to tuya_error_code.h for details
 * 
 * @note This function must be called before tuya_iot_init_params()
 * @note Uses ty_subscribe_event internally to listen for EVENT_INIT
 */
OPERATE_RET tuya_p2p_sdk_subscribe_iot_init_event(VOID);

/**
 * @brief Terminate the Tuya P2P SDK and release resources
 * 
 * This function shuts down the Tuya Peer-to-Peer SDK and releases all
 * associated resources. It should be called when P2P functionality is
 * no longer needed or during application shutdown.
 * 
 * @return OPERATE_RET Operation result code, refer to tuya_error_code.h for details
 * 
 * @note After calling this function, tuya_p2p_sdk_init() must be called again
 *       before using any other P2P SDK functions
 * @note Ensure all ongoing P2P sessions are properly closed before calling this function
 */
OPERATE_RET tuya_p2p_sdk_end();

/**
 * @brief Send a video frame through the P2P connection
 * 
 * This function inputs a video frame to be transmitted over the P2P channel.
 * The frame will be encoded and sent to connected peers.
 * 
 * @param[in] p_frame Pointer to the media frame structure containing video data
 *                    Must include frame data, size, timestamp, and format information
 * 
 * @return OPERATE_RET Operation result code, refer to tuya_error_code.h for details
 * 
 * @note The frame data should be in the format specified during SDK initialization
 * @note This function is non-blocking; frames are queued for transmission
 * @note Ensure p_frame is valid and contains properly formatted video data
 */
OPERATE_RET tuya_p2p_sdk_put_video_frame(IN CONST MEDIA_FRAME_T *p_frame);

/**
 * @brief Send an audio frame through the P2P connection
 * 
 * This function inputs an audio frame to be transmitted over the P2P channel.
 * The frame will be encoded and sent to connected peers.
 * 
 * @param[in] p_frame Pointer to the media frame structure containing audio data
 *                    Must include frame data, size, timestamp, and format information
 * 
 * @return OPERATE_RET Operation result code, refer to tuya_error_code.h for details
 * 
 * @note The frame data should be in the format specified during SDK initialization
 * @note This function is non-blocking; frames are queued for transmission
 * @note Ensure p_frame is valid and contains properly formatted audio data
 */
OPERATE_RET tuya_p2p_sdk_put_audio_frame(IN CONST MEDIA_FRAME_T *p_frame);

#endif
