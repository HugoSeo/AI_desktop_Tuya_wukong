#ifndef __UI_SVC_DEVINFO_H__
#define __UI_SVC_DEVINFO_H__

/*
 * Device-info provider for the "About" page.
 *
 * Keeps the UI layer free of business headers: pages include only this header
 * and read device/version strings through these getters. Every getter returns
 * a non-NULL, NUL-terminated string (placeholder "--" when unavailable), so
 * callers never need a NULL check.
 */

const char *ui_svc_devinfo_fw_version(void);   /* firmware / app version */
const char *ui_svc_devinfo_sdk_info(void);      /* TuyaOS SDK info */
const char *ui_svc_devinfo_device_id(void);     /* device / gateway id */
const char *ui_svc_devinfo_uuid(void);          /* device authorization UUID */

#endif /* __UI_SVC_DEVINFO_H__ */
