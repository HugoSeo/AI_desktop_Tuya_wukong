<!-- Auto-translated from ../../howto/customize-ui.md. Do not edit manually. -->
# Customize the UI

## Goal

Understand the different ways to customize the UI in the wukong demo, which files each one touches, and where to find the detailed rules.

This guide is navigation only — **it does not repeat the framework details**. The UI framework's architecture, data flow, and coding conventions are authoritatively defined in
[`src/ui/RULES.md`](../../../src/ui/RULES.md) (mandatory for both AI tools and human contributors), with subsystem documentation in
[`src/ui/README.md`](../../../src/ui/README.md). Read both before touching `src/ui/`.

## Prerequisites

- You have built and flashed by following the [Quick Start](../quickstart.md).
- The board has the new UI framework enabled (`CONFIG_ENABLE_TUYA_UI=y`; check/enable it via `make app_menuconfig`).

## Steps

UI customization has four tiers; pick one based on the scope of your change, smallest to largest:

### ① Modify an existing wukong page

Page source lives at `src/ui/pages/ui_page_<name>.c` (e.g. `ui_page_home.c`, `ui_page_settings.c`);
see the "II. Page Implementation" section of `src/ui/README.md` for the page list.

- **Pages are self-contained**: a page's layout, state, and rendering logic all live in its own `.c` file — don't split it into separate files;
  only extract to `src/ui/widgets/` when a UI element is reused across **multiple** pages (`RULES.md` §2/§6).
- Just modify that page's four lifecycle callbacks — `on_create` (build the widget tree), `on_enter` (refresh by dirty bits), `on_leave`, `on_destroy` —
  no need to touch routing, registration, or any other framework code.
- When adding/changing state display, follow the single-direction data flow: business thread `ui_state_set_*()` marks dirty → `ui_tick` (30ms) →
  the page's `on_enter(dirty)` consumes it → refreshes LVGL (`RULES.md` §1, §5).

### ② Add a new page

Follow the "Quick Reference: Adding a New Page" template in `src/ui/RULES.md`:

1. Add a `UI_PAGE_XXX` to the enum in `src/ui/pages/ui_page_ids.h` (magic numbers are forbidden).
2. Create `src/ui/pages/ui_page_xxx.c`, implement `on_create/on_enter/on_leave/on_destroy`, and export
   `const ui_page_entry_t ui_page_xxx_entry`.
3. In `ui_app_init()` in `src/ui/ui_app.c`, `extern` that entry and call
   `ui_route_register(&ui_page_xxx_entry)` (following the existing 21 `ui_route_register` calls already in that file).
4. To navigate to it from another page, use `ui_route_push(UI_PAGE_XXX)` (don't let pages `#include` each other).

New pages must likewise follow the self-containment, single-direction data flow, and i18n constraints — see the FAQ below and `RULES.md`.

### ③ Trim whole feature clusters (`UI_FEATURE_*`, no code changes)

When some feature pages aren't needed (no camera, no recording, etc.), don't delete code — trim the whole
cluster out of the firmware via Kconfig: `make app_menuconfig` → the **UI Features** menu under the UI
configuration, 9 switches in total (camera & photo album, music, recording & transcribe, time management,
detection records, P2P call, file browser, audio diagnostics, weather), all enabled by default. Turning one
off removes, at the same time:

- the corresponding page and background-service source files (excluded from the build — saves flash/RAM directly);
- the entry card in App Center (the card table is gated by `ui_feature_available()` and hides automatically).

Core pages (home/chat/settings/diagnostics) are unaffected: service functions of disabled clusters are provided
as no-op fallbacks by `src/ui/services/ui_svc_stubs.c`, so linking succeeds and the UI degrades gracefully.
Note that some switches depend on lower-level capabilities (e.g. the camera cluster depends on
`ENABLE_TUYA_CAMERA`, the recording cluster on `ENABLE_AI_MODE_RECORD`) and stay invisible in the menu when
the underlying capability is off. After changing switches, run `make app_config APP_NAME=tuyaos_demo_wukong_ai`
to regenerate the config headers, then rebuild.

### ④ Fully custom board UI (bypassing wukong pages)

If the goal is for a board to not use wukong's default page set at all, take the board-UI-takeover approach: implement
`app_ui_init()` / `app_ui_msg_handler()` under the board directory, and call
`ui_app_register_board_ui(app_ui_init, app_ui_msg_handler)` in `tuya_device_board_init()`. This is
step 6 of [Porting a New Board](porting-new-board.md); the full explanation and existing board examples
(`T5AI_BOARD_ROBOT`/`T5AI_BOARD_EVB`/`T5AI_BOARD_EVB_PRO`/`T5AI_BOARD_EYES`) are in the "UI Implementation" section of `src/boards/README.md`.

## Verification

1. `make app_config APP_NAME=tuyaos_demo_wukong_ai` (after changing Kconfig/switches) →
   `make app APP_NAME=tuyaos_demo_wukong_ai` builds successfully.
2. Flash to a real device: the target page enters/exits normally, touch gestures (swipe right to go back, etc.) work.
3. Switch between Chinese and English (the language item on the settings page), and confirm any new/changed text follows along, with no leftover text in the old language.
4. Leave the page open for several minutes and confirm there's no audio stutter caused by misuse of animations/`lv_timer` (especially test this while music is playing).

## FAQ

**Changed state but the page doesn't refresh**
Check whether you used `ui_state_set_*()` to mark dirty, rather than changing a page-internal variable directly and expecting it to take effect automatically; also check whether
`on_enter(dirty)` is missing the branch for the corresponding `UI_STATE_GROUP_*` bit (`RULES.md` §5).

**New page/widget uses `lv_timer_create` to refresh itself**
The framework forbids this — refresh must always go through dirty bits → `on_enter(dirty)`, otherwise it bypasses the single-direction data flow constraint
(`RULES.md`'s "Prohibited Patterns").

**Text turns to garbage / doesn't change after switching languages**
The pointer returned by `ui_i18n_text()` **must not be cached across a language switch** — re-call it at render time every time; new text must go through both
the `i18n/ui_i18n.h` + `i18n/ui_i18n_data.c` language tables — hardcoded strings are forbidden (`RULES.md`'s "i18n" section).

**Crash (`PC=0`) right after destroying a page / clearing a list**
Most likely a cached `lv_obj_*` static pointer wasn't nulled out in the same step as its container being freed by `lv_obj_clean`/`lv_obj_del`,
and a later tick refresh or async callback dereferences the dangling pointer. Freeing the container and nulling every static pointer into its subtree must happen in the same step
(`RULES.md` §7.1).

**Custom board UI doesn't take effect, still shows the default pages**
Check whether `ui_app_register_board_ui()` is actually called inside `tuya_device_board_init()`, and whether the call happens
before UI init; see the FAQ entry of the same name in [Porting a New Board](porting-new-board.md).

## Support

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
