# 新增一个 MCP 工具

## 目标

在 wukong demo 中为 AI 智能体新增一个本地 MCP（Model Context Protocol）工具，让 LLM 可以在对话中调用它控制设备。本文以新增 `device_night_light_set`（开关一盏夜灯并设置亮度）为例，演示推荐的实现模式——**把 MCP 指令转换为 DP、注入既有 DP 处理通道**，完成从工具模块实现、注册、编译开关到验证的全过程：

```
LLM 决定调用 MCP 工具
   │  handler(args)                          # 你的工具模块
   ▼
构造 {"dps":{...}} → sf_send_gw_dev_cmd(DP_CMD_MCP)   # 异步入队，与 App/云端下发 DP 汇入同一条处理链
   ▼
__soc_dev_obj_dp_cmd_cb()                    # src/tuya_app_main.c，既有 DP 回调
   ▼
业务执行 + DP 状态上报                        # 与 App 控制共用同一份业务逻辑，面板状态自动同步
```

这样做的好处：设备的业务动作只在 DP 回调里实现一份，MCP（语音）、App、云端三种控制入口行为天然一致，状态上报也不用另写。具体把 MCP 参数映射成哪些 dpid，由你的产品 DP schema 决定，本文的夜灯 dpid 仅为示例。

MCP 模块的整体架构、能力清单、已有工具列表见 [`src/wukong/toolkits/mcp/README.md`](../../src/wukong/toolkits/mcp/README.md)；本文只覆盖"新增一个工具"这一具体任务，遇到概念性问题（框架分层、回调扩展模式等）请回去看那篇。

## 前置条件

- 已按[快速开始](../quickstart.md)跑通编译、烧录、对话。
- 读过 [`src/wukong/toolkits/mcp/README.md`](../../src/wukong/toolkits/mcp/README.md) 的"架构"和"编译时配置"两节，理解 `MCP_ENABLE_TOOLS`（框架层，`mcp_server.h` 里默认开）和 `ENABLE_TOOLKITS_*`（应用层，Kconfig 配置）两级开关的区别。
- 确认总开关已打开：`build/appconfig/T5AI_BOARD` 等预置配置里 `CONFIG_ENABLE_TUYA_TOOLKITS=y`（默认板已经是开的，新板/精简配置需自行确认）。

## 步骤

### 1. 创建工具模块文件

在 `src/wukong/mcp/tools/` 下新增一对 `.h`/`.c`，头文件只声明一个 `init` 函数（照抄现有工具，如 `mcp_tool_motion.h` 的写法）：

```c
// src/wukong/mcp/tools/mcp_tool_night_light.h
#ifndef __MCP_TOOL_NIGHT_LIGHT_H__
#define __MCP_TOOL_NIGHT_LIGHT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET mcp_tool_night_light_init(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __MCP_TOOL_NIGHT_LIGHT_H__ */
```

`.c` 里实现 handler 并用 `MCP_TOOL_ADD` 注册。handler 签名固定为 `MCP_TOOL_HANDLER_CB`（见 `mcp_server_tools.h`）：

