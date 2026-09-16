# 新增或裁剪一个对话模式

## 目标

在 wukong demo 中新增一个对话模式（闲聊子模式 或 设备模式），或者从产品固件里裁剪掉不需要的模式，完成从模式实现、配置注册到编译验证的全流程。本文以新增一个假想的闲聊子模式 `AI_CHAT_SUB_MY_MODE` 为例（设备模式的登记步骤同理，差异处会单独说明），并给出裁剪现有模式的对应做法。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通任意一块板（能编译、烧录、对话）。
- 读过 [`src/mode/README.md`](../../src/mode/README.md) 的「概述」「状态机」两节，理解模式模块的**两级模式层次**：
  - 一级 **设备模式** `AI_DEVICE_MODE_E`（`src/mode/wukong_ai_mode.h`）：闲聊 / 翻译 / P2P / 录音 / 生图 / 侦测，决定当前业务形态。
  - 二级 **闲聊子模式** `AI_CHAT_SUB_MODE_E`（同文件）：长按 / 单次按键 / 唤醒 / 自由，仅在设备模式为「闲聊」时生效，决定语音触发方式。
- 先想清楚新模式是哪一级：只改变语音触发方式（要不要长按、要不要唤醒词）→ 闲聊子模式；引入一种新的整机业务形态（比如新的云端场景/新的数据上行方式）→ 设备模式。

## 步骤

### 1. 新增模式开关（Kconfig）

编辑 `src/mode/Kconfig`，在 `menu "Enabled AI Mode"` 块（`ENABLE_AI_MODE_HOLD` 等选项所在处）新增一项：

```kconfig
        config ENABLE_AI_MODE_MY_MODE
            bool "Chat-MyMode (一句话描述触发方式)"
            default n
```

如果这个新模式需要能被设成「上电默认模式」，还要在同文件的 `choice "Default AI Mode"` 里补一个 `config AI_CHAT_DEFAULT_MY_MODE`（`depends on ENABLE_AI_MODE_MY_MODE`），并在 `config TUYA_AI_CHAT_DEFAULT_MODE` 的 `default N if AI_CHAT_DEFAULT_XXX` 列表里追加一行（`N` 取下一个未使用的序号，闲聊子模式排在 0–3，设备模式排在 4–8，参照 `src/mode/Kconfig` 现有的 9 行）。

### 2. 实现模式源文件

新建 `src/mode/wukong_ai_mode_my_mode.c`，整体包在 `#if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)` 里——仓库里每一个模式文件（hold/oneshot/wakeup/free/translate/p2p/record/picture/detection）都是这样，关掉开关后整个 `.c` 编译成空翻译单元，不产生任何代码。骨架（照抄 `src/mode/wukong_ai_mode_oneshot.c` 的真实结构，函数名/回调签名与其一致，仅把 `oneshot` 换成 `my_mode`）：

