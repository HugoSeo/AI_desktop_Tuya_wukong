#include "ui_svc_call.h"
#include "tuya_app_config.h" /* ENABLE_AI_MODE_P2P */
#include "ui_app.h"          /* ui_app_async_call — marshal to UI thread */
#include "ui_page_ids.h"     /* UI_PAGE_CALL */
#include "ui_route.h" /* ui_route_push/current — foreground call page */
#include "ui_svc_camera.h" /* ui_svc_camera_available — camera readiness */
#include "ui_comp_popup.h" /* ui_comp_popup_toast */
#include "ui_i18n.h"       /* ui_i18n_text */
#include <stdint.h>

#if defined(ENABLE_AI_MODE_P2P) && (ENABLE_AI_MODE_P2P == 1)

#include "base_event.h" /* ty_subscribe_event */
#include "gw_intf.h"    /* get_gw_cntl — 本机设备 id(自身过滤) */
#include "tal_memory.h" /* tal_malloc / tal_free */
#include "tal_workq_service.h" /* WORKQ_SYSTEM, tal_workq_schedule / delayed work */
#include "tuya_p2p_tmm_voip.h" /* TMM VoIP 适配层:call/answer/hangup + EVENT_TOY_VOIP_UI */
#include "tuya_tmm_manager.h"      /* call manager */
#include "tuya_tmm_manager_atop.h" /* contact sync/list ATOP APIs */
#include "tuya_tmm_stream.h"       /* EVENT_TMM_STREAM_READY */
#include "uni_log.h"
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* atoi */
#include <string.h>

#define CALL_EVT_SUBSCRIBER "ui_call"
#define CALL_STREAM_EVT_SUBSCRIBER "ui_call_stream"
#define CALL_OUTBOUND_TIMEOUT_MS 30000
#define CALL_TARGET_APP "key_1" /* 呼叫绑定 APP 的虚拟 id(对齐原生 call_app) \
                                 */

static ui_svc_call_cb_t s_cb = NULL;
static ui_call_state_t s_state = UI_CALL_STATE_IDLE;
static DELAYED_WORK_HANDLE s_timeout_work = NULL;
static bool s_inited = false;
static bool s_enabled = false; /* P2P enable switch; KV-backed via dev_ctrl */
static bool s_auto_answer = false; /* 来电自动接听设置(UI 层持有) */
static char s_peer_name[UI_CALL_CONTACT_NAME_LEN] = {0};

/* ---- UI-thread side: apply state + notify page -------------------------- */
static void notify_ui_cb(void *p) {
  ui_call_state_t st = (ui_call_state_t)(uintptr_t)p;
  s_state = st;

  /* 来电振铃 / 刚接通时把呼叫页推到前台,即使当前没有页面注册回调,
   * 用户也能看到呼叫 UI。页面 on_create 会读 ui_svc_call_get_state()。 */
  if ((st == UI_CALL_STATE_INCOMING || st == UI_CALL_STATE_IN_CALL) &&
      ui_route_current() != UI_PAGE_CALL) {
    ui_route_push(UI_PAGE_CALL);
  }

  if (s_cb) {
    s_cb(st);
  }
}

/* UI 线程:摄像头不可用提示。voip_ui_cb 跑在 manager 线程,经 ui_app_async_call 触发。 */
static void camera_unavail_toast_cb(void *p) {
  (void)p;
  ui_comp_popup_toast(ui_i18n_text(UI_TEXT_CAMERA_UNAVAILABLE), 2500);
}

/* Callable from any thread; defers to the UI thread. */
static void post_state(ui_call_state_t st) {
  ui_app_async_call(notify_ui_cb, (void *)(uintptr_t)st);
}

/* ---- outbound timeout (workq thread) ------------------------------------ */
static void timeout_work_cb(void *data) {
  (void)data;
  PR_WARN("call: outbound timeout");
  tuya_toy_tmm_voip_hangup();
  post_state(UI_CALL_STATE_FAILED);
}