```c
// src/wukong/mcp/tools/mcp_tool_night_light.c
#include "mcp_tool_night_light.h"
#include "wukong_ai_mcp.h"

#include "smart_frame.h"          /* sf_send_gw_dev_cmd / SF_GW_DEV_CMD_S */
#include "tuya_cloud_com_defs.h"  /* DP_CMD_EXT_APP */
#include "tal_log.h"

/* MCP 来源的 DP 命令类型：SDK 在 tuya_cloud_com_defs.h 预留了应用扩展区
 * DP_CMD_EXT_APP = [100,110]，应用侧自定义取值放在这个区间即可，
 * 不需要改 SDK 头文件；若应用内已占用 100，顺延取值。 */
#define DP_CMD_MCP  (DP_CMD_EXT_APP)

/* 示例 DP 映射——具体映射哪些 dpid、什么类型，由你的产品 DP schema 决定 */
#define DPID_NIGHT_LIGHT_SWITCH  "20"   /* bool：夜灯开关 */
#define DPID_NIGHT_LIGHT_BRIGHT  "22"   /* value：亮度 */

STATIC ty_cJSON *__make_text_content(CONST CHAR_T *text)
{
    ty_cJSON *arr = ty_cJSON_CreateArray();
    if (arr) {
        ty_cJSON_AddItemToArray(arr, mcp_content_make_text(text));
    }
    return arr;
}

STATIC OPERATE_RET __set_night_light(CONST CHAR_T *name, CONST ty_cJSON *args,
                                      ty_cJSON **out_content, BOOL_T *is_error,
                                      VOID *user_data)
{
    ty_cJSON *j = NULL;

    (VOID)name;
    (VOID)user_data;

    j = args ? ty_cJSON_GetObjectItem(args, "enable") : NULL;
    if (j == NULL || !ty_cJSON_IsBool(j)) {
        *is_error = TRUE;
        *out_content = __make_text_content("Missing or invalid 'enable'");
        return OPRT_INVALID_PARM;
    }
    BOOL_T enable = ty_cJSON_IsTrue(j) ? TRUE : FALSE;

    /* 1. MCP 参数 → DP 报文。格式必须是 {"dps":{"<dpid>":<值>}}，
     *    与 App/云端下发 DP 的载荷一致；控制子设备时可加 "devId" 字段。 */
    ty_cJSON *root = ty_cJSON_CreateObject();
    ty_cJSON *dps  = ty_cJSON_CreateObject();
    if (root == NULL || dps == NULL) {
        ty_cJSON_Delete(root);
        ty_cJSON_Delete(dps);
        *is_error = TRUE;
        *out_content = __make_text_content("Out of memory");
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddBoolToObject(dps, DPID_NIGHT_LIGHT_SWITCH, enable);
    j = ty_cJSON_GetObjectItem(args, "brightness");
    if (j && ty_cJSON_IsNumber(j)) {
        ty_cJSON_AddNumberToObject(dps, DPID_NIGHT_LIGHT_BRIGHT, j->valueint);
    }
    ty_cJSON_AddItemToObject(root, "dps", dps);

    /* 2. 注入既有 DP 处理通道。sf_send_gw_dev_cmd 是异步接口：拷贝命令结构、
     *    投递到高优先级工作队列后即返回，返回 OPRT_OK 只代表入队成功，
     *    实际执行发生在 __soc_dev_obj_dp_cmd_cb（src/tuya_app_main.c）。
     *    JSON 所有权：入队成功后由框架在处理完成时释放，调用方不得再动；
     *    入队失败才由调用方释放。（同款用法见 SDK svc_ai_agent 对
     *    DP_CMD_AI_SKILL 的处理。） */
    SF_GW_DEV_CMD_S gd_cmd = {DP_CMD_MCP, root};
    OPERATE_RET rt = sf_send_gw_dev_cmd(&gd_cmd);
    if (rt != OPRT_OK) {
        ty_cJSON_Delete(root);
        TAL_PR_ERR("night light send dp cmd failed: %d", rt);
        *is_error = TRUE;
        *out_content = __make_text_content("Failed to dispatch DP command");
        return rt;
    }

    *out_content = __make_text_content("OK");
    return OPRT_OK;
}

OPERATE_RET mcp_tool_night_light_init(VOID)
{
    return MCP_TOOL_ADD(
        "device_night_light_set",
        "Turn the night light on/off and set its brightness.",
        __set_night_light, NULL,
        MCP_SCHEMA_BOOL("enable", "true to turn on, false to turn off"),
        MCP_SCHEMA_INT_OPT_RANGE("brightness", "Brightness (0-100), only meaningful when enable=true", 0, 100)
    );
}
```

- 参数校验失败（如缺少必填参数）要把 `*is_error` 置 `TRUE` 并在 `out_content` 里放文字说明，见 `mcp_tool_control.c` 里 `__set_mode` 的写法。
- `smart_frame.h` 是 SDK `svc_dp` 组件头文件，app 内已有使用先例（`src/miscs/battery/tuya_ai_battery.c`、`src/wukong/audio/wukong_playback_ctrl.c`），不需要额外加 include path。
- DP 真正的业务执行写在既有 DP 回调 `__soc_dev_obj_dp_cmd_cb`（`src/tuya_app_main.c`）里——如果该 dpid 的处理逻辑已经为 App 控制实现过，MCP 这条路**零新增业务代码**；需要区分指令来源时，回调入参 `TY_RECV_OBJ_DP_S` 的 `cmd_tp` 字段即本次的 `DP_CMD_MCP`。
- 可用的 schema 宏（`MCP_SCHEMA_INT` / `_OPT` / `_RANGE`、`STR`、`STR_ENUM`、`BOOL`、`NUM`，以及必填/可选变体）都在 `mcp_server_tools.h` 里；`MCP_TOOL_ADD` 会自动追加 `MCP_SCHEMA_END` 终止符，直接用它，不要手写底层的 `mcp_server_tool_register`。
- 工具描述（第二个参数）要写清楚**什么场景下 LLM 应该调用它**，参考已有工具的描述风格（如 `device_camera_take_photo` 的"用户请求查看、识别或描述视觉内容时必须先调用此工具"）。