```c
/**
 * @file wukong_ai_mode_my_mode.c
 * @brief MyMode implementation.
 */
#include "tuya_cloud_types.h"
#include "wukong_ai_mode.h"
#include "wukong_kws.h"
#include "tuya_ai_toy.h"

#if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)

STATIC AI_CHAT_MODE_HANDLE_T s_ai_my_mode_cb = {0};
STATIC AI_CHAT_MODE_PARAM_T  s_ai_my_mode = {0};
STATIC AI_CHAT_STATE_E       s_ai_cur_state = AI_CHAT_INVALID;

STATIC OPERATE_RET wukong_ai_my_mode_int_cb(VOID *data, INT_T len)
{
    /* 设置 VAD 模式（WUKONG_AUDIO_VAD_MANUAL 手动 / WUKONG_AUDIO_VAD_AUTO 自动） */
    wukong_audio_input_wakeup_mode_set(WUKONG_AUDIO_VAD_AUTO);
    wukong_kws_enable();   /* 不需要唤醒词的模式用 wukong_kws_disable() */

    CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_MY_MODE, s_ai_my_mode.state, AI_CHAT_IDLE);
    s_ai_my_mode.wakeup_stat = FALSE;
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_deint_cb(VOID *data, INT_T len)
{
    wukong_ai_agent_input_stop();
    s_ai_cur_state = AI_CHAT_INVALID;
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_task_cb(VOID *data, INT_T len)
{
    /* 按 s_ai_my_mode.state 做状态机轮询，参照 wukong_ai_oneshot_task_cb 实现 */
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_event_cb(VOID *data, INT_T len)
{
    TUYA_CHECK_NULL_RETURN(data, OPRT_INVALID_PARM);
    WUKONG_AI_EVENT_T *event = (WUKONG_AI_EVENT_T *)data;

    switch (event->type) {
    case WUKONG_AI_EVENT_ASR_OK:
        CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_MY_MODE, s_ai_my_mode.state, AI_CHAT_THINK);
        break;
    case WUKONG_AI_EVENT_TTS_PRE:
        CHAT_SUB_STATE_CHANGE(AI_CHAT_SUB_MY_MODE, s_ai_my_mode.state, AI_CHAT_SPEAK);
        break;
    default:
        break;
    }
    return OPRT_OK;
}

STATIC OPERATE_RET wukong_ai_my_mode_key_cb(VOID *data, INT_T len)
{
    /* 物理按键事件：PUSH_KEY_TYPE_E，参照 wukong_ai_oneshot_key_cb 实现触发逻辑 */
    return OPRT_OK;
}

/* 未实现的回调保持 NULL：dispatch 时会打印 "does not support: xxx" 并返回
 * OPRT_NOT_FOUND，是安全的默认行为（见 src/mode/wukong_ai_mode.c 的
 * MODE_DISPATCH_CALL 宏），不需要每个回调都写。 */

OPERATE_RET ai_my_mode_register(AI_CHAT_MODE_HANDLE_T **cb)
{
    s_ai_my_mode_cb.on_init  = wukong_ai_my_mode_int_cb;
    s_ai_my_mode_cb.on_deinit = wukong_ai_my_mode_deint_cb;
    s_ai_my_mode_cb.on_key   = wukong_ai_my_mode_key_cb;
    s_ai_my_mode_cb.on_task  = wukong_ai_my_mode_task_cb;
    s_ai_my_mode_cb.on_event = wukong_ai_my_mode_event_cb;
    /* 按需补 on_wakeup / on_vad / on_client / on_notify_idle /
     * on_audio_input / on_picture / on_interrupt，签名见
     * src/mode/wukong_ai_mode.h 的 AI_CHAT_MODE_HANDLE_T */
    *cb = &s_ai_my_mode_cb;
    return OPRT_OK;
}

#endif /* ENABLE_AI_MODE_MY_MODE */
```

设备模式的写法完全一样，只是状态变更宏换成 `DEVICE_MODE_STATE_CHANGE(AI_DEVICE_MODE_MY_MODE, ...)`，注册函数建议命名 `ai_my_mode_register`（约定俗成，不强制）。

不需要改 `local.mk`：应用根目录 `local.mk` 里 `LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/mode -name "*.c" ...)` 已经通配整个 `src/mode/` 目录，新文件会自动被纳入编译。

### 3. 在模式管理器中登记

编辑 `src/mode/wukong_ai_mode.h`：

- 在 `AI_CHAT_SUB_MODE_E`（闲聊子模式）或 `AI_DEVICE_MODE_E`（设备模式）里，在对应的 `_MAX` 之前新增枚举值，例如 `AI_CHAT_SUB_MY_MODE`。

编辑 `src/mode/wukong_ai_mode.c`：

1. 在文件头部的 `extern` 声明区（第 18–26 行，`extern OPERATE_RET ai_hold_register(...)` 等）新增一行 `extern OPERATE_RET ai_my_mode_register(AI_CHAT_MODE_HANDLE_T **cb);`。
2. 在 `_chat_sub_str[]`（第 36–38 行）或 `_device_mode_str[]`（第 40–42 行）里按枚举顺序补一个字符串，供日志打印使用。
3. 若 `ENABLE_TUYA_UI` 打开，`s_chat_sub_display[]` / `s_device_mode_display[]`（第 51–65 行）也要同步补一个中文展示名，否则 UI 通知会越界读到脏数据。
4. 在 `wukong_ai_mode_init()` 里（第 511–545 行是各模式的注册块）按新开关新增一段：

   ```c
   #if defined(ENABLE_AI_MODE_MY_MODE) && (ENABLE_AI_MODE_MY_MODE == 1)
       __register_chat_sub(AI_CHAT_SUB_MY_MODE, ai_my_mode_register);
   #endif
   ```

   设备模式对应改用 `__register_device_mode(AI_DEVICE_MODE_MY_MODE, ai_my_mode_register)`。

### 4. 让板型启用它 / 设为默认

`ENABLE_AI_MODE_XXX` 默认是 `n`，需要显式开启才会被编译进对应板型固件，有两条路径：

- **单板临时调试**：`make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai` → `AI Mode Configuration` → `Enabled AI Mode` 勾选新选项；如需设为默认模式，在同菜单的 `Default AI Mode` 里选中。保存后自动触发 `make app_config`（也可手动执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 生成 `include/tuya_app_config.h`）。
- **产品固化到某个板型**：编辑该板型的预置配置快照 `build/appconfig/<板型名>`（如 `build/appconfig/T5AI_BOARD`），把 `# CONFIG_ENABLE_AI_MODE_MY_MODE is not set` 改成 `CONFIG_ENABLE_AI_MODE_MY_MODE=y`（需要设默认模式时同时改 `CONFIG_TUYA_AI_CHAT_DEFAULT_MODE=N`），然后 `make app_config_choice APP_NAME=tuyaos_demo_wukong_ai` 选中该板型、`make app_config APP_NAME=tuyaos_demo_wukong_ai` 生效。各板型当前启用哪些模式见 [`build/README_CN.md`](../../build/README_CN.md) 2.3 节的表格。

> 若新增的是闲聊子模式，并且希望能通过语音 / MCP 指令切换（如「切换到自由对话」），还需要在 `src/wukong/mcp/tools/mcp_tool_control.c` 的 `s_mode_map[]`（模式名 ↔ `AI_CHAT_SUB_MODE_E` 映射表）里补一行，否则 `get_mode`/`set_mode` 这两个 MCP 工具认不出新模式。

### 5. 编译

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## 验证方法

1. 烧录后看串口日志：`wukong_ai_mode_init` 阶段应打印 `init chat sub-mode N (my_mode)`（闲聊子模式，来自 `src/mode/wukong_ai_mode.c` 的初始化分支）或 `init device mode N (my_mode)`（设备模式）；切模式时应打印 `switch chat sub-mode to N (my_mode)` / `switch device mode to N (my_mode)`。
2. 走一轮触发路径：观察 `CHAT_SUB_STATE_CHANGE` / `DEVICE_MODE_STATE_CHANGE` 打印的 `mode %s state change from %s to %s` 日志，应按 `INIT → IDLE → LISTEN → UPLOAD → THINK → SPEAK → IDLE` 正常迁移。
3. 若 `ENABLE_TUYA_UI` 打开：切换到新模式后应弹出「已成功切换到 XX」通知（`__post_mode_switched_notify` 产生）；正常走完一轮对话，TTS 能正常播放。
4. 若通过 `s_mode_map[]` 暴露给 MCP：调用 `get_mode` 工具确认返回的字符串是新模式名，`set_mode` 能切换过去。
5. 断电重新上电：确认模式能按 `TUYA_AI_CHAT_DEFAULT_MODE` 或上次切换后保存的模式正确恢复（`wukong_ai_toy_trigger_mode_get/set` 落到 KV，见 `src/tuya_ai_toy.c`）。

## 裁剪（移除不需要的模式）

大多数场景**不需要删代码**，只需要关闭 Kconfig 开关：

