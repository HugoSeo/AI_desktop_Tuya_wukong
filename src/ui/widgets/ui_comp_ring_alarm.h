#ifndef __UI_COMP_RING_ALARM_H__
#define __UI_COMP_RING_ALARM_H__

typedef enum {
    UI_RING_KIND_ALARM = 0,
    UI_RING_KIND_REMINDER,
    UI_RING_KIND_COUNTDOWN,
} ui_ring_kind_t;

typedef void (*ui_ring_stop_cb_t)(ui_ring_kind_t kind, const char *id);

/* Show the global ringing overlay (UI thread only). id may be NULL/empty.
   For ALARM: shows Snooze + Stop; REMINDER: Stop; COUNTDOWN: Confirm.
   on_stop is invoked (if non-NULL) when the user taps the primary action,
   BEFORE the overlay dismisses. The caller wires it to the appropriate action
   (e.g. acknowledging an alarm). id may be NULL/empty. */
void ui_comp_ring_alarm_show(ui_ring_kind_t kind, const char *id,
                             const char *message, ui_ring_stop_cb_t on_stop);
void ui_comp_ring_alarm_dismiss(void);

#endif /* __UI_COMP_RING_ALARM_H__ */