### 2. 在 `wukong_ai_mcp.c` 中注册

编辑 `src/wukong/mcp/wukong_ai_mcp.c`，加 include 和受 `ENABLE_TOOLKITS_*` 宏保护的初始化调用：

```c
#include "tools/mcp_tool_night_light.h"

// 在 wukong_ai_mcp_init() 内，仿照其他 toolkit 加一段：
#if defined(ENABLE_TOOLKITS_NIGHT_LIGHT) && (ENABLE_TOOLKITS_NIGHT_LIGHT == 1)
    TUYA_CALL_ERR_LOG(mcp_tool_night_light_init());
#endif
```

### 3. 在 Kconfig 中加配置项

编辑 `src/wukong/mcp/Kconfig`，在 `if ENABLE_TUYA_TOOLKITS` 块内新增一项（照抄已有 `ENABLE_TOOLKITS_MOTION` 等的格式）：

```kconfig
    config ENABLE_TOOLKITS_NIGHT_LIGHT
        bool "Night light toolkit"
        default n
        help
          AI可控制夜灯开关与亮度
```

### 4. 在 `local.mk` 中加条件编译

编辑应用根目录 `local.mk`，在 `ifeq ($(CONFIG_ENABLE_TUYA_TOOLKITS), y)` 块内（约第 193 行起，跟其他 `mcp_tool_*` 同一段）新增：

```makefile
ifeq ($(CONFIG_ENABLE_TOOLKITS_NIGHT_LIGHT), y)
LOCAL_SRC_FILES += $(shell find $(LOCAL_PATH)/src/wukong/mcp/tools/mcp_tool_night_light.c -name "*.c" -o -name "*.cpp" -o -name "*.cc")
endif
```

头文件搜索路径已经包含 `src/wukong/mcp`（`local.mk` 里 `LOCAL_TUYA_SDK_CFLAGS += -I$(LOCAL_PATH)/src/wukong/mcp`），所以工具模块里可以直接 `#include "wukong_ai_mcp.h"`，不需要额外加 include path。

### 5. 打开配置并生成配置头

```bash
cd apps/tuyaos_demo_wukong_ai
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
# 顶层菜单找到 "Enable MCP toolkits" → 勾选 "Night light toolkit"
make app_config APP_NAME=tuyaos_demo_wukong_ai
```

> **注意**：改过 `Kconfig` / `local.mk` 后必须重跑 `make app_config`，否则 `include/tuya_app_config.h` 不更新，`ENABLE_TOOLKITS_NIGHT_LIGHT` 不会生效。

### 6. 编译

```bash
make app APP_NAME=tuyaos_demo_wukong_ai
```

## 验证方法

1. **编译期**：确认 `include/tuya_app_config.h` 里出现 `#define ENABLE_TOOLKITS_NIGHT_LIGHT 1`，且 `make app` 编译通过、`mcp_tool_night_light.c` 被链接进产物（无未定义符号报错）。
2. **启动日志**：烧录后从串口日志确认工具注册成功，`mcp_server_tools.c` 在注册成功时会打印：
   ```
   Tool registered: device_night_light_set
   Description: Turn the night light on/off and set its brightness.
   Input schema: {...}
   ```
   以及 `wukong_ai_mcp.c` 里的 `MCP Server initialized successfully`。
3. **运行期**：通过语音/对话让 AI 触发该工具（如"打开夜灯"），沿链路核对三段日志：
   - `mcp_server_tools.c` 在每次工具调用结束时打印 `mcp tool finished name=device_night_light_set is_error=0 eid=...`；`is_error=1` 说明 handler 走了错误分支，结合日志里打印的 `out_content` 文本排查参数。
   - smart_frame 收到注入命令时打印 `Rev Cmd 100, data:...`（`100` 即 `DP_CMD_MCP` 取值，`components/svc_dp/src/smart_frame.c`）——没有这行说明 `sf_send_gw_dev_cmd` 没被调用或入队失败。
   - 既有 DP 回调 `__soc_dev_obj_dp_cmd_cb` 收到对应 dpid——走到这里说明已汇入标准 DP 通道，剩下与 App 下发无异。
