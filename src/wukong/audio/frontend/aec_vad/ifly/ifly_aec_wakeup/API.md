# ifly_aec_wakeup 库 API 文档

## 概述

离线语音唤醒和回声消除库。

**头文件**
- `ivw_module.h` - 唤醒词识别
- `ifly_aec.h` - 回声消除
- `vtn_auth_adapter.h` - 网络授权

---

## 1. 唤醒词识别 (ivw_module.h)

### 配置结构
```c
typedef struct {
    const char* mlp_res_data;       // MLP 资源指针
    size_t mlp_res_size;            // MLP 资源大小
    const char* keyword_res_data;   // 关键词资源指针
    size_t keyword_res_size;        // 关键词资源大小
    ivw_wakeup_callback_t wakeup_callback;  // 唤醒回调
    void* user_param;               // 用户参数
} ivw_config_t;

typedef int (*ivw_wakeup_callback_t)(const char* param, void* userparam);
```

### 错误码
```c
#define IVW_OK              0   // 成功
#define IVW_ERROR_PARAM    -1   // 参数错误
#define IVW_ERROR_INIT     -2   // 初始化失败
#define IVW_ERROR_NOT_INIT -3   // 未初始化
```

### 接口函数

**ivw_module_init**
```c
int ivw_module_init(const ivw_config_t* config);
```
初始化唤醒模块（资源需预先加载）。

**ivw_module_deinit**
```c
int ivw_module_deinit(void);
```
反初始化。

**ivw_module_is_initialized**
```c
int ivw_module_is_initialized(void);
```
检查是否已初始化，返回 1 已初始化，0 未初始化。

**ivw_module_process_audio**
```c
int ivw_module_process_audio(const int16_t* audio_data, int byte_size);
```
处理音频数据检测唤醒词。
- `audio_data`: 16kHz/16bit/单声道，建议 320 字节（160 采样点）
- 返回: `IVW_OK` 成功

**ivw_module_get_version**
```c
const char* ivw_module_get_version(void);
```
获取版本号。

---

## 2. 回声消除 (ifly_aec.h)

### 接口函数

**ifly_aec_set_mic_number**
```c
void ifly_aec_set_mic_number(int number);
```
设置麦克风数量（1 或 2），需在 `ifly_aec_init` 前调用。

**ifly_aec_init**
```c
int ifly_aec_init(void);
```
初始化 AEC，返回 0 成功。

**ifly_aec_uninit**
```c
int ifly_aec_uninit(void);
```
反初始化，返回 0 成功。

**ifly_aec_process**
```c
int ifly_aec_process(const short *in, short *out, int num_samples);
```
执行 AEC 处理。
- `in`: 输入音频，多通道交织（L/R/Ref）
- `out`: 输出单声道
- `num_samples`: 固定 128 采样点
- 返回: 0 成功

**ifly_aec_get_mic_number**
```c
int ifly_aec_get_mic_number(void);
```
获取当前麦克风数量。

**ifly_aec_get_version**
```c
const char* ifly_aec_get_version(void);
```
获取版本号。

---

## 3. 网络授权 (vtn_auth_adapter.h)

### 接口函数

**vtn_net_auth_init**
```c
int vtn_net_auth_init(char* appid, char* sn, const char* token);
```
初始化授权模块。
- `appid`: 应用 ID
- `sn`: 设备序列号
- `token`: 保存的令牌（首次为 NULL）

**vtn_net_auth_verify**
```c
char* vtn_net_auth_verify(void);
```
执行网络授权验证，返回令牌字符串（需保存）。

**vtn_net_auth_get_status**
```c
int vtn_net_auth_get_status(void);
```
获取授权状态，返回 0 已授权，非 0 未授权。

---

## 使用示例

### 初始化
```c
#include "ivw_module.h"
#include "ifly_aec.h"
#include "vtn_auth_adapter.h"

static int wakeup_cb(const char* param, void* userparam) {
    printf("唤醒词: %s\n", param);
    return 0;
}

void init_audio(void) {
    // 1. 初始化授权
    vtn_net_auth_init(APP_ID, DEVICE_SN, saved_token);
    
    // 2. 初始化 AEC（双麦）
    ifly_aec_set_mic_number(2);
    ifly_aec_init();
    
    // 3. 初始化 IVW
    ivw_config_t cfg = {
        .mlp_res_data = mlp_data,
        .mlp_res_size = mlp_size,
        .keyword_res_data = keyword_data,
        .keyword_res_size = keyword_size,
        .wakeup_callback = wakeup_cb,
        .user_param = NULL
    };
    ivw_module_init(&cfg);
}
```

### 音频处理
```c
void process_frame(short* mic_data) {
    short aec_out[128];
    static short ivw_buf[160];
    static int cnt = 0;
    
    // 1. AEC 处理（128 采样点）
    ifly_aec_process(mic_data, aec_out, 128);
    
    // 2. 累积到 160 采样点
    memcpy(&ivw_buf[cnt * 128], aec_out, 256);
    if (++cnt < 3) return;
    cnt = 0;
    
    // 3. 应用增益
    for (int i = 0; i < 160; i++) {
        ivw_buf[i] *= 5;
    }
    
    // 4. IVW 检测
    ivw_module_process_audio(ivw_buf, 320);
}
```

### 清理
```c
void deinit_audio(void) {
    ivw_module_deinit();
    ifly_aec_uninit();
}
```

---

## 注意事项

1. **资源加载**: IVW 资源需预先加载到内存，不支持文件路径
2. **帧长要求**: AEC 固定 128 采样点，IVW 建议 160 采样点
3. **音频格式**: 16kHz, 16bit, 单声道（AEC 输出已是单声道）
