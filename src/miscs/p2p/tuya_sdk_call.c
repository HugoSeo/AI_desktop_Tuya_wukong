#include <stdio.h>
#include <unistd.h>
#include "tuya_iot_config.h" 
#include "tuya_sdk_call.h"
#include "uni_log.h"
#include "base_event.h"   /* ty_publish_event */


/* 来电自动接通开关：仅内存态，持久化由 UI 侧负责（启动时通过 set 同步）。
 * 默认关闭：来电以响铃 UI 呈现，由用户手动接听（产品默认不自动接通）。
 * __call_handler 运行在 SDK workqueue 线程，直接读取该标志即可，无需访问 KV。 */
STATIC BOOL_T s_auto_answer = FALSE;

STATIC void __call_handler(void * data)
{
    /**
     * 收到的呼叫请求或回复处理(运行在 SDK workqueue 线程)。
     * - 把全部通话生命周期事件(TUYA_TMM_CONTROL_EVT_E)经 TUYA_IPC_CALL 上报,
     *   由 UI 服务(ui_svc_call)订阅并刷新界面;
     * - 来电(INCOMING)且自动接通开启时,直接 answer 建立 P2P 会话。
    */
    TUYA_TMM_CONTROL_EVT_E evt = (TUYA_TMM_CONTROL_EVT_E)(int)data;

    ty_publish_event(TUYA_IPC_CALL, &evt);

    if (evt == TUYA_TMM_CONTROL_EVT_INCOMING && s_auto_answer) {
        PR_DEBUG("incoming: auto-answer on -> answer\r\n");
        tuya_tmm_control_answer();
    }

    return;
}

OPERATE_RET TUYA_IPC_answer()
{
    return tuya_tmm_control_answer();
}

OPERATE_RET TUYA_IPC_reject()
{
    return tuya_tmm_control_busy();
}

VOID TUYA_IPC_call_auto_answer_set(BOOL_T on)
{
    s_auto_answer = on;
}

BOOL_T TUYA_IPC_call_auto_answer_get(VOID)
{
    return s_auto_answer;
}

OPERATE_RET tmm_control_evt_cb(TUYA_TMM_CONTROL_INFO_S* pinfo, VOID* priv_data)
{
    /**
     * 收到呼叫请求或者回复的处理函数
     * 此处需要使用异步处理，否则会阻塞SDK内部线程的正常运转。
     * 客户可以自己创建线程或者工作队列来处理此异步请求，也可以使用涂鸦SDK内部的workq处理
     * 若使用涂鸦内部workq处理，此部分代码无需修改
    */
    PR_DEBUG("receive evt %d\n", pinfo->event);
    WORKQUEUE_HANDLE ty_wq_hand = tal_workq_get_handle(WORKQ_SYSTEM);
    tal_workqueue_schedule(ty_wq_hand, __call_handler, (VOID *)(pinfo->event)); 
    return OPRT_OK;
}

OPERATE_RET TUYA_IPC_call_init()
{
    tuya_tmm_control_init(tmm_control_evt_cb, NULL, 30);
}

OPERATE_RET TUYA_IPC_call_app()
{ 
    /**
     * key_1对应于app呼叫页面上配置的key_1 or key_2
     * sp_dpsxj为固定值，为双向视频呼叫ipc特有参数，请勿修改。
     * 
    */
    return tuya_tmm_control_call("key_1","sp_dpsxj", "dgnzk", TUYA_TMM_CONTROL_STREAM_TYPE_AUDIO);
}

OPERATE_RET TUYA_IPC_hangup()
{
    tuya_ipc_p2p_stream_close();
    return tuya_tmm_control_hangup();
}