4. **状态闭环**：DP 回调执行并上报后，涂鸦智能 App 面板上该 DP 的状态应同步变化——这是"MCP 与 App 共用一条通道"生效的直接证据。
5. **单元测试（可选）**：参考 `src/wukong/mcp/tools/tests/test_suite.py` 的声明式注册方式（`_TESTS` 列表里已有 `mcp`/`mcp_tools`/`mcp_control` 三组），为新工具写一组 C 测试并加入该列表，然后跑：
   ```bash
   cd apps/tuyaos_demo_wukong_ai
   pytest src/wukong/mcp/tools/tests/test_suite.py -k night_light -v
   ```

## 常见问题

**改了 Kconfig / local.mk 但工具没生效**
少跑了 `make app_config APP_NAME=tuyaos_demo_wukong_ai`——所有 `CONFIG_*` 改动都要经它落到 `include/tuya_app_config.h`，`wukong_ai_mcp.c` 里的 `#if defined(ENABLE_TOOLKITS_XXX)` 才能生效。

**分不清 `MCP_ENABLE_TOOLS` 和 `ENABLE_TOOLKITS_XXX`**
前者是框架层开关（`mcp_server.h` 里定义，默认已开，控制整个 Tools 协议能力是否编译），后者是应用层开关（Kconfig 生成，控制某一个具体 tool 模块是否编译/初始化）。新增工具通常只需要关心后者。

**`MCP_TOOL_ADD` 和 `mcp_server_tool_register` 该用哪个**
用宏 `MCP_TOOL_ADD`，它会自动在变参末尾追加 `MCP_SCHEMA_END`（即 `NULL`）终止符。直接调用底层的 `mcp_server_tool_register` 需要手动加终止符，容易漏掉导致变参解析越界。

**handler 里忘记设置 `out_content`，LLM 侧收到空响应**
`out_content` 是出参，框架不会给默认值；即便是"仅记录一下、无返回内容"的工具也建议至少放一个 `mcp_content_make_text("OK")`，避免客户端解析到空数组时行为不一致。

**参数校验失败但没报错**
只要业务判定为失败（缺参数、参数不合法、底层调用失败等），必须显式 `*is_error = TRUE`；不设置的话即便返回非 `OPRT_OK`，LLM 侧也可能把这次调用当作成功处理。

**工具描述写得太笼统，LLM 不会主动调用**
参考 `src/wukong/toolkits/mcp/README.md`"工具清单"里各工具的说明风格，在描述里明确写清楚触发场景（"用户请求 XXX 时必须调用""不确定当前状态时先调用 YYY 查询"等），而不是只写一句功能概述。

**日志报 `dev null or no dps`**
注入的 JSON 缺少 `"dps"` 对象——`sf_send_gw_dev_cmd` 的载荷必须是 `{"dps":{...}}` 结构（`__sf_handle_recv_dp` 强制校验），把 dpid 键值直接放在顶层是不行的。另外该报错也可能是设备未激活、DP 控制结构未就绪，先确认设备已配网激活。

**`sf_send_gw_dev_cmd` 返回 OK 但灯没动作**
返回 OK 只代表**入队成功**，不代表执行成功。按验证方法第 3 条沿三段日志定位：有 `Rev Cmd 100` 没到 DP 回调，多为 JSON 格式/dpid 不在 schema 内被过滤；到了回调没动作，查回调里该 dpid 的业务分支。

**JSON 释放崩溃或内存泄漏**
所有权规则记牢：`sf_send_gw_dev_cmd` 返回 `OPRT_OK` 后，`cmd_js` 由框架在异步处理完成时释放，调用方再 `ty_cJSON_Delete` 就是 double-free；返回失败时框架不会接管，调用方必须自己释放。注意 handler 入参 `args` 归框架所有，不要把 `args` 的子节点直接挂进 `cmd_js`（需要就 `ty_cJSON_Duplicate`）。

**`DP_CMD_MCP` 该取什么值**
用 SDK 预留的应用扩展区 `DP_CMD_EXT_APP`（[100,110]，`tuya_cloud_com_defs.h`），不要占用 0~9 的既有取值，也不要改 SDK 头文件。SDK 另有 `DP_CMD_AI_SKILL`（云端 AI 技能 DeviceControl 下发用）——MCP 自定义一个独立取值的意义在于 DP 回调里能通过 `cmd_tp` 区分"云端技能下发"与"端侧 MCP 工具"两种来源。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
