# 链路策略方案

## 模式定义

- mode 0: 仅4G
- mode 1: WiFi/4G 自动切换

默认状态由配网结果决定：4G配网激活成功设置mode=0，WiFi配网成功设置mode=1。
App可通过MQTT协议修改，设备本地也可通过 `tuya_ai_linkpolicy_set()` 接口设置。

## 链路切换操作

### 1. 进入配网（临时状态，不保存）

订阅事件 `EVENT_WIFI_NETWORK_STATUS`，参数 `STAT_UNPROVISION_AP_STA_UNCFG` 触发：

```c
tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_WIFI);
tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_DEFAULT);
```

### 2. 仅4G（mode=0）

4G配网激活成功（订阅事件 `EVENT_LINK_ACTIVATE`，`activate_info_t.linkage == LINKAGE_TYPE_CAT1`）：

```c
tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_CAT1);
```

### 3. WiFi/4G自动切换（mode=1）

WiFi配网激活成功（`activate_info_t.linkage == LINKAGE_TYPE_WIFI`）：

```c
tuya_svc_netmgr_linkage_set_default(LINKAGE_TYPE_DEFAULT);
```

## 实现要点

- [x] 设备状态保存，上电加载，KV操作（key: `ai.linkpolicy`，值与本地缓存比对，避免重复写flash）
- [x] 设备本地设置接口：`tuya_ai_linkpolicy_set(mode)`
- [x] MQTT协议注册 `mqc_app_reg_ext_proto`，回复 `mqc_app_ext_proto_data_rept`
- [x] CLI测试命令：`tuya_ai_linkpolicy_cli_cmd`
- [x] 设备能力上报：订阅 `EVENT_RUN`，通过 `ty_meta_report` 上报 `supportMultLink: true`

## 设备能力上报

订阅 `EVENT_RUN` 事件（`SUBSCRIBE_TYPE_ONETIME`），SDK激活完成后上报设备能力：

```c
ty_cJSON *meta = ty_cJSON_CreateObject();
ty_cJSON_AddBoolToObject(meta, "supportMultLink", TRUE);
ty_meta_report(meta, REPORT_MODE_DEFAULT);
ty_cJSON_Delete(meta);
```

字段说明：
- `supportMultLink: true` — 告知云端设备支持多链路切换能力，App详情页据此展示相关配置入口

## MQTT协议交互

### 1. App设置多网络工作模式（cloud -> device, protocol 64）

```json
{
    "protocol": 64,
    "t": 1459168450,
    "data": {
        "reqType": "multLinkConfig",
        "mode": 0
    }
}
```

### 2. App主动查询当前多网络工作模式（cloud -> device, protocol 64）

```json
{
    "protocol": 64,
    "t": 1459168450,
    "data": {
        "reqType": "multLinkStatus"
    }
}
```

### 3. 设备回复（device -> cloud, protocol 65）

设置和查询共用同一回复格式：

```json
{
    "protocol": 65,
    "t": 1459168450,
    "data": {
        "reqType": "multLinkStatus",
        "mode": 1
    }
}
```

## 文件说明

- `tuya_ai_linkpolicy.h` — 接口声明
- `tuya_ai_linkpolicy.c` — 实现：KV持久化、MQTT handler、事件订阅、本地设置接口、CLI测试命令
