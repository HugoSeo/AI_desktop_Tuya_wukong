#include "ui_route.h"
#include <string.h>

#define UI_ROUTE_MAX_PAGES 40

static const ui_page_entry_t *s_pages[UI_ROUTE_MAX_PAGES];
static int s_page_count;
static ui_page_id_t s_stack[UI_ROUTE_STACK_DEPTH];
static uint8_t s_stack_top;
static ui_page_id_t s_root;
static ui_route_change_cb_t s_change_cb;
static ui_page_id_t s_last_change_page;   /* dedup: last id passed to s_change_cb */

static const ui_page_entry_t *find_page(ui_page_id_t id) {
    for (int i = 0; i < s_page_count; i++) {
        if (s_pages[i]->id == id) return s_pages[i];
    }
    return NULL;
}

static void create_page(ui_page_id_t id) {
    const ui_page_entry_t *p = find_page(id);
    if (p && p->lifecycle.on_create) p->lifecycle.on_create(NULL);
}

static void enter_page(ui_page_id_t id, uint32_t dirty) {
    const ui_page_entry_t *p = find_page(id);
    if (p && p->lifecycle.on_enter) p->lifecycle.on_enter(dirty);

    /* Foreground-change observer: fire only when the top actually changes, so
     * the per-tick refresh path (enter_page with the same id) doesn't spam it. */
    if (id != s_last_change_page) {
        s_last_change_page = id;
        if (s_change_cb) {
            s_change_cb(id);
        }
    }
}

static void leave_current(void) {
    const ui_page_entry_t *cur = find_page(ui_route_current());
    if (cur && cur->lifecycle.on_leave) cur->lifecycle.on_leave();
}

static void leave_destroy_current(void) {
    const ui_page_entry_t *cur = find_page(ui_route_current());
    if (cur) {
        if (cur->lifecycle.on_leave) cur->lifecycle.on_leave();
        if (cur->lifecycle.on_destroy) cur->lifecycle.on_destroy();
    }
}

/* Pop and destroy every page stacked above the root, leaving the root at the
 * bottom. The root itself is never left/destroyed and is not re-entered here;
 * callers decide when to fire its on_enter. */
static void unwind_to_root(void) {
    while (s_stack_top > 1) {
        leave_destroy_current();
        s_stack_top--;
    }
}

void ui_route_init(ui_page_id_t root) {
    s_page_count = 0;
    s_stack_top = 0;
    s_root = root;
    s_last_change_page = UI_PAGE_NONE;
    memset(s_stack, 0, sizeof(s_stack));
}

void ui_route_set_change_cb(ui_route_change_cb_t cb) {
    s_change_cb = cb;
}

void ui_route_register(const ui_page_entry_t *page) {
    if (!page || s_page_count >= UI_ROUTE_MAX_PAGES) return;
    s_pages[s_page_count++] = page;
}

ui_page_id_t ui_route_current(void) {
    if (s_stack_top == 0) return UI_PAGE_NONE;
    return s_stack[s_stack_top - 1];
}

ui_page_id_t ui_route_previous(void) {
    if (s_stack_top < 2) return UI_PAGE_NONE;
    return s_stack[s_stack_top - 2];
}

uint8_t ui_route_stack_depth(void) {
    return s_stack_top;
}

void ui_route_push(ui_page_id_t page) {
    /* Pushing the root from deeper in the stack means "return home": unwind
     * every page above the immovable root bottom. */
    if (page == s_root && s_stack_top > 1) {
        unwind_to_root();
        enter_page(ui_route_current(), UI_DIRTY_ENTER);
        return;
    }

    if (s_stack_top > 0 && s_stack[s_stack_top - 1] == page) {
        enter_page(page, UI_DIRTY_ENTER);
        return;
    }

    int existing_idx = -1;
    for (int i = 0; i < s_stack_top; i++) {
        if (s_stack[i] == page) { existing_idx = i; break; }
    }

    if (existing_idx < 0 && s_stack_top >= UI_ROUTE_STACK_DEPTH) return;

    if (s_stack_top > 0) {
        leave_current();
    }

    if (existing_idx >= 0) {
        for (int i = existing_idx; i < s_stack_top - 1; i++) {
            s_stack[i] = s_stack[i + 1];
        }
        s_stack[s_stack_top - 1] = page;
        enter_page(page, UI_DIRTY_ENTER);
    } else {
        s_stack[s_stack_top++] = page;
        create_page(page);
        enter_page(page, UI_DIRTY_ENTER);
    }
}

void ui_route_pop(void) {
    /* The root page is the immovable stack bottom: never pop it. */
    if (s_stack_top <= 1 || s_stack[s_stack_top - 1] == s_root) return;
    leave_destroy_current();
    s_stack_top--;
    enter_page(ui_route_current(), UI_DIRTY_ENTER);
}

void ui_route_replace(ui_page_id_t page) {
    /* Never replace the root bottom in place; grow the stack instead so the
     * root always survives. */
    if (s_stack_top <= 1) {
        ui_route_push(page);
        return;
    }
    leave_destroy_current();
    s_stack[s_stack_top - 1] = page;
    create_page(page);
    enter_page(page, UI_DIRTY_ENTER);
}

void ui_route_reset(ui_page_id_t page) {
    /* Clear the intermediate pages but keep the root at the bottom. */
    unwind_to_root();
    if (s_stack_top == 0) {
        /* Stack was never seeded; fall back to a plain push (no auto-seed). */
        ui_route_push(page);
        return;
    }
    if (page == ui_route_current()) {
        enter_page(page, UI_DIRTY_ENTER);
        return;
    }
    ui_route_push(page);
}

void ui_route_refresh_current(uint32_t dirty) {
    enter_page(ui_route_current(), dirty);
}
