#ifndef __TUYA_P2P_APP_H__
#define __TUYA_P2P_APP_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Tuya P2P application service
 * 
 * This function initializes and starts the Tuya peer-to-peer (P2P) application.
 * It sets up the necessary resources and configurations required for P2P communication.
 * 
 * @return OPERATE_RET Returns the operation result status
 *         - OPRT_OK: Success
 *         - Error code: Failure (specific error codes depend on implementation)
 * 
 * @note This function should be called after the Tuya SDK is properly initialized
 * @note Ensure network connectivity is available before calling this function
 */
OPERATE_RET tuya_p2p_app_start(VOID);

INT_T tuya_ipc_app_audio_frame_put(VOID *data, INT_T len);
#ifdef __cplusplus
}
#endif


#endif