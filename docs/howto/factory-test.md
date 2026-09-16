# 成品产测（整机产测）

## 目标

说明量产/试产阶段如何使用成品产测：设备上电后自动扫描产测热点，扫到后**连接热点进入局域网**，由涂鸦成品产测上位机工具驱动完成整机测试（按键/LED/录音播音等测试项、固件升级）。全链路入口是 `prodtest_ssid_scan()`（SDK `svc_mf_test` 组件），app 侧参考实现是 `src/miscs/mftest/tuya_ai_toy_mf_test.c` 的 `ty_ai_toy_mf_test_init()`。

```
上电 → tuya_app_main.c
  ├─ ty_ai_toy_mf_test_init()      # prodtest_app_register() 注册产测 SSID 与回调
  └─ prodtest_ssid_scan(500)       # 扫信道 6 的 beacon，500ms 窗口
       ├─ 扫到成品产测 SSID（默认 ty_ai_mf_test）
       │     → 连接热点 → UDP 广播设备发现（上位机按 MAC/SN 认领设备）
       │     → 上位机「成品产测-AI 产品」工具下发测试项 / 固件升级
       ├─ 扫到本地自检 SSID（默认 ty_ai_mf_test_1，wukong 扩展的离线快检）
       │     → 不连接：LED 闪烁（约 1Hz）+ 循环播放提示音，产线快速判断喇叭/主板
       └─ 都没扫到 / 守卫不过 → 返回 false，走正常业务启动
```

触发即接管：`tuya_app_main` 直接 return，正常配网/AI 业务不会启动。SSID 说明：

| 通路 | 默认 SSID | 匹配规则 | 热点要求 |
|---|---|---|---|
| 成品产测（标准流程） | `ty_ai_mf_test`（SDK 原始默认 `tuya_mdev_test4`，被 app 覆写） | 全等 | 2.4G、开放（无需密码）、可连接 |
| 本地自检（wukong 扩展，可选） | `ty_ai_mf_test_1` | SDK 前缀收集 + app 回调全等筛选 | 仅需 beacon 可见 |

## 前置条件