/* ---- EVENT_TOY_VOIP_UI (adapter/manager thread) — call lifecycle -------- */
static INT_T voip_ui_cb(VOID_T *data) {
  if (data == NULL) {
    return OPRT_COM_ERROR;
  }

  TUYA_TOY_VOIP_UI_EVT_T *evt = (TUYA_TOY_VOIP_UI_EVT_T *)data;

  /* 捕获对端名称(来电时携带主叫方昵称;呼出时也可能回填)。 */
  if (evt->peer_name[0] != '\0') {
    strncpy(s_peer_name, evt->peer_name, sizeof(s_peer_name) - 1);
    s_peer_name[sizeof(s_peer_name) - 1] = '\0';
  }

  switch (evt->status) {
  case TUYA_TOY_TMM_VOIP_STATUS_CALLING:
    post_state(UI_CALL_STATE_CALLING);
    break;

  case TUYA_TOY_TMM_VOIP_STATUS_INCOMING:
    /* 摄像头不可用:直接拒接,不接听/不进 P2P 模式/不初始化视频流,
     * 从源头避免 SDK ring buffer 空转刷屏(根因见 plan Context)。 */
    if (!ui_svc_camera_available()) {
      PR_WARN("call: reject incoming, camera unavailable");
      tuya_toy_tmm_voip_hangup(); /* INCOMING 态 hangup 即拒接,给主叫回挂断 */
      ui_app_async_call(camera_unavail_toast_cb, NULL);
      break; /* 不 post INCOMING,保持 IDLE,通话页不闪现 */
    }
    post_state(UI_CALL_STATE_INCOMING);
    if (s_auto_answer) {
      tuya_toy_tmm_voip_answer();
    }
    break;

  case TUYA_TOY_TMM_VOIP_STATUS_INCALL:
    tal_workq_stop_delayed(s_timeout_work);
    post_state(UI_CALL_STATE_IN_CALL);
    break;

  case TUYA_TOY_TMM_VOIP_STATUS_IDLE:
  default:
    tal_workq_stop_delayed(s_timeout_work);
    /* 适配层把 拒接/挂断/未接/错误 统一收敛为 IDLE;若此前处于活动态,
     * 呈现为 ENDED(页面据此显示"已结束"后回到空闲),否则维持 IDLE。 */
    if (s_state == UI_CALL_STATE_CALLING || s_state == UI_CALL_STATE_INCOMING ||
        s_state == UI_CALL_STATE_IN_CALL) {
      post_state(UI_CALL_STATE_ENDED);
    } else {
      post_state(UI_CALL_STATE_IDLE);
    }
    break;
  }
  return OPRT_OK;
}

/* ---- public API --------------------------------------------------------- */
static INT_T contacts_stream_ready_evt_cb(VOID_T *data);

