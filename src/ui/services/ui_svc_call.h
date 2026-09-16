#ifndef __UI_SVC_CALL_H__
#define __UI_SVC_CALL_H__

#include <stdbool.h>
#include <stdint.h>

/*
 * Voice-call control service.
 *
 * This is the ONLY UI-layer file that touches the P2P (TMM VoIP) business APIs
 * and the EVENT_TOY_VOIP_UI event. Pages depend solely on this header so they
 * stay free of business/platform includes (see src/ui/RULES.md §6/§8).
 *
 * Handles outbound calls (device-to-app / device-to-device) and inbound calls.
 * Inbound calls are auto-answered by this service when the auto-answer setting
 * is ON; when OFF, the incoming call surfaces as UI_CALL_STATE_INCOMING so the
 * call page can show a ringing UI and let the user answer/reject.
 *
 * Threading: EVENT_TOY_VOIP_UI and the outbound-timeout fire on non-UI threads;
 * this service marshals every state change onto the UI thread via
 * ui_app_async_call before invoking the page callback, so the callback runs in
 * the UI thread and may touch LVGL directly.
 */

typedef enum {
    UI_CALL_STATE_IDLE = 0,
    UI_CALL_STATE_CALLING,         /* outbound dial issued, waiting for answer  */
    UI_CALL_STATE_INCOMING,        /* inbound call ringing, awaiting answer/reject */
    UI_CALL_STATE_IN_CALL,         /* media stream up, talking                  */
    UI_CALL_STATE_FAILED,          /* reject / busy / unanswered / error / timeout */
    UI_CALL_STATE_ENDED,           /* peer hung up (HANGUP) or call broke (STOP)   */
    UI_CALL_STATE_ENDED_BY_OTHERS, /* answered on another device (ACCEPTED_BY_OTHERS) */
} ui_call_state_t;

typedef void (*ui_svc_call_cb_t)(ui_call_state_t state);

/* Subscribe the call/stream events + START the TMM VoIP stack (idempotent).
 * The P2P enable switch is the stack's launch gate (docs/adr/0008): while OFF
 * this is a runtime no-op — zero threads, no RTC signaling, device unreachable
 * for incoming calls. No-op build when the P2P feature is compiled out. Called
 * from ui_services_init() and again from ui_svc_call_enabled_set(true). */
void ui_svc_call_init(void);

/* P2P enable switch (runtime gate for ui_svc_call_init). Persistence is owned by
 * ui_svc_dev_ctrl (KV); default OFF. set(true) starts P2P immediately (idempotent);
 * set(false) only clears the flag — there is no de-init (one-way until reboot). */
bool ui_svc_call_enabled_get(void);
void ui_svc_call_enabled_set(bool on);

/* Register/clear the state callback. Pages set it in on_create, clear (NULL)
 * in on_destroy. */
void ui_svc_call_set_cb(ui_svc_call_cb_t cb);

/* Initiate an outbound call to the bound mobile App (device-to-app) and arm the
 * 30s timeout. */
void ui_svc_call_dial(void);

/* A callable peer device (device-to-device). */
#define UI_CALL_CONTACT_MAX       16
#define UI_CALL_CONTACT_ID_LEN    40
#define UI_CALL_CONTACT_NAME_LEN  64
typedef struct {
    char id[UI_CALL_CONTACT_ID_LEN];
    char name[UI_CALL_CONTACT_NAME_LEN];
} ui_call_contact_t;

/* Async fetch of callable peer DEVICES (excludes App entries and self). The
 * underlying manager fetch may hit the cloud, so it runs off the UI thread on
 * WORKQ_SYSTEM; `cb` is invoked back on the UI thread with the collected list
 * (list/count borrowed — copy what you need, do not retain the pointer).
 *
 * Callback contract:
 *   count >= 0  fetch succeeded (0 = genuinely no callable device)
 *   count <  0  fetch failed (retryable; list is NULL)
 * cb registration is single-slot: pass NULL to unregister (page on_destroy;
 * in-flight results are then dropped). While a cb is registered, stream-ready
 * auto-refetches and re-invokes it. Requests while a fetch is already in
 * flight are coalesced into the pending one. */
typedef void (*ui_svc_call_contacts_cb_t)(const ui_call_contact_t *list, int count);
void ui_svc_call_contacts_async(ui_svc_call_contacts_cb_t cb);

#define UI_SVC_CALL_CONTACT_DELETE_OK           0   /* mirrors TUYA_TMM_OK */
#define UI_SVC_CALL_CONTACT_DELETE_FAILED      -1   /* mirrors TUYA_TMM_FALSE */

/* Async delete of a peer device contact. The cloud call runs on WORKQ_SYSTEM;
 * cb is invoked back on the UI thread with the delete result:
 *   UI_SVC_CALL_CONTACT_DELETE_OK       delete succeeded
 *   otherwise                           delete failed
 */
typedef void (*ui_svc_call_contact_delete_cb_t)(int result);
void ui_svc_call_contact_delete_async(const char *dev_id, ui_svc_call_contact_delete_cb_t cb);

#define UI_SVC_CALL_CONTACT_APPLY_OK           0
#define UI_SVC_CALL_CONTACT_APPLY_FAILED      -1

/* Async contact application by the peer UUID read from NFC. The cloud call is
 * isolated in this service and cb is delivered on the UI thread. */
typedef void (*ui_svc_call_contact_apply_cb_t)(int result);
void ui_svc_call_contact_apply_uuid_async(const char *uuid,
                                          ui_svc_call_contact_apply_cb_t cb);

/* Initiate an outbound call to a specific peer device id (device-to-device)
 * and arm the 30s timeout. */
void ui_svc_call_dial_device(const char *dev_id);

/* Answer a ringing inbound call (UI_CALL_STATE_INCOMING). */
void ui_svc_call_answer(void);

/* Reject a ringing inbound call (reply busy) and return to idle. */
void ui_svc_call_reject(void);

/* Hang up an active/dialing call and cancel the timeout. */
void ui_svc_call_hangup(void);

/* Current state (authoritative; survives page re-create). */
ui_call_state_t ui_svc_call_get_state(void);

/* Peer name for the current call (set by the caller before dial, or captured
 * from the incoming-call event). Returns "" when unset. The pointer is valid
 * until the next set_peer_name / dial / incoming call. */
void        ui_svc_call_set_peer_name(const char *name);
const char *ui_svc_call_get_peer_name(void);

/* Compile switch for the auto-answer user setting (settings-page switch row +
 * KV restore in ui_svc_dev_ctrl). The product default is manual answer, so
 * this is 0: no switch row is created, no persisted value is restored, and the
 * runtime value stays at its default (off). Set to 1 to bring back the switch
 * and its persistence in one place. */
#ifndef ENABLE_UI_CALL_AUTO_ANSWER
#define ENABLE_UI_CALL_AUTO_ANSWER 0
#endif

/* Auto-answer setting bridge — runtime value lives in the call layer
 * (tuya_sdk_call.c); persistence is owned by ui_svc_dev_ctrl. */
bool ui_svc_call_auto_answer_get(void);
void ui_svc_call_auto_answer_set(bool on);

#endif /* __UI_SVC_CALL_H__ */
