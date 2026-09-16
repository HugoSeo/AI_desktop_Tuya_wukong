#ifndef __UI_ROUTE_H__
#define __UI_ROUTE_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UI_ROUTE_STACK_DEPTH  8
#define UI_PAGE_NONE          0

/* on_enter(dirty) sentinel: the route passes this on a real page entry
 * (create/push/pop/replace/reset). Per-tick refreshes pass a partial state-group
 * bitmap (max (1<<UI_STATE_GROUP_MAX)-1), so this value never collides with a
 * real dirty set. Pages do one-time entry work under ui_dirty_is_enter(dirty)
 * and reactive work under `dirty & (1u<<GROUP)` — never unconditionally (it would
 * run every ~1s on the SYSTEM tick). See src/ui/RULES.md §5. */
#define UI_DIRTY_ENTER  0xFFFFFFFFu

static inline bool ui_dirty_is_enter(uint32_t dirty) {
    return dirty == UI_DIRTY_ENTER;
}

typedef uint8_t ui_page_id_t;

typedef struct {
    void (*on_create)(void *parent);
    void (*on_enter)(uint32_t dirty);
    void (*on_leave)(void);
    void (*on_destroy)(void);
} ui_page_lifecycle_t;

typedef struct {
    ui_page_id_t         id;
    const char          *name;
    ui_page_lifecycle_t  lifecycle;
} ui_page_entry_t;

/* Foreground-page change observer. Invoked with the new current page id after
 * any transition that actually changes the foreground (deduped — not fired on
 * per-tick refresh). Stays business-free: receives a plain id, no LVGL. */
typedef void (*ui_route_change_cb_t)(ui_page_id_t cur);

void         ui_route_init(ui_page_id_t root);
void         ui_route_set_change_cb(ui_route_change_cb_t cb);
void         ui_route_register(const ui_page_entry_t *page);
void         ui_route_push(ui_page_id_t page);
void         ui_route_pop(void);
void         ui_route_replace(ui_page_id_t page);
void         ui_route_reset(ui_page_id_t page);
ui_page_id_t ui_route_current(void);
ui_page_id_t ui_route_previous(void);
uint8_t      ui_route_stack_depth(void);
void         ui_route_refresh_current(uint32_t dirty);

#endif /* __UI_ROUTE_H__ */
