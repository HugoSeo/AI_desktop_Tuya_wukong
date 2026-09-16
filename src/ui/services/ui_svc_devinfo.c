#include "ui_svc_devinfo.h"
#include "sdk_version.h"
#include "tuya_devos_utils.h"

/* get_gw_uuid() is provided by DevOS but is not declared in the public
 * tuya_devos_utils.h header in this SDK revision. */
extern const char *get_gw_uuid(void);

/*
 * Device-info provider for the "About" page. Sources version/device strings
 * from the SDK; every getter returns a non-NULL, NUL-terminated string,
 * falling back to the placeholder when the underlying value is unavailable.
 */

const char *ui_svc_devinfo_fw_version(void)
{
    return get_gw_sw_ver(DEV_NM_ATH_SNGL);
}

const char *ui_svc_devinfo_sdk_info(void)
{
    return SDK_ID;
}

const char *ui_svc_devinfo_device_id(void)
{
    return get_gw_dev_id();
}

const char *ui_svc_devinfo_uuid(void)
{
    const char *uuid = get_gw_uuid();
    return (uuid != NULL && uuid[0] != '\0') ? uuid : "--";
}