void ui_svc_call_init(void) {
  if (!s_enabled) {
    /* P2P 开关 OFF——TMM 栈不启动:零线程/零信令,设备对外不可达
     * (APP 呼叫无响应,docs/adr/0008)。开关打开后由 enabled_set 重跑此函数。 */
    return;
  }
  if (s_inited) {
    return;
  }

  tal_workq_init_delayed(WORKQ_SYSTEM, timeout_work_cb, NULL, &s_timeout_work);

  if (ty_subscribe_event(EVENT_TOY_VOIP_UI, CALL_EVT_SUBSCRIBER, voip_ui_cb,
                         SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
    PR_ERR("call: subscribe EVENT_TOY_VOIP_UI failed");
  }

  /* 链路就绪时补拉联系人:新通讯录接口不依赖 stream-ready,但保留该事件
   * 作为 P2P 栈启动后的自愈刷新触发。 */
  if (ty_subscribe_event(EVENT_TMM_STREAM_READY, CALL_STREAM_EVT_SUBSCRIBER,
                         contacts_stream_ready_evt_cb,
                         SUBSCRIBE_TYPE_NORMAL) != OPRT_OK) {
    PR_ERR("call: subscribe EVENT_TMM_STREAM_READY failed");
  }

  /* 按需启动 TMM VoIP 栈(线程/信令在此刻才产生,启动链路与旧方案一致,
   * docs/adr/0008)。放在订阅之后,避免漏掉启动线程随即发布的 stream-ready。
   * 后开开关时 MQTT 早已连接的时序由 tuya_tmm_stream_init 内部补偿。 */
  tuya_toy_tmm_voip_start();

  s_inited = true;
}

bool ui_svc_call_enabled_get(void) { return s_enabled; }

void ui_svc_call_enabled_set(bool on) {
  s_enabled = on;
  if (on) {
    ui_svc_call_init();
  }
}

void ui_svc_call_set_cb(ui_svc_call_cb_t cb) { s_cb = cb; }

void ui_svc_call_dial(void) {
  s_state = UI_CALL_STATE_CALLING;
  /* 呼叫绑定 APP(设备→APP)。设备↔设备呼叫需先从 get_dev_rtc_list 选目标,
   * 传具体设备 id 给 tuya_toy_tmm_voip_call —— 见联系人列表(后续)。 */
  tuya_toy_tmm_voip_call(CALL_TARGET_APP);
  tal_workq_start_delayed(s_timeout_work, CALL_OUTBOUND_TIMEOUT_MS, LOOP_ONCE);
}

/* ---- device-to-device contacts (async) --------------------------------- */
typedef struct {
  int count;
  ui_call_contact_t items[UI_CALL_CONTACT_MAX];
} contacts_result_t;

typedef struct {
  char dev_id[UI_CALL_CONTACT_ID_LEN];
  int result;
  ui_svc_call_contact_delete_cb_t cb;
} contact_delete_work_t;

typedef struct {
  char uuid[33];
  int result;
  ui_svc_call_contact_apply_cb_t cb;
} contact_apply_work_t;

static ui_svc_call_contacts_cb_t s_contacts_cb = NULL;
static bool s_contacts_fetching = false; /* in-flight 防抖;仅 UI 线程读写 */

/* contact.list 回调(WORKQ 线程):type "0"=设备、"1"=App;只收设备,排除自身。 */
static void contact_collect_cb(CHAR_T *id, CHAR_T *name, CHAR_T *type,
                               VOID *usrdata) {
  contacts_result_t *r = (contacts_result_t *)usrdata;
  GW_CNTL_S *gw = get_gw_cntl();
  const CHAR_T *self_id = (gw != NULL) ? gw->gw_if.id : NULL;

  if (r == NULL || id == NULL || id[0] == '\0' ||
      r->count >= UI_CALL_CONTACT_MAX) {
    return;
  }
  if (type != NULL && type[0] != '\0' && atoi(type) != 0) {
    return; /* 跳过 App(及非设备)条目 */
  }
  if (self_id != NULL && self_id[0] != '\0' && strcmp(id, self_id) == 0) {
    return; /* 排除自身设备 */
  }
  snprintf(r->items[r->count].id, sizeof(r->items[r->count].id), "%s", id);
  snprintf(r->items[r->count].name, sizeof(r->items[r->count].name), "%s",
           (name != NULL && name[0] != '\0') ? name : id);
  r->count++;
}

/* UI 线程:把结果交给页面回调,随后释放。r==NULL 表示拉取失败(count<0)。 */
static void contacts_deliver_ui_cb(void *p) {
  contacts_result_t *r = (contacts_result_t *)p;
  s_contacts_fetching = false;
  if (s_contacts_cb) {
    s_contacts_cb(r ? r->items : NULL, r ? r->count : -1);
  }
  if (r) {
    tal_free(r);
  }
}

/* WORKQ_SYSTEM:在 UI 线程外拉取设备 RTC 列表(可能触云端)。 */
static void contacts_fetch_work_cb(void *arg) {
  (void)arg;

  contacts_result_t *r =
      (contacts_result_t *)tal_malloc(sizeof(contacts_result_t));
  if (r == NULL) {
    ui_app_async_call(contacts_deliver_ui_cb, NULL);
    return;
  }
  memset(r, 0, sizeof(*r));
  /* Legacy path kept in manager but no longer used here:
   * tuya_tmm_manager_get_dev_rtc_list(contact_collect_cb, r); */
  if (tuya_tmm_manager_atop_contact_sync_list(contact_collect_cb, r) !=
      OPRT_OK) {
    PR_ERR("call: contact sync/list failed");
    tal_free(r);
    ui_app_async_call(contacts_deliver_ui_cb, NULL);
    return;
  }
  ui_app_async_call(contacts_deliver_ui_cb, r);
}

/* UI 线程:发起一次拉取(有注册回调且无在途拉取时)。 */
static void contacts_fetch_request(void) {
  if (s_contacts_cb == NULL || s_contacts_fetching) {
    return;
  }
  s_contacts_fetching = true;
  if (tal_workq_schedule(WORKQ_SYSTEM, contacts_fetch_work_cb, NULL) !=
      OPRT_OK) {
    PR_ERR("call: schedule contacts fetch failed");
    s_contacts_fetching = false;
    s_contacts_cb(NULL, -1);
  }
}

/* UI 线程:stream-ready 封送落点。 */
static void contacts_stream_ready_ui_cb(void *p) {
  (void)p;
  contacts_fetch_request();
}

/* EVENT_TMM_STREAM_READY(tmm 业务线程):决策一律封送到 UI 线程做,
 * in-flight 标志因此无需加锁。 */
static INT_T contacts_stream_ready_evt_cb(VOID_T *data) {
  (void)data;
  ui_app_async_call(contacts_stream_ready_ui_cb, NULL);
  return OPRT_OK;
}

void ui_svc_call_contacts_async(ui_svc_call_contacts_cb_t cb) {
  s_contacts_cb = cb;
  if (cb == NULL) {
    return; /* 注销(页面 on_destroy);在途结果送达时判空丢弃 */
  }
  contacts_fetch_request();
}

/* UI 线程:交付删除结果并释放工作项。 */
static void contact_delete_deliver_ui_cb(void *p) {
  contact_delete_work_t *work = (contact_delete_work_t *)p;
  if (work == NULL) {
    return;
  }
  if (work->cb != NULL) {
    work->cb(work->result);
  }
  tal_free(work);
}

/* WORKQ_SYSTEM:删除联系人可能触云端,不能阻塞 UI 线程。 */
static void contact_delete_work_cb(void *arg) {
  contact_delete_work_t *work = (contact_delete_work_t *)arg;
  if (work == NULL) {
    return;
  }

  work->result = tuya_tmm_manager_atop_contact_delete(0, work->dev_id);
  ui_app_async_call(contact_delete_deliver_ui_cb, work);
}

void ui_svc_call_contact_delete_async(const char *dev_id,
                                      ui_svc_call_contact_delete_cb_t cb) {
  if (dev_id == NULL || dev_id[0] == '\0') {
    if (cb != NULL) {
      cb(UI_SVC_CALL_CONTACT_DELETE_FAILED);
    }
    return;
  }

  contact_delete_work_t *work =
      (contact_delete_work_t *)tal_malloc(sizeof(*work));
  if (work == NULL) {
    if (cb != NULL) {
      cb(UI_SVC_CALL_CONTACT_DELETE_FAILED);
    }
    return;
  }

  memset(work, 0, sizeof(*work));
  snprintf(work->dev_id, sizeof(work->dev_id), "%s", dev_id);
  work->result = UI_SVC_CALL_CONTACT_DELETE_FAILED;
  work->cb = cb;
  if (tal_workq_schedule(WORKQ_SYSTEM, contact_delete_work_cb, work) !=
      OPRT_OK) {
    PR_ERR("call: schedule contact delete failed");
    tal_free(work);
    if (cb != NULL) {
      cb(UI_SVC_CALL_CONTACT_DELETE_FAILED);
    }
  }
}

static void contact_apply_deliver_ui_cb(void *p) {
  contact_apply_work_t *work = (contact_apply_work_t *)p;
  if (work == NULL) {
    return;
  }
  if (work->cb != NULL) {
    work->cb(work->result);
  }
  tal_free(work);
}

static void contact_apply_work_cb(void *arg) {
  contact_apply_work_t *work = (contact_apply_work_t *)arg;
  if (work == NULL) {
    return;
  }
  work->result = tuya_tmm_manager_atop_contact_apply_uuid(work->uuid);
  PR_INFO("call: contact apply result=%d", work->result);
  ui_app_async_call(contact_apply_deliver_ui_cb, work);
}

void ui_svc_call_contact_apply_uuid_async(const char *uuid,
                                          ui_svc_call_contact_apply_cb_t cb) {
  if (uuid == NULL || uuid[0] == '\0') {
    if (cb != NULL) {
      cb(UI_SVC_CALL_CONTACT_APPLY_FAILED);
    }
    return;
  }

  contact_apply_work_t *work =
      (contact_apply_work_t *)tal_malloc(sizeof(*work));
  if (work == NULL) {
    if (cb != NULL) {
      cb(UI_SVC_CALL_CONTACT_APPLY_FAILED);
    }
    return;
  }
  memset(work, 0, sizeof(*work));
  snprintf(work->uuid, sizeof(work->uuid), "%s", uuid);
  work->result = UI_SVC_CALL_CONTACT_APPLY_FAILED;
  work->cb = cb;
  if (tal_workq_schedule(WORKQ_SYSTEM, contact_apply_work_cb, work) != OPRT_OK) {
    PR_ERR("call: schedule contact apply failed");
    tal_free(work);
    if (cb != NULL) {
      cb(UI_SVC_CALL_CONTACT_APPLY_FAILED);
    }
  }
}

void ui_svc_call_dial_device(const char *dev_id) {
  if (dev_id == NULL || dev_id[0] == '\0') {
    return;
  }
  s_state = UI_CALL_STATE_CALLING;
  tuya_toy_tmm_voip_call((CHAR_T *)dev_id);
  tal_workq_start_delayed(s_timeout_work, CALL_OUTBOUND_TIMEOUT_MS, LOOP_ONCE);
}

void ui_svc_call_answer(void) {
  /* 防御:摄像头不可用时拒绝手动接听(正常路径下此时不会推通话页,此为兜底)。 */
  if (!ui_svc_camera_available()) {
    PR_WARN("call: deny manual answer, camera unavailable");
    ui_svc_call_reject();
    return;
  }
  /* 接听;媒体接通后经 EVENT_TOY_VOIP_UI 的 INCALL 驱动到 IN_CALL。 */
  tuya_toy_tmm_voip_answer();
}

void ui_svc_call_reject(void) {
  s_state = UI_CALL_STATE_IDLE;
  tuya_toy_tmm_voip_hangup(); /* INCOMING 态下的挂断即拒接 */
}

void ui_svc_call_hangup(void) {
  tal_workq_stop_delayed(s_timeout_work);
  s_state = UI_CALL_STATE_IDLE;
  tuya_toy_tmm_voip_hangup();
}

ui_call_state_t ui_svc_call_get_state(void) { return s_state; }

void ui_svc_call_set_peer_name(const char *name) {
  if (name != NULL && name[0] != '\0') {
    strncpy(s_peer_name, name, sizeof(s_peer_name) - 1);
    s_peer_name[sizeof(s_peer_name) - 1] = '\0';
  } else {
    s_peer_name[0] = '\0';
  }
}

const char *ui_svc_call_get_peer_name(void) { return s_peer_name; }

bool ui_svc_call_auto_answer_get(void) { return s_auto_answer; }

void ui_svc_call_auto_answer_set(bool on) { s_auto_answer = on; }

#else /* P2P feature compiled out — provide inert stubs */

void ui_svc_call_init(void) {}
bool ui_svc_call_enabled_get(void) { return false; }
void ui_svc_call_enabled_set(bool on) { (void)on; }
void ui_svc_call_set_cb(ui_svc_call_cb_t cb) { (void)cb; }
void ui_svc_call_dial(void) {}
void ui_svc_call_contacts_async(ui_svc_call_contacts_cb_t cb) { (void)cb; }
void ui_svc_call_contact_delete_async(const char *dev_id,
                                      ui_svc_call_contact_delete_cb_t cb) {
  (void)dev_id;
  if (cb)
    cb(UI_SVC_CALL_CONTACT_DELETE_FAILED);
}
void ui_svc_call_contact_apply_uuid_async(const char *uuid,
                                          ui_svc_call_contact_apply_cb_t cb) {
  (void)uuid;
  if (cb)
    cb(UI_SVC_CALL_CONTACT_APPLY_FAILED);
}
void ui_svc_call_dial_device(const char *dev_id) { (void)dev_id; }
void ui_svc_call_answer(void) {}
void ui_svc_call_reject(void) {}
void ui_svc_call_hangup(void) {}
ui_call_state_t ui_svc_call_get_state(void) { return UI_CALL_STATE_IDLE; }
void ui_svc_call_set_peer_name(const char *n) { (void)n; }
const char *ui_svc_call_get_peer_name(void) { return ""; }
bool ui_svc_call_auto_answer_get(void) { return false; }
void ui_svc_call_auto_answer_set(bool on) { (void)on; }

#endif /* ENABLE_AI_MODE_P2P */