- 单板调试：`make app_menuconfig` 关掉对应 `ENABLE_AI_MODE_XXX`，`make app_config` 生效。
- 产品固化：在该板型的 `build/appconfig/<板型名>` 快照里把 `CONFIG_ENABLE_AI_MODE_XXX=y` 改成 `# CONFIG_ENABLE_AI_MODE_XXX is not set`，`make app_config_choice` + `make app_config`。

原理：每个模式 `.c` 文件的全部内容都包在 `#if defined(ENABLE_AI_MODE_XXX) && (ENABLE_AI_MODE_XXX == 1)` 里（`wukong_ai_mode.c` 里对应的注册调用也一样加了 `#if`），关闭开关后该文件编译为空翻译单元、注册调用整段被预处理器裁掉，不占运行时资源，也不需要碰 `local.mk`。

如果确定某个产品线永远不会用到某个模式，可以进一步物理清理（非必需）：删除对应 `wukong_ai_mode_xxx.c`、从 `wukong_ai_mode.h` 的枚举里移除、从 `wukong_ai_mode.c` 的 `extern` 声明/注册调用/显示名数组里去掉对应行。由于枚举值删除会让后面的值整体前移，务必同步核对 `_chat_sub_str[]` / `_device_mode_str[]` / UI 展示名数组的下标仍然一一对应。

裁掉的模式如果正好是某板型 `TUYA_AI_CHAT_DEFAULT_MODE` 或用户上次保存的触发模式，`wukong_ai_mode_init()` 会自动 fallback 到下一个已启用的闲聊子模式（不会崩溃，见 `src/mode/wukong_ai_mode.c` 第 552–572 行），但体验上要记得改选一个真正保留的模式作为默认值，见 [`build/README_CN.md`](../../build/README_CN.md) 3.11 节。

## 常见问题

**`make app_menuconfig` 里看不到新加的 `ENABLE_AI_MODE_XXX`**
`src/mode/Kconfig` 里的 `config` 块缩进/位置写错，没有落在 `menu "Enabled AI Mode"` 内；或者忘记保存后重新进入 menuconfig。

**改了 Kconfig / appconfig 快照但固件行为没变**
没有执行 `make app_config APP_NAME=tuyaos_demo_wukong_ai` 重新生成 `include/tuya_app_config.h`——所有 `CONFIG_*` 改动都要经它落到头文件才生效，这和适配新板子时的坑一样。

**新模式一直没有被激活，还是走旧模式**
检查两点：`wukong_ai_mode_init()` 里 `__register_chat_sub`/`__register_device_mode` 的调用是否真的被对应的 `#if ENABLE_AI_MODE_XXX` 包住并且该宏确实为 1（看 `include/tuya_app_config.h` 里 `CONFIG_ENABLE_AI_MODE_XXX` 是否是 `1`）；`TUYA_AI_CHAT_DEFAULT_MODE` 或 KV 里保存的触发模式是否仍指向旧模式。

**切换模式返回 `OPRT_NOT_SUPPORTED`**
目标模式的 `enabled` 标志没有被置位——通常是 Kconfig 开关没打开，或者枚举值超出了 `AI_CHAT_SUB_MAX` / `AI_DEVICE_MODE_MAX` 边界。

**日志里模式名显示乱码或空字符串**
`_chat_sub_str[]` / `_device_mode_str[]`（以及 `ENABLE_TUYA_UI` 下的 `s_chat_sub_display[]` / `s_device_mode_display[]`）没有和新枚举值同步补齐，数组下标越界读到了未初始化内存。

**MCP 的 `set_mode`/`get_mode` 认不出新的闲聊子模式**
只有需要语音/MCP 可控时才用得到：去 `src/wukong/mcp/tools/mcp_tool_control.c` 的 `s_mode_map[]` 补一行 `{"my_mode_name", AI_CHAT_SUB_MY_MODE}`。

**新增的是需要摄像头的设备模式（比如参考 P2P/生图/侦测），某些板子编译或运行异常**
确认该板型已开启 `ENABLE_TUYA_CAMERA`；P2P 模式还需要在子项里单独打开 `ENABLE_AI_MODE_P2P`（见 [`build/README_CN.md`](../../build/README_CN.md) 2.2 节）。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
