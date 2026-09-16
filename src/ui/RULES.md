# UI Framework Rules

This file defines the design principles and coding conventions for the Vue-inspired LVGL UI framework.
All AI coding tools (Claude Code, Cursor, Copilot, etc.) and human contributors MUST follow these rules
when working on files under `src/ui/`.

---

## Architecture Principles

### 1. Single Direction Data Flow

```
Business Thread → ui_state_set_*() (标脏, 无锁)
    → ui_tick (30ms 消费 dirty bitmap)
    → ui_route_refresh_current(dirty)
    → page.on_enter(dirty)
    → lv_obj_*
```

- Data flows **downward only**: State → Tick → Page → Widget
- User interactions flow **upward** via callbacks: Widget event → Page handler → `ui_svc_*()`
  (the services layer calls the business API directly — §8; long-running work goes to a workqueue
  or the service's async path).
- **Never** read LVGL widget state to derive application state; `ui_state` is the single source of truth

### 2. Component-Based Design

Each UI element is an independent component with:

- **Props**: read-only input from parent (passed at creation, updated by parent)
- **Lifecycle**: `create` → `update` → `destroy`
- **Render**: a pure function of props → LVGL widget tree

Rules:
- One component per `.c` file, named `ui_comp_<name>.c`
- Components MUST NOT access global state directly; pass data through props
- Components MUST NOT call business layer APIs; use action callbacks
- Components own their LVGL objects; LVGL's parent-child deletion handles cleanup automatically
- If a component holds resources beyond LVGL objects (dynamic memory, subscriptions, handles),
  it MUST provide an explicit `destroy` function
- Pure LVGL wrappers (thin wrappers around `lv_*_create`) may omit `destroy` — parent deletion suffices
- Singleton components (e.g., statusbar) may use file-scope statics for their single instance

### 3. Page as Self-Contained Unit

Pages are special components registered via `ui_page_entry_t` lifecycle struct:

```c
const ui_page_entry_t ui_page_xxx_entry = {
    .id = UI_PAGE_XXX,
    .name = "xxx",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};
```

Lifecycle callbacks:
- `on_create(void *parent)` — Build LVGL widget tree
- `on_enter(uint32_t dirty)` — Refresh based on dirty state-group bitmap
- `on_leave()` — Page is leaving foreground (pause, persist settings if needed)
- `on_destroy()` — Hide + async-delete LVGL root, then NULL all static pointers

**Page destruction pattern (MANDATORY):**

```c
static void on_destroy(void)
{
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
    }
    // NULL all static pointers...
}
```

Rules for page destruction:
- `on_destroy` is called from within the page's own event callback chain (e.g., gesture → `ui_route_pop` → `on_destroy`). **Never** use `lv_obj_del()` here — it deletes the object while LVGL is still processing events on it, causing silent failures.
- Use `lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN)` for immediate visual removal, then `lv_obj_del_async(s_screen)` to safely defer actual deletion to the next timer tick.

Rules:
- Pages register via `ui_route_register(&entry)` in `ui_app_init()`
- Page transitions are handled by the router (`ui_route_push/pop/replace/reset`), never by the page itself
- Pages MUST NOT include or depend on other pages (exception: overlay pages may query `_is_visible()`)
- **Pages are self-contained**: all page-specific UI logic (layout, rendering, state) lives directly in the
  page `.c` file. Do NOT split page-internal pieces into separate widget files.
- Only extract to `widgets/` when a widget is **genuinely reusable across multiple pages**
- **Offset full-screen content below the global statusbar.** The statusbar is on `lv_layer_top()` at `(0,0)`
  height `ui_adapt(32)`. Every regular page MUST reserve that band:
  ```c
  lv_obj_set_style_pad_top(s_screen, ui_adapt(32), 0);
  ```
  (Overlay pages on `lv_layer_top()` are exempt.)

### 3.1 Swipe-Back Navigation (MANDATORY for regular pages)

**Every regular (non-root, non-overlay) page MUST support swipe-right to go back.** This is the
default navigation gesture; do not ship a page without it. (The root page `UI_PAGE_HOME` and overlay
pages are exempt — home has nowhere to go back to, overlays dismiss via their own gesture.)

**Exception — immersive media pages:** a page whose primary interaction IS horizontal swiping may
repurpose right-swipe (e.g. the photo viewer uses left/right swipe for prev/next picture), but then
it MUST provide an always-reachable explicit back button that calls `ui_route_pop()`. Current
exceptions: `UI_PAGE_PHOTO` (viewer mode only — its grid mode still uses right-swipe = back to viewer).

Implement it per-page (pages are self-contained; there is no global handler — LVGL dispatches gestures
to the pressed object, so a single screen-level handler can't reliably cover every page):

```c
static void gesture_cb(lv_event_t *e)
{
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());  // PITFALL guard — see below
        ui_route_pop();
    }
}

// in on_create, on the page root:
lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);
```

Requirements & rationale:
- **Direction is right-swipe = back** (`LV_DIR_RIGHT`). Matches the existing chat/home/photo convention.
  Left-swipe is reserved for "go deeper/forward" where a page defines it (e.g. home → chat).
- **Clear `LV_OBJ_FLAG_GESTURE_BUBBLE` (together with `SCROLLABLE`) on the page root `s_screen`.**
  Attaching `gesture_cb` to `s_screen` is not enough on its own — pages whose root only cleared
  `SCROLLABLE` were observed to never fire the gesture, while every working page also clears
  `GESTURE_BUBBLE`. Clear both.
- **No home/root guard needed in the callback.** `ui_route_pop()` already refuses to pop the root
  (`UI_PAGE_HOME`) or an empty stack, so a stray right-swipe on home is a safe no-op.

**PITFALL — mis-triggering the page below (MANDATORY guard):**
`gesture_cb` runs synchronously while LVGL is still processing the touch. Calling `ui_route_pop()`
tears down the current page immediately, revealing the page underneath. When the finger lifts, that
release is hit-tested against the newly-revealed page and fires a **click on it** — e.g. swiping back
from About lands on the Settings "About" row and re-opens About. **Always call
`lv_indev_wait_release(lv_indev_get_act())` before `ui_route_pop()`**: it tells the input device to
swallow the rest of this touch (no further press/click is emitted until release), so the release never
leaks to the page below. (Same class of bug the pulldown overlay guards against by deferring its
dismiss.)

### 3.2 Page Title Bar (standard layout)

Most regular pages have a title bar at the very top: an icon-only back button on the left and a
centered title. Use the same structure everywhere so pages look consistent.

**The title bar MUST sit BELOW the global statusbar, or the statusbar pill covers it** (and the back
button becomes untappable). The statusbar lives on `lv_layer_top()` at `(0,0)`, height `ui_adapt(32)`.
The page root reserves that band with `pad_top` (see §3, also MANDATORY); the title bar is then the
first child inside that padding.

Conventions:
- **Statusbar band: `ui_adapt(32)`** reserved via `lv_obj_set_style_pad_top(s_screen, ui_adapt(32), 0)`.
- **Title bar height: `ui_adapt(48)`**, full width, transparent background, non-scrollable.
- **Back button** (`icon_back_24_24` via `ui_comp_btn_icon_create`) left-aligned, `ui_adapt(8)` inset,
  its click handler calls `ui_route_pop()`.
- **Title label** centered, color `UI_COLOR_TEXT`, text from `ui_i18n_text(...)` (re-resolve on
  `on_enter(SETTINGS)` for language changes).

```c
lv_obj_set_style_pad_top(s_screen, ui_adapt(32), 0);   // reserve statusbar band (or it covers the title bar)

lv_obj_t *titlebar = lv_obj_create(s_screen);
lv_obj_remove_style_all(titlebar);
lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(48));
lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);

lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);

lv_obj_t *title = lv_label_create(titlebar);
lv_label_set_text(title, ui_i18n_text(UI_TEXT_XXX_TITLE));
lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);
```

The back button and swipe-right (§3.1) are redundant on purpose — both call `ui_route_pop()`, so the
page is reachable-back by either gesture or tap.

### 4. Overlay Pages

Overlay pages (e.g., pulldown panel) differ from regular pages:
- Created on `lv_layer_top()` instead of `lv_scr_act()`
- Do NOT replace the underlying screen content
- MUST provide `_is_visible()` query function
- Dismiss via `ui_route_pop()`
- Register a gesture detection zone on `lv_layer_top()` in their `_init()` function

### 5. Dirty-Flag Refresh (No Polling)

There are three state groups, each with a corresponding dirty bit:

| Group | `ui_state_group_t` | Contains |
|---|---|---|
| System | `UI_STATE_GROUP_SYSTEM` | battery, wifi, volume, charging, online, timestamp, calendar/alarm count |
| Settings | `UI_STATE_GROUP_SETTINGS` | language, theme_mode, brightness |
| Chat | `UI_STATE_GROUP_CHAT` | chat_status, device_mode, chat_sub_mode |

- `ui_state_set_*()` marks the corresponding group dirty (only when value actually changes)
- `ui_tick` (30ms LVGL timer) calls `ui_state_consume_dirty()`, dispatches dirty bitmap to statusbar and current page
- Pages receive `dirty` in `on_enter()` and only refresh affected elements
- **Never** use `lv_timer_create` for UI refresh; use the tick → dirty mechanism

**Per-second SYSTEM tick:** `ui_tick` polls the local clock ~1s and marks SYSTEM dirty on every
**second** change (second-granular by design — it drives the call-duration clock and the music
progress bar). Consequence: the current page's `on_enter(SYSTEM)` fires once per second, so any
SYSTEM-driven refresh MUST dedup — cache the last rendered value and skip the LVGL write when the
content hasn't changed (LVGL 8 does NOT dedup for you, see §9). Pages that don't render per-second
content simply ignore the SYSTEM bit.

**Language change refresh:** Any page that displays i18n text MUST handle `UI_STATE_GROUP_SETTINGS` dirty in `on_enter()`. For pages with dedup guards (e.g., date strings keyed on day/month), MUST invalidate the guard on SETTINGS dirty so text is reformatted with the new locale.

**`on_enter(dirty)` contract — gate everything, never run work unconditionally.**
`on_enter` is NOT a one-shot "page entered" callback — it is the per-tick refresh hook and fires
**~once per second** (the SYSTEM tick) for the foreground page, plus on every real entry and on
event-driven dirties. Doing unconditional work (or logging) in `on_enter` therefore runs every
second. Structure every `on_enter` as gated branches:

```c
static void on_enter(uint32_t dirty) {
    if (ui_dirty_is_enter(dirty)) {            /* real entry: one-time setup */
        /* preview start, cb (re)register, first paint, immersive statusbar… */
    }
    if (dirty & (1u << UI_STATE_GROUP_SYSTEM))   { /* per-second: MUST dedup (§9) */ }
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS))  { /* language: re-resolve i18n */ }
    if (dirty & (1u << UI_STATE_GROUP_CHAT))      { /* chat status / mode */ }
}
```

- The route passes `UI_DIRTY_ENTER` (`0xFFFFFFFF`, see `core/ui_route.h`) on a real entry and a
  partial group bitmap on tick refreshes; use `ui_dirty_is_enter(dirty)` for entry-only work — never
  a bare `dirty == 0xFFFFFFFF`. Because entry sets all bits, the `& GROUP` branches also run on entry
  (correct for first paint).
- **Never** place a side-effect (LVGL write, `PR_*` log, service call) outside a gate — it executes
  on every per-second tick. Live data from a non-`ui_state` source (camera frames, weather, transcribe
  phase) MUST arrive via that service's callback, not a per-tick `on_enter` poll; `on_enter` only does
  entry catch-up + i18n.
- Good references: `ui_page_clock.c`, `ui_page_call.c`, `ui_page_pomodoro.c`, `ui_page_settings.c`.

### 5.1 What Goes in `ui_state` vs Page-Local State

`ui_state` is for **global, cross-page state** — consumed by multiple pages or persisted across navigation:
- `SYSTEM`: battery, WiFi, volume, charging, online, timestamp (statusbar + multiple pages)
- `SETTINGS`: language, brightness (all pages + persistence layer)
- `CHAT`: device_mode, chat_status (home, chat, settings, mode page)

**Page-local state** should be a file-scope `static` in the page `.c` file:
- State only consumed by that page AND that resets on page re-creation
- Streaming/animation state, scroll position

Rule of thumb: if the state must survive page destroy/re-create cycles, it belongs in `ui_state`.

### 6. Separation of Concerns

| Layer | Directory | Responsibility | May depend on |
|-------|-----------|---------------|---------------|
| Pages | `pages/` | Business screen logic (self-contained) | `widgets/`, `core/`, `style/`, `i18n/`, `services/`, business headers (minimize) |
| Widgets | `widgets/` | Reusable UI components (shared across pages) | `core/`, `style/`, `i18n/` |
| Core | `core/` | State, tick, route, event | Nothing (pure C, no LVGL, no business headers) |
| Services | `services/` | Data providers + device control: keep UI layer free of business/platform headers | `core/`, business/platform headers, **no LVGL** |
| Style | `style/` | Theme tokens, adaptive scaling, layout | Nothing |
| i18n | `i18n/` | Language strings | Nothing |
| Assets | `assets/` | Static resources (fonts, icons) | — |
| Port | `port/` | LVGL driver integration + compat stubs | LVGL, HAL headers |

- **Never** import upward (e.g., `core/` must not include `widgets/` or `pages/`)
- **Never** include LVGL headers in `core/` or `services/` files
- `port/` is the only layer that touches LVGL HAL directly

### 7. Thread Safety

- `ui_state_set_*()` can be called from **any thread** — it only writes a scalar + bitwise-OR on dirty (atomic on Cortex-M33)
- All LVGL operations (`lv_obj_*`) MUST happen in the UI thread (`ui_loop` in `ui_port.c`)
- External code that needs to call LVGL must use `ui_port_lock()` / `ui_port_unlock()`
- `ui_state_consume_dirty()` only runs in the UI thread — no lost updates
- Long-running or I/O operations (KV write, network) MUST run on a workqueue (`tal_workq_schedule`), never in the UI thread

### 7.1 Object Lifetime — null cached pointers on free (MANDATORY)

A file-static pointer that caches an `lv_obj_*` (a widget, a row, a list of items) becomes a
**dangling pointer the instant the object is freed** — including when a *parent* or *container* is
freed, since `lv_obj_del` / `lv_obj_clean` / `lv_obj_del_async` recursively free the whole subtree.
Dereferencing it later (a tick refresh, a service callback, `lv_obj_update_layout`) is a
use-after-free that typically surfaces as a `PC=0` MemFault deep inside the next `_lv_disp_refr_timer`
layout pass — far from the real bug. (Root cause of the 2026-06-30 `ui_loop` crash: chat's
`s_stream_label` survived the bubble that `evict_oldest_bubble()` freed.)

**The invariant: reset the count and null every cached child pointer in the SAME step that frees the
container.** Never leave a window where a static points into freed memory.

```c
/* GOOD — clock.c: null children BEFORE cleaning their parent */
s_arc = NULL; s_timer_face = NULL;
lv_obj_clean(s_content);

/* GOOD — music_list.c / photo.c: clean + reset count together; all access guarded by idx < count */
lv_obj_clean(s_list);   s_row_cnt = 0;
lv_obj_clean(s_grid_scroll);   memset(s_items, 0, sizeof(s_items));   s_item_count = 0;

/* BAD — a pointer that outlives the object it points into */
lv_obj_del(s_bubbles[0]);   /* frees the bubble + its label subtree */
/* s_stream_label still points into the freed label → next append writes freed memory */
```

Checklist when a page/widget caches `lv_obj_*` statics:
- Every `lv_obj_clean`/`lv_obj_del`/`lv_obj_del_async` site nulls (or count-resets) all statics that
  point into the freed subtree — in the same function, no early return in between.
- Guard array access with `idx < count`, and reset `count` together with the `clean`.
- `on_destroy` nulls all of the page's cached statics (most pages already do; keep it exhaustive).
- Prefer live lookups (`lv_obj_get_child`) over cached child pointers when the container is rebuilt.

### 7.2 Buffers LVGL Holds By Pointer (MANDATORY)

§7.1 covers *us* caching *LVGL's* pointers. This is the mirror image: several LVGL APIs
store **the caller's buffer by pointer and never copy it** — `lv_line_set_points()`,
`lv_canvas_set_buffer()`, `lv_img_set_src()` with a descriptor, `lv_chart_set_ext_*_array()`.
LVGL then reads that memory on every redraw.

Because `on_destroy` must use `lv_obj_del_async()` (deleting synchronously would free the
object mid-event), those widgets **outlive `on_destroy` by at least one refresh**. Freeing
the buffer there hands LVGL a dangling pointer for the next frame — a `PC=0` MemFault in
`_lv_disp_refr_timer`, identical in symptom to §7.1 and just as far from the real bug.

**The invariant: unbind before you free.**

```c
/* GOOD — ui_page_screen_test.c on_destroy: drop LVGL's references first */
for (uint8_t i = 0; i < DRAW_MAX_STROKES; i++) {
    if (s_stroke_obj[i]) lv_line_set_points(s_stroke_obj[i], NULL, 0);
}
lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
lv_obj_del_async(s_screen);
tal_free(s_stroke_pts); s_stroke_pts = NULL;   /* nothing points into it now */
```

`lv_line` tolerates `(NULL, 0)` explicitly (`lv_line.c` early-returns when
`point_num == 0 || point_array == NULL`); check the widget's draw path before assuming the
same for other widgets.

A **cross-thread** variant of the same hazard: if a workq thread writes the buffer, a
release from the UI thread must not free it mid-write. Latch the request and let the
completion callback (which runs on the UI thread, after the worker has finished) do the
free — see `ui_svc_detection_release()` / `s_free_pending`.

### 8. Background Services (`services/`)

Services fall into two categories:

**Data services** (e.g., `ui_svc_weather.c`, `ui_svc_call.c`, `ui_svc_picture.c`, `ui_svc_music.c`):
- Fetch data / subscribe events off the UI thread (workqueue or business event threads)
- Expose: `init()`, `get()` / `available()`, `set_cb()` callback hook
- MUST NOT touch LVGL widgets; deliver data via `ui_state_set_*()` or fire the page callback
- **The SERVICE marshals every callback onto the UI thread via `ui_app_async_call()`** before
  invoking it — so page callbacks may touch LVGL directly. Pages never marshal themselves.

**Single-slot callback convention:** `set_cb()` is a single slot (last writer wins). Two lifecycle
patterns, pick by whether the page can be COVERED by another page while its data keeps changing:
- *Covered pages* (music, music list): register in `on_create`/`on_enter`, **clear in `on_leave`**;
  `on_enter` re-registers when unset and does one full resync from `get_status()` to catch up on
  changes missed while covered.
- *Plain pages* (home/weather, call, photo): register in `on_create`, **clear in `on_destroy`**
  (late async results become no-ops).

**Device-control service** (`ui_svc_dev_ctrl.c`):
- Keeps UI layer free of business/platform headers (`gw_intf.h`, `tuya_ai_toy.h`, `tuya_display_hw.h`, etc.)
- Pages call `ui_svc_dev_ctrl_*` instead of SDK APIs directly
- Same isolation rule for the other domains: P2P/IPC call APIs only in `ui_svc_call.c`, picture
  backend + JPEG decode only in `ui_svc_picture.c`, playback control only in `ui_svc_music.c`

**Settings persistence model** (`ui_svc_dev_ctrl`):

```
load()  → call once at startup (early in ui_services_init(), right after
         ui_fs_init() and before the other services start; runs after
         ui_i18n_init/ui_state_init, before the first page renders).
         Reads JSON from KV → applies language/brightness/mode to runtime.
         KV lives on internal flash, so this does NOT depend on the external
         volume. Note ui_fs_init() no longer mounts anything synchronously:
         wukong_storage mounts the volume asynchronously and publishes
         EVENT_WUKONG_STORAGE_READY; services whose one-shot boot load DOES
         need the volume (recording index; playlist/alarms via the store FS
         backend) defer that load to storage.ready (subscribe + ready()
         catch-up through a WORKQ_SYSTEM trampoline — see ui_svc_fs.c)

save()  → call in on_leave() of any settings-related page
         snapshots current ui_state values → compares against s_last_saved
         → if changed: schedules KV write + mode switch on WORKQ_SYSTEM (non-blocking)

*_set() → runtime-only: updates ui_state (and i18n for language, hardware for brightness/volume)
         NO KV write; cheap to call on every slider tick or button tap

*_get() → reads from ui_state (authoritative at runtime)
```

```c
// Startup (ui_app_init → ui_services_init)
ui_i18n_init(UI_LANG_ZH_CN);
ui_state_init();
...
ui_services_init();         // ui_svc_dev_ctrl_load() runs first inside,
                            // overriding defaults with persisted values

// Settings page on_leave
static void on_leave(void) {
    ui_svc_dev_ctrl_save();
}
```

All services are wired in `services/ui_services.c` (`ui_services_init()`), called once from `ui_app_init()`.

### 9. Continuous Animations & Audio Coexistence

This UI shares one CPU with real-time audio (music playback, TTS, AEC). Rendering work competes
directly with the audio threads — an invisible animation can audibly stutter playback. Rules:

- **Stop every `lv_anim` in `on_leave`; restart it in `on_enter`.** `ui_route_push` does NOT destroy
  the covered page — its LVGL tree stays alive under the new page, and a running animation keeps
  invalidating + redrawing at ~33fps beneath an opaque screen, waking the render pipeline for
  nothing. The `on_enter` resync (see §8 single-slot callback) restarts the animation from current
  state. `on_destroy` must also delete the anim (guarded, since `on_leave` already ran).
- **No marquee labels on audio-adjacent pages.** `LV_LABEL_LONG_SCROLL_CIRCULAR` redraws
  continuously and starves the audio threads. Use `LV_LABEL_LONG_DOT`.
- **No `transform_angle` rotation on large widgets.** LVGL 8 renders a transformed widget through an
  intermediate layer (w × h × 4 bytes allocated + software-rotated EVERY frame); on this target the
  allocation can fail (widget silently skipped) and the per-frame cost starves audio. Animate a
  small element's position instead (e.g. the music disc orbits a 10px marker rather than rotating
  the 180px disc) — moving a small object invalidates only its own area.
- **LVGL 8 does NOT dedup same-value writes.** `lv_obj_set_style_*` invalidates the object even when
  the value is unchanged, and `lv_label_set_text` reallocates + invalidates even for identical text.
  Any call site that fires repeatedly (per-second tick, player events) MUST cache the last rendered
  value and skip the write when nothing changed. (`lv_bar_set_value` is the exception — it
  early-returns on equal value.)
- **`LV_LABEL_LONG_DOT` + percent width pitfall:** LONG_DOT bakes the "..." truncation using the
  label's CURRENT width. Setting text while a flex/percent layout is still pending (width ~0) blanks
  the label. Call `lv_obj_update_layout(screen)` once after building the tree, before the first
  `lv_label_set_text`.

---

## Coding Conventions

### File Naming

```
core/ui_state.c          - 响应式状态 + dirty bitmap (3 groups: system/settings/chat)
core/ui_tick.c           - 30ms 定时调度（消费 dirty → 刷新）
core/ui_route.c          - 页面导航栈
style/ui_theme.c         - 颜色/字体 token
style/ui_adaptive.c      - 屏幕自适应缩放
style/ui_layout.c        - 布局常量
i18n/ui_i18n.c           - 多语言接口（中文/英文，运行时切换）
i18n/ui_i18n_data.c      - 语言包数据（designated-initializer 数组，加新语言加列即可）
widgets/ui_comp_statusbar.c  - 全局状态栏（lv_layer_top 单例，FULL/MINIMAL 模式）
widgets/ui_comp_popup.c      - 弹窗（CONFIRM/LOADING/INFO/TOAST）
widgets/ui_comp_btn.c        - 按钮（primary/secondary/text/icon/circle/icon+text）
widgets/ui_comp_label.c      - 文本标签
widgets/ui_comp_card.c       - 卡片容器
widgets/ui_comp_bar.c        - 进度条/数值条
widgets/ui_comp_slider.c     - 滑块（隐藏旋钮 + 可选图标，下拉面板音量/亮度）
widgets/ui_comp_icon.c       - 图标
widgets/ui_comp_link.c       - 超链接标签（拷贝参数，点击回传；对话页"查看图片"）
widgets/ui_comp_picture.c    - RGB565 图片画布（free_fn 决定缓冲所有权，纯渲染器）
widgets/ui_comp_list.c       - 列表
widgets/ui_comp_navbar.c     - 导航栏
widgets/ui_comp_ring_alarm.c - 全局响铃 overlay（闹钟/提醒/倒计时到点；Snooze/Stop）
services/ui_services.c       - 后台服务注册入口（ui_services_init）
services/ui_svc_weather.c    - 天气数据服务（workq 拉取，回调/状态下发）
services/ui_svc_dev_ctrl.c   - 设备控制服务（语言/亮度/音量/模式/重置，load/save 持久化）
services/ui_svc_devinfo.c    - 设备信息只读查询（固件版本/SDK/设备 ID）
services/ui_svc_call.c       - 通话控制服务（P2P/IPC 唯一入口，状态机 + UI 线程封送）
services/ui_svc_picture.c    - 相册图片服务（WORKQ 串行解码，seq latest-wins，附件交接）
services/ui_svc_music.c      - 音乐控制服务（播放事件订阅、播放列表、进度、autoplay）
services/ui_svc_camera.c     - 相机数据服务（预览帧/拍照/缩略图，唯一相机后端入口，UI 线程封送）
services/ui_svc_video.c      - 本地录像服务（共享 MJPEG 相机流、异步 AVI 封装、UI 线程封送）
services/ui_svc_video_playback.c - 本地视频服务（Demo FS 枚举、异步 AVI 解码、帧 latest-wins）
services/ui_svc_recording.c  - 录音服务（录制采集 + 录音列表 + 播放 + 转写态持久化）
services/ui_svc_transcribe.c - 云端转写工作流服务（上传/触发/轮询/下载，常驻轮询线程）
services/ui_svc_detection.c  - 侦测记录服务（thing.ipc.ai.robot.msg.list 拉取 + 分页，WORKQ）
services/ui_svc_fs.c         - 统一文件系统服务（复用 wukong_storage，ui_fs_path / 异步枚举）
services/ui_svc_tm.c         - 时间管理服务（闹钟/日程/倒计时/秒表/番茄钟，观察者 + 到点事件封送）
services/ui_svc_audio_diag.c - 音频诊断服务（audio_dump 通道选择，运行时不持久化）
pages/ui_page_home.c         - 主页（时钟/天气/闹钟/相机+通话快捷按钮）
pages/ui_page_chat.c         - 聊天页面（气泡流 + 附件预览 + 查看图片链接）
pages/ui_page_pulldown.c     - 下拉 overlay（音量/亮度滑块 + 快捷入口）
pages/ui_page_settings.c     - 设置页（语言/模式/AI识图/自动接通/FPS/音频诊断/关于/重置）
pages/ui_page_about.c        - 关于页（固件版本/SDK/设备 ID）
pages/ui_page_mode.c         - 模式选择页（AI 设备主模式）
pages/ui_page_call.c         - 通话页（拨号/接听/拒接/挂断 + 秒级时长时钟）
pages/ui_page_photo.c        - 相册页（viewer + grid 双布局，批量删除，AI 识图）
pages/ui_page_video.c        - 本地视频页（录像列表 + 无音频 AVI 播放）
pages/ui_page_music.c        - 音乐播放页（唱片动画/进度条/播放模式）
pages/ui_page_music_list.c   - 播放列表页（增量行高亮，cJSON 列表）
pages/ui_page_clock.c        - 时钟页（闹钟/倒计时/秒表三 tab，经 ui_svc_tm）
pages/ui_page_schedule.c     - 日程提醒页（列表 + 删除，经 ui_svc_tm）
pages/ui_page_pomodoro.c     - 番茄钟页（环形进度 + 阶段，经 ui_svc_tm）
pages/ui_page_camera.c       - 相机页（实时预览 + 拍照 + 相册缩略图，经 ui_svc_camera）
pages/ui_page_recording.c    - 录音页（录制 / 计时，经 ui_svc_recording）
pages/ui_page_recording_list.c       - 录音列表页（行列表 + 内嵌播放卡 + 转写入口）
pages/ui_page_recording_transcribe.c - 录音转写页（上传转写 + 转写/总结阅读，经 ui_svc_transcribe）
pages/ui_page_files.c        - 文件浏览页（逐级遍历 UI_FS_MOUNT，经 ui_svc_fs）
pages/ui_page_detection.c    - 侦测记录页（云端 AI 告警只读浏览 + 分页，经 ui_svc_detection）
pages/ui_page_audio_diag.c   - 音频诊断页（audio_dump 通道选择，经 ui_svc_audio_diag）
pages/ui_page_ids.h          - Page ID 枚举（集中管理）
port/ui_port.c               - LVGL 初始化 + UI 线程
port/lv_conf.h               - LVGL 编译配置（字体/控件开关）
assets/font/                 - 字体 .c 文件
assets/icon/                 - 图标 .c 文件（C 数组格式）
```

### Default Font

The project default font is `AlibabaPuHuiTi3_Regular18` (CJK + Latin, 18px).
It is set as `LV_FONT_DEFAULT` in `port/lv_conf.h` — all LVGL objects inherit it automatically.
Board-UI builds (`CONFIG_UI_WUKONG_PAGES=n`) do not compile `assets/font/` at all;
`LV_FONT_DEFAULT`/`UI_FONT_DEFAULT` fall back to the LVGL built-in `lv_font_montserrat_14`
(Latin only) — board UIs ship their own fonts and set them explicitly.

- **Never** call `lv_obj_set_style_text_font(obj, UI_FONT_DEFAULT, 0)` — it is redundant
- Only set an explicit font when you intentionally override the default (e.g., the large time label uses `lv_font_montserrat_48`)
- The font contains GB2312 Chinese + ASCII + emoji via NotoEmoji; it does NOT include FontAwesome/`LV_SYMBOL_*` glyphs (those come from Montserrat). Avoid `LV_SYMBOL_OK`, `LV_SYMBOL_CHECK`, etc. for custom UI — use colored `lv_obj` shapes or available icons from `assets/icon/` instead.

### Header Guards

All header guards MUST use double-underscore prefix and suffix: `__<NAME>_H__`

```c
#ifndef __UI_PAGE_CHAT_H__
#define __UI_PAGE_CHAT_H__
...
#endif /* __UI_PAGE_CHAT_H__ */
```

### Page ID Management

All page IDs MUST be defined in `pages/ui_page_ids.h`:

```c
typedef enum {
    UI_PAGE_HOME = 1,
    UI_PAGE_CHAT,
    UI_PAGE_PULLDOWN,
    UI_PAGE_SETTINGS,
    UI_PAGE_ABOUT,
    UI_PAGE_MODE,
    UI_PAGE_CALL,
    UI_PAGE_PHOTO,
    UI_PAGE_MUSIC,
    UI_PAGE_MUSIC_LIST,
    UI_PAGE_CLOCK,
    UI_PAGE_SCHEDULE,
    UI_PAGE_POMODORO,
    UI_PAGE_CAMERA,
    UI_PAGE_RECORDING,
    UI_PAGE_RECORDING_LIST,
    UI_PAGE_RECORDING_TRANSCRIBE,
    UI_PAGE_FILES,
    UI_PAGE_DETECTION,
    UI_PAGE_AUDIO_DIAG,
    // add new pages here
    UI_PAGE_ID_MAX
} ui_page_id_enum_t;
```

- **Never** use magic numbers for page IDs
- IDs start at 1 (0 = `UI_PAGE_NONE` reserved by router)

### Function Naming

```c
// Widget lifecycle
ui_comp_xxx_create(parent, props)       // Required
ui_comp_xxx_refresh(props)              // Singleton refresh (e.g., statusbar)
ui_comp_xxx_update(obj, props)          // Instance update
ui_comp_xxx_destroy(obj)                // Only if holding non-LVGL resources

// Page entry (static lifecycle + exported entry struct)
static void on_create(void *parent);
static void on_enter(uint32_t dirty);
static void on_leave(void);
static void on_destroy(void);
const ui_page_entry_t ui_page_xxx_entry = { ... };

// State operations
ui_state_get_xxx(void)        // Returns const pointer to state group
ui_state_set_xxx(value)       // Sets value + marks dirty if changed
ui_state_consume_dirty(void)  // Returns and clears dirty bitmap (UI thread only)
ui_state_mark_dirty(group)    // Force-mark a group dirty

// Device control service
ui_svc_dev_ctrl_load()             // Startup: restore from KV
ui_svc_dev_ctrl_save()             // on_leave: persist to KV (async, workq)
ui_svc_dev_ctrl_xxx_get()          // Read from ui_state
ui_svc_dev_ctrl_xxx_set(value)     // Update runtime state (no KV write)

// i18n
ui_i18n_text(UI_TEXT_KEY)     // Returns localized string for current language
```

### Popup Widget (`ui_comp_popup`)

Four popup types, all centered on `lv_layer_top()` with a semi-transparent black overlay:

| Type | Usage |
|---|---|
| `UI_COMP_POPUP_CONFIRM` | Destructive / confirm actions — Cancel + OK buttons, both centered |
| `UI_COMP_POPUP_LOADING` | Long async operation (e.g., device reset) — spinner, no buttons |
| `UI_COMP_POPUP_INFO` | One-time notification — OK button only |
| `UI_COMP_POPUP_TOAST` | Transient message, auto-dismiss after `duration_ms` |

```c
// Confirm dialog with callback
ui_comp_popup_show(UI_COMP_POPUP_CONFIRM, title, message, my_cb);
// cb(true) = confirmed, cb(false) = cancelled; popup dismisses automatically

// Loading spinner (shown after confirm — defer with lv_async_call)
static void show_loading_async(void *unused) {
    ui_comp_popup_show(UI_COMP_POPUP_LOADING, NULL, "正在处理…", NULL);
    do_actual_work();
}
static void my_confirm_cb(bool confirmed) {
    if (confirmed) lv_async_call(show_loading_async, NULL);
    // Do NOT call ui_comp_popup_show here — on_confirm dismisses AFTER this cb returns
}

// Toast
ui_comp_popup_toast("已保存", 2000);

// Manual dismiss
ui_comp_popup_dismiss();
```

**Important:** `on_confirm` calls the callback then immediately calls `ui_comp_popup_dismiss()`. If you call `ui_comp_popup_show()` synchronously inside the callback, the new popup gets dismissed by the dismiss call that follows. Always defer the next popup via `lv_async_call()`.

### Buttons (`ui_comp_btn`)

All buttons come from the shared `ui_comp_btn` widget. Do **not** hand-roll buttons by stacking `lv_img` + `lv_label` with absolute positioning.

- **Text/primary/secondary** — `ui_comp_btn_create_styled(parent, text, style, cb)`
- **Icon-only** — `ui_comp_btn_icon_create(parent, icon_src, cb)` — used for title-bar back buttons
- **Circle** — `ui_comp_btn_circle_create(parent, icon_src, text, cb)` — `text=NULL` for icon only
- **Icon + text** — `ui_comp_btn_create_with_icon(parent, icon_src, text, style, pos, cb)`

Every creator calls `lv_obj_remove_style_all()` before applying its own styles. Page-specific fills are added by the page via `lv_obj_set_style_*` on the returned object.

### i18n

```c
// 1. Add key to i18n/ui_i18n.h enum (use designated-initializer slots, ordering matters)
UI_TEXT_NEW_KEY,

// 2. Add translations to i18n/ui_i18n_data.c
[UI_TEXT_NEW_KEY] = "中文",   // in ZH_CN table
[UI_TEXT_NEW_KEY] = "English", // in EN table

// 3. Use in code
lv_label_set_text(label, ui_i18n_text(UI_TEXT_NEW_KEY));

// 4. Re-resolve at render time — never cache pointers across language switches
// WRONG: static const char *cached = ui_i18n_text(KEY);  // stale after language change
// RIGHT: lv_label_set_text(lbl, ui_i18n_text(KEY));       // resolve fresh each time
```

- **Never** hardcode display text in `.c` files
- **Never** cache `ui_i18n_text()` return values across language switches — re-resolve at render time
- Use direct UTF-8 Chinese in `ui_i18n_data.c`, never hex escapes
- `snprintf` format strings that contain Chinese characters (`%d月%d日 %s`) work correctly; ensure argument types match format specifiers exactly — passing an `int` to `%s` causes UB (ARM reads arbitrary memory as a char pointer)

### Memory Management

- Components allocate LVGL objects in `create`; LVGL parent deletion handles cleanup
- If a component allocates non-LVGL resources, it MUST free them in `destroy`
- Use `tal_malloc` / `tal_free` for non-LVGL heap allocation (e.g., workqueue context structs)
- **Never** use raw `malloc/free`

#### Page-scoped buffers: allocate on entry, not in BSS

A file-static array sized for the worst case sits in `.bss` for the whole uptime, even
though most pages are never opened in a given session — and `.heap` is whatever SRAM is
left after `.bss`, so every static KB is a KB the heap never gets. Anything of
appreciable size (roughly ≥1KB: row registries, item pages, point pools) belongs on the
heap, allocated in `on_create` and released in `on_destroy`:

```c
static grid_item_t *s_items;                                  /* not [N] */
#define ITEMS_BYTES  (sizeof(*s_items) * N)                   /* size via the element! */

/* on_create */  s_items = tal_malloc(ITEMS_BYTES);
                 if (s_items) memset(s_items, 0, ITEMS_BYTES);
/* on_destroy */ tal_free(s_items); s_items = NULL; s_item_count = 0;
```

- **Allocation failure must degrade, never block page creation** — render an empty state
  or disable that one feature. Guard every access site (a `count` that stays 0 when the
  buffer is NULL usually covers the loops; explicit callbacks need their own check).
- **`sizeof` trap:** after `array` → `pointer`, `sizeof(s_items)` silently becomes 4, so
  `memset(s_items, 0, sizeof(s_items))` clears 4 bytes and compiles clean. Always size
  blocks via `sizeof(*ptr) * N`. Note `sizeof(s_items[i].title)` (a member array) stays
  correct — don't "fix" those.
- Prefer one struct array over several parallel arrays: one allocation, one lifetime.
- Service-owned buffers that a page drives need an explicit `acquire`/`release` pair, and
  release must respect any in-flight worker — see §7.2.
- Before freeing any buffer you handed to LVGL, unbind it first — see §7.2.

### Streaming Text (UTF-8 Safety)

When appending partial text (e.g., AI streaming responses):
- MUST check UTF-8 multi-byte boundaries before truncating
- Use `lv_label_ins_text(label, LV_LABEL_POS_LAST, text)` for incremental append

```c
// Boundary check: don't cut in the middle of a multi-byte sequence
while (copy_len > 0 && (text[copy_len] & 0xC0) == 0x80) {
    copy_len--;  // back up past continuation bytes
}
```

### Error Handling

- Return `OPERATE_RET` for functions that can fail
- Use early-return pattern, no deep nesting
- Log errors via `PR_ERR()`, warnings via `PR_WARN()`
- **Never** silently swallow errors in component lifecycle functions

---

## Compatibility Layer Rules

The file `port/tuya_ai_display_stub.c` bridges business code and the new framework.

- `tuya_ai_display_msg()` routes messages to the active page's `on_msg()` handler
- **Never** add business logic to the stub; it only routes messages to pages
- **Never** block in stub functions (business code calls from various thread contexts)
- **Never** modify `tuya_ai_display.h` — the interface is frozen

---

## Build System Rules

### Kconfig

- New features go under `if ENABLE_TUYA_UI` in `src/ui/Kconfig`
- Generated macros in `tuya_app_config.h` have **NO** `CONFIG_` prefix (e.g., `UI_DMA2D_ENABLE`)
- **Never** reuse old `CONFIG_TUYA_LVGL_*` or `CONFIG_TUYA_GUI_*` names

### Feature Flags (UI_FEATURE_*)

按依赖簇裁剪功能的编译开关，定义于 `src/ui/Kconfig` 的 `menu "UI Features"`（嵌套在 `if UI_WUKONG_PAGES` 下，
board-UI 板子关掉时整簇降级为 n），经 `tuya_app_config.h` 生成无 `CONFIG_` 前缀宏。簇：CAMERA(相机+相册) / VIDEO(本地录像+播放) / MUSIC /
RECORDING(录音+转写) / TIME(闹钟+日程+番茄钟) / DETECTION / CALL / FILES /
AUDIO_DIAG / WEATHER。核心集（home/chat/settings/about/mode/pulldown +
dev_ctrl/devinfo/fs 服务）不可裁。

机制分工：
- **local.mk**：opt-out 排除式。新增可裁功能 → 在 `UI_DISABLED_SRCS` 加一个
  `ifneq ($(CONFIG_UI_FEATURE_X), y)` 块列出其文件；core 文件靠 glob 自动包含。
- **注册/init/wiring**（`ui_app.c` / `ui_services.c`）：用
  `#if defined(UI_FEATURE_X) && UI_FEATURE_X` 守护引用点。
- **核心页调用可选服务**：不写 `#if`；由总是编译的 `services/ui_svc_stubs.c`
  在功能关闭时提供 no-op/false（`#if !(UI_FEATURE_X)` 守护，与真服务互补）。
  新增此类调用 → 同步在 `ui_svc_stubs.c` 补 stub。
- **运行时入口**：`ui_feature_available(UI_FEATURE_ID_X)`（`core/ui_feature.h`）
  判断，不可用则 `ui_comp_popup_toast(ui_i18n_text(UI_TEXT_FEATURE_UNAVAILABLE), …)`；
  「不可用即隐藏」的列表行（如 settings）可直接据此不创建，无需 toast。

#### 链接安全铁律（MANDATORY — 新增/裁剪任何可裁功能都必须按此做）

**不变式**：当某簇 `UI_FEATURE_X=n` 时，其文件被 local.mk 剔除，文件里的导出符号变为未定义。**任何「始终编译」的 translation unit 对这些符号的引用，都必须要么 `#if defined(UI_FEATURE_X) && UI_FEATURE_X` 守护、要么由 `ui_svc_stubs.c` 提供 stub**，否则该簇 `=n` 时链接报 `undefined reference`。

「始终编译」的 TU 不止核心页，**容易漏的有**：

- **`port/` 层消息分发**（如 `tuya_ai_display_stub.c`）——它按 `ui_route_current()` 把消息路由给页面，会**直接调用可选页的 `ui_page_<x>_on_msg()` 等导出函数**。这类「port→可选页函数」引用必须 `#if UI_FEATURE_X` 守护（守护要卡在 `} else` 与下个 `{` 之间，使关掉时坍缩为合法 C）。← 2026-06-30 的 detection 链接 bug 即出于此处被漏。
- **核心服务**（`ui_svc_dev_ctrl` 等）对可选服务函数的调用——走 stub（如 `ui_svc_call_enabled_get` 等已在 stub TU）。
- **可裁簇 → 另一可裁簇**：一个可裁簇的文件直接调用**另一个可裁簇**的导出符号（如 `ui_svc_recording.c` 调 MUSIC 的 `ui_svc_music_stop()`）。当 A=y 而 B=n 时 A 仍编译、B 被剔除 → 链接失败。同样必须 `#if defined(UI_FEATURE_B) && UI_FEATURE_B` 守护调用点（或 B 加 stub，或 Kconfig `UI_FEATURE_A select UI_FEATURE_B`）。
- `ui_app.c` 的注册 / `on_route_change` / `on_tm_fire` 等 wiring——走 `#if`。

**新增可裁功能时必做的审计（不能只看 `ui_svc_*` 和 `_entry`）**：
```sh
# 该簇所有导出符号（页面 on_msg / set_* 等 + 服务函数）被谁引用：
grep -rn "ui_page_<x>_\|ui_svc_<x>_" --include="*.c" src/ui \
  | grep -v "pages/ui_page_<x>\|services/ui_svc_<x>"   # 排除其自身定义文件
```
逐条判定来源文件：若是「始终编译」TU → 必须 `#if` 守护或补 stub；若是同簇文件 → 随簇进退，无需处理。

**验证以编译矩阵兜底**：每个 feature 单独 `=n` + 全关，都必须能**链接**（不只是编译）。**且必须包含「一簇关、依赖它的另一簇开」的组合**（如 MUSIC=n + RECORDING=y）——单关/全关/全开都不会触发这种 A=y+B=n 的簇间引用。这是发现遗漏引用的唯一可靠手段——单文件/单 diff 的 review 看不到「未改动的始终编译文件」对新裁簇的引用。

### local.mk

- All new UI sources are in the `ifeq ($(CONFIG_ENABLE_TUYA_UI), y)` block
- **Never** add new UI source files outside this guard
- Use `$(shell find ...)` for directories that will grow; explicit paths for single files

### LVGL Source

- The LVGL 8.3.11 source in `lvgl/` is **unmodified upstream**
- **Never** patch LVGL source directly; use `lv_conf.h` or wrapper functions in `port/`

---

## Testing Rules

- UI framework modules do **NOT** require unit tests
- Correctness is verified by on-device testing and visual inspection
- **Never** add test stub headers (e.g., `lvgl.h`) under `src/ui/` — they conflict with the real LVGL include path during cross-compilation

---

## Prohibited Patterns

| Pattern | Why | Do Instead |
|---------|-----|-----------|
| `extern` globals for UI state | Breaks single-direction flow | Use `ui_state_set/get` |
| `lv_timer_create` for refresh | Bypasses dirty-flag tick | Use state dirty → `on_enter(dirty)` |
| `#if defined(CONFIG_T5AI_BOARD)` in UI | Board-specific UI breaks portability | Use runtime config via `ui_adaptive` |
| Hardcoded display strings | Blocks i18n | Use `ui_i18n_text(KEY)` |
| Caching `ui_i18n_text()` return pointer | Pointer becomes stale after language switch | Re-resolve at render time |
| Hex-escaped Chinese (`\xe4\xb8\xad`) | Unreadable source | Use UTF-8 literals directly |
| `sleep` / `delay` in UI thread | Blocks LVGL timer | Use async callbacks / workqueue |
| KV write / network I/O in UI thread | Blocks 30ms tick; causes frame drops | Schedule on `WORKQ_SYSTEM` via `tal_workq_schedule` |
| `lv_obj_del()` in `on_destroy` | Deletes object while LVGL processes events on it | Use `lv_obj_add_flag(HIDDEN)` + `lv_obj_del_async()` |
| Freeing a container (`lv_obj_clean`/`lv_obj_del`) without nulling static pointers into its subtree | Cached pointer dangles → `PC=0` MemFault in a later layout pass (see §7.1) | Null the statics / reset the count in the same step as the free |
| Freeing a buffer LVGL holds by pointer (`lv_line_set_points`, `lv_canvas_set_buffer`, …) without unbinding it | `on_destroy` deletes async, so the widget reads the freed buffer on the next refresh → `PC=0` MemFault (see §7.2) | Unbind (`lv_line_set_points(obj, NULL, 0)`) before `tal_free` |
| Worst-case-sized file-static array (≳1KB) for page-scoped data | Occupies `.bss` for the whole uptime and shrinks `.heap` even when the page is never opened | Allocate in `on_create`, free in `on_destroy` (see Memory Management) |
| `memset(ptr, 0, sizeof(ptr))` after turning an array into a pointer | `sizeof` silently becomes 4 — clears 4 bytes, compiles clean, corrupts data | Size via `sizeof(*ptr) * N` |
| Calling `ui_comp_popup_show(LOADING, …)` synchronously inside a confirm callback | `on_confirm` calls the callback then dismisses the popup — new popup gets torn down immediately | Defer with `lv_async_call()` |
| Setting `UI_FONT_DEFAULT` explicitly on widgets | LV_FONT_DEFAULT already inherited; call is a no-op and clutters code | Omit; only set when overriding |
| Using `LV_SYMBOL_OK` / `LV_SYMBOL_CHECK` for custom checkmarks | LV_SYMBOL glyphs come from Montserrat which is disabled | Use a styled `lv_obj` circle/rect, or an icon from `assets/icon/` |
| Business SDK headers in page files (`tuya_ai_toy.h`, `gw_intf.h`, etc.) | Pages depend on platform layer; harder to test/port | Route through `ui_svc_dev_ctrl` / `ui_svc_call` / `ui_svc_picture` / `ui_svc_music` |
| Leaving an `lv_anim` running in `on_leave` | Covered page's tree stays alive; anim keeps redrawing ~33fps under the new page, starving audio | Stop the anim in `on_leave`, restart from state in `on_enter` (see §9) |
| `LV_LABEL_LONG_SCROLL_CIRCULAR` (marquee) near audio | Continuous redraw starves audio threads | `LV_LABEL_LONG_DOT` |
| `transform_angle` on large widgets | Per-frame intermediate-layer alloc + software rotation; alloc failure skips the widget silently | Animate a small element's position (see §9) |
| Repeated same-value `lv_obj_set_style_*` / `lv_label_set_text` | LVGL 8 doesn't dedup — every call invalidates/reallocates | Cache last rendered value; skip the write when unchanged |
| Modifying `lvgl/src/` | Diverges from upstream | Use `lv_conf.h` or port wrapper |
| Magic page ID numbers | Hard to track | Define in `ui_page_ids.h` enum |
| Hand-rolling buttons in pages | Duplicates layout/press logic | Use `ui_comp_btn_*` and override fills |
| Page importing another page | Creates circular dependency | Use `ui_route_push(ID)` |
| `i18n_text()` format string with wrong arg types | UB on ARM — int used as char* pointer reads arbitrary memory, may render as Korean | Match argument types to format specifiers exactly |

---

## Quick Reference: Adding a New Page

```c
// 1. Add page ID to pages/ui_page_ids.h
UI_PAGE_SETTINGS,

// 2. Create pages/ui_page_settings.c
#include "lvgl.h"
#include "ui_route.h"
#include "ui_theme.h"
#include "ui_adaptive.h"
#include "ui_i18n.h"
#include "ui_state.h"
#include "ui_comp_btn.h"
// NO business SDK headers here — use ui_svc_dev_ctrl if needed
#include "ui_page_ids.h"

LV_IMG_DECLARE(icon_back_24_24);
static lv_obj_t *s_screen = NULL;
// ... other static handles

static void back_click_cb(lv_event_t *e) { (void)e; ui_route_pop(); }

// MANDATORY: swipe-right = back (see "3.1 Swipe-Back Navigation")
static void gesture_cb(lv_event_t *e) {
    (void)e;
    if (lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_RIGHT) {
        lv_indev_wait_release(lv_indev_get_act());  // guard: don't click the page below
        ui_route_pop();
    }
}

static void on_create(void *parent) {
    (void)parent;
    s_screen = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_size(s_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_screen, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);  // MANDATORY for swipe-back
    lv_obj_add_event_cb(s_screen, gesture_cb, LV_EVENT_GESTURE, NULL);                 // MANDATORY: swipe-right = back
    lv_obj_set_style_pad_top(s_screen, ui_adapt(32), 0);  // MANDATORY: clear statusbar (or it covers the title bar)

    // Standard title bar: back button (left) + centered title — see "3.2 Page Title Bar"
    lv_obj_t *titlebar = lv_obj_create(s_screen);
    lv_obj_remove_style_all(titlebar);
    lv_obj_set_size(titlebar, LV_PCT(100), ui_adapt(48));
    lv_obj_set_style_bg_opa(titlebar, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(titlebar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *back = ui_comp_btn_icon_create(titlebar, &icon_back_24_24, back_click_cb);
    lv_obj_align(back, LV_ALIGN_LEFT_MID, ui_adapt(8), 0);
    lv_obj_t *title = lv_label_create(titlebar);
    lv_label_set_text(title, ui_i18n_text(UI_TEXT_SETTINGS_TITLE));
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // build the rest of the widget tree below the title bar...
}

static void on_enter(uint32_t dirty) {
    if (dirty & (1u << UI_STATE_GROUP_SETTINGS)) {
        // refresh i18n text; reset any dedup guards
    }
    if (dirty & (1u << UI_STATE_GROUP_CHAT)) {
        // refresh mode-related UI
    }
}

static void on_leave(void) {
    ui_svc_dev_ctrl_save();   // if this page has settings-related sliders/pickers
}

static void on_destroy(void) {
    if (s_screen) {
        lv_obj_add_flag(s_screen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_del_async(s_screen);
        s_screen = NULL;
        // NULL all other static handles
    }
}

const ui_page_entry_t ui_page_settings_entry = {
    .id = UI_PAGE_SETTINGS,
    .name = "settings",
    .lifecycle = { on_create, on_enter, on_leave, on_destroy }
};

// 3. Register in ui_app.c
extern const ui_page_entry_t ui_page_settings_entry;
ui_route_register(&ui_page_settings_entry);
```

---

## Quick Reference: Adding a New Widget

Widgets in `widgets/` are **only** for small reusable UI elements shared across multiple pages.
Page-specific UI stays inside the page file itself.

```c
// widgets/ui_comp_volume_bar.h
typedef struct { uint8_t level; bool muted; } ui_comp_volume_bar_props_t;
lv_obj_t *ui_comp_volume_bar_create(lv_obj_t *parent, const ui_comp_volume_bar_props_t *props);
void ui_comp_volume_bar_update(lv_obj_t *obj, const ui_comp_volume_bar_props_t *props);
```

---

## Quick Reference: Adding i18n Text

```c
// 1. Add key to i18n/ui_i18n.h enum (before UI_TEXT_MAX)
UI_TEXT_NEW_KEY,

// 2. Add translations to both tables in i18n/ui_i18n_data.c
[UI_TEXT_NEW_KEY] = "中文",    // ZH_CN
[UI_TEXT_NEW_KEY] = "English", // EN

// 3. Use in code — resolve fresh, never cache
lv_label_set_text(label, ui_i18n_text(UI_TEXT_NEW_KEY));
```

---

## Quick Reference: Settings Persistence

```c
// Changing a setting (UI thread, on slider/button event):
ui_svc_dev_ctrl_language_set(lang);     // updates i18n + ui_state, no KV
ui_svc_dev_ctrl_brightness_set(value);  // updates hardware + ui_state, no KV
ui_svc_dev_ctrl_volume_set(value);      // updates audio + ui_state
ui_svc_dev_ctrl_mode_set(mode);         // updates ui_state only

// Persisting on page exit:
static void on_leave(void) {
    ui_svc_dev_ctrl_save();  // async, compares vs. shadow copy, writes KV only if changed
}

// Reset device (clears ui KV then unbinds):
ui_svc_dev_ctrl_reset();
```