- 编译配置：`ENABLE_PRODUCT_AUTOTEST=1`（SDK 平台预置）且 `CONFIG_ENABLE_AI_MF_TEST=y`（T5AI_BOARD 默认已开，其他板检查 `build/appconfig/<板名>`）。
- **设备未配网、未激活**：有配网记录就不再扫描；设备激活运行超过 15 分钟后 KV 写入关闭标志 `MF_TEST_CLOSE_FLAG`，**永久关闭产测入口**（防止市售设备被同名热点误触发），此后只有整机擦除重烧才能恢复。
- 产测路由器：SSID `ty_ai_mf_test`，**2.4G、无需密码**，建议固定**信道 6**（设备扫描只看信道 6，见 `PRODTEST_LISTEN_CHANNEL`）。
- 上位机：涂鸦「生产解决方案」中的**成品产测 - AI 产品**工具（PMS 账号登录），与产测路由器同一局域网；安装与操作流程见[涂鸦开发者平台：AI 语音盒子成品产测](https://developer.tuya.com/cn/docs/iot/T5-AI-01?id=Kekc0a3zoz6n0)。
- 设备一般应已完成写授权，具体以产线工单/产测 Json 配置为准。

## 步骤

### 1. 搭建产线环境

产测路由器按上述要求开启；上位机装好产测工具、导入产测 Json（或输入工单号），确认 UDP 端口（5560）等连接参数——详见上面的公网操作文档。

### 2. 设备上电

先开热点、后给设备上电（扫描窗口只有 500ms）。设备扫到 SSID 后自动连接热点并广播设备发现报文，上位机设备列表出现该设备后，按工具流程运行测试。

### 3.（可选）定制产测 SSID

两种方式按需选择：

| 方式 | 改什么 | 影响范围 |
|---|---|---|
| API 注册 | 构造 `prodtest_app_cfg_t` 调 `prodtest_app_register()`（后注册覆盖先注册），`prod_ssid` 字段即成品产测 SSID | 全量可定制：成品产测 SSID、自检 SSID 列表、回调、`gwcm_mode` |
| 改宏重编 | `src/miscs/mftest/tuya_ai_toy_mf_test.c` 中 `PRODUCT_TEST_WIFI`（成品产测）/ `PRODUCT_TEST_WIFI_1`（本地自检） | 两类都可改 |

API 注册的参考写法（即 `ty_ai_toy_mf_test_init()` 的做法）：

```c
#include "prod_test.h"

static const char *my_selftest_list[] = { "my_selftest_ssid" };  /* 本地自检热点列表，可不用 */

prodtest_app_cfg_t cfg = {
    .gwcm_mode  = GWCM_OLD_PROD,              /* GWCM_OLD 会直接跳过扫描 */
    .prod_ssid  = "my_mf_ssid",               /* 成品产测 SSID，全等匹配 */
    .ssid_list  = my_selftest_list,
    .ssid_count = 1,
    .file_name  = APP_BIN_NAME,
    .file_ver   = USER_SW_VER,
    .app_cb     = my_ssid_info_cb,            /* 扫到自检热点后的回调 */
    .product_cb = my_mf_cmd_proc,             /* 产测命令的应用侧扩展处理 */
};
prodtest_app_register(&cfg);
```

注册必须发生在 `prodtest_ssid_scan()` 之前（`tuya_app_main.c` 中两者相邻，改 `ty_ai_toy_mf_test_init()` 内部即可）。

## 验证方法

串口日志按链路分段：

| 日志 | 含义 |
|---|---|
| `ty_ai_toy_mf_test_init` | 产测配置已注册 |
| `prodtest_ssid_scan`（`ignored` = 未触发，走正常启动） | 扫到热点，已进入产测 |
| `prodtest_connect success` | 已连上产测热点 |
| `Send Broadcast UDP Discover Package...` | 正在广播设备发现，等上位机认领 |

上位机侧：设备列表出现该设备（按 MAC/SN），运行后逐项显示测试结果。

本地自检通路（`ty_ai_mf_test_1`）的整机现象：LED 灭 = 信号弱（RSSI < -60dBm，靠近热点重新上电）；LED 闪烁（约 1Hz）+ 循环提示音 = 自检进行中（最长 4 小时）；LED 常亮 = 结束。

退出产测态：移除热点后重新上电即可。

## 常见问题

**上电完全没有扫描动作（连 `prodtest_ssid_scan ignored` 都没有）**
`CONFIG_ENABLE_AI_MF_TEST` 未开（`ty_ai_toy_mf_test_init` 日志也不会有），或 SDK 配置里 `ENABLE_PRODUCT_AUTOTEST` 不为 1——整段代码被编译裁剪。

**日志一直是 `prodtest_ssid_scan ignored`**
按守卫顺序排查：
1. **配过网**：设备有配网记录就直接跳过。解绑或恢复出厂清掉配网信息后重试。
2. **激活超 15 分钟**：日志出现 `have actived over 15 min, not enter mf_init`，KV 关闭标志已写入，该设备产测入口永久关闭（保护市售设备），需整机擦除重烧才能恢复——产线排查请换未激活设备。
3. **热点信道不对**：扫描只看信道 6，路由器信道固定为 6。
4. **时序不对**：扫描窗口仅 500ms，必须先开热点再给设备上电。
5. **`gwcm_mode` 配成了 `GWCM_OLD`**：该模式下不扫描（除非先调 `prodtest_ignore_wcm()`）；参考实现用 `GWCM_OLD_PROD`。

**扫到了但连接失败（`prodtest_connect failed`）**
确认热点是 2.4G、开放网络（无密码）、信号正常；连接失败设备会放弃产测走正常启动，重新上电再试。

**连上热点但上位机发现不了设备**
上位机与产测路由器必须同一局域网；检查产测工具的 UDP 端口配置（5560）与产测电脑防火墙是否放行 UDP。

**SSID 匹配规则容易混**
成品产测 `prod_ssid` 是**全等**匹配；本地自检 `ssid_list` 在 SDK 收集阶段是**前缀**匹配（`strncmp`，同前缀热点如 `ty_ai_mf_test_10` 也会被收进结果），wukong 参考实现在回调里又做了一次**全等**筛选才真正进入自检。自定义 `app_cb` 时注意这一层差异，产线环境做好 SSID 隔离。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
