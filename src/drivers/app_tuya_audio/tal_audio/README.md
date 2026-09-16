# tal_audio

`tal_audio` 提供统一的音频采集输入接口和音频播放输出接口，当前从实现上可见：

- 输入侧公共接口在 `include/tal_audio_input.h`
- 输出侧公共接口在 `include/tal_audio_output.h`
- 当前仓库已接入的输入实现包括 ADC、Digital Mic（DMIC）和 I2S
- 当前仓库已接入的输出实现包括 DAC 和 I2S
- UAV 类型已在公共头文件中预留，但当前仓库内默认实现未启用

本文档基于 `src/tal_audio_input.c` 和 `src/tal_audio_output.c` 的当前实现整理，重点说明接口用途、调用顺序、阻塞行为和多线程使用约束。

## 0. 组件范围

### 0.1 当前实现范围

按当前仓库中的设备开关与实现文件，`tal_audio` 的实际落地能力如下：

| 方向 | 公共类型 | 当前仓库实现状态 | 主要实现文件 |
| --- | --- | --- | --- |
| 输入 | `TAL_AUDIO_INPUT_ADC` | 已实现 | `src/audio_devices/tal_audio_adc.c` |
| 输入 | `TAL_AUDIO_INPUT_DMIC` | 已实现 | `src/audio_devices/tal_audio_dmic.c` |
| 输入 | `TAL_AUDIO_INPUT_I2S` | 已实现 | `src/audio_devices/tal_audio_i2s.c` |
| 输入 | `TAL_AUDIO_INPUT_UAV` | 预留 | `src/tal_audio_input.c` |
| 输出 | `TAL_AUDIO_OUTPUT_DAC` | 已实现 | `src/audio_devices/tal_audio_dac.c` |
| 输出 | `TAL_AUDIO_OUTPUT_I2S` | 已实现 | `src/audio_devices/tal_audio_i2s.c` |
| 输出 | `TAL_AUDIO_OUTPUT_UAV` | 预留 | `src/tal_audio_output.c` |

### 0.2 关键实现特征

- 输入侧通过底层设备回调把 PCM 帧上送给业务层（ADC、DMIC、I2S 共用同一回调 `tal_audio_input_data_cb`）
- 输出侧通过内部 worker 线程和 ring buffer 异步喂数给底层播放设备（DAC 或 I2S）
- I2S 同时支持输入（RX）和输出（TX），通过 `tkl_i2s_*` 系列接口与硬件交互
- 输入句柄内部有互斥锁，但业务层仍应把同一 `handle` 当作共享资源统一串行管理
- 输出写入接口 `tal_audio_output_write` 是阻塞接口
- 当前测试代码位于 `src/test/test_audio_onboard.c` 和 `src/test/test_audio_i2s.c`

## 1. 输入接口说明

### 1.1 适用场景

音频输入接口用于从底层音频设备采集 PCM 数据，并通过回调把数据上送到业务层。

当前接口句柄类型：

```c
typedef VOID_T *TAL_AUDIO_INPUT_HANDLE;
```

### 1.2 配置结构

输入初始化使用 `TAL_AUDIO_INPUT_CFG_T`：

```c
typedef struct {
    TAL_AUDIO_INPUT_TYPE_E type;
    union {
        TAL_AUDIO_INPUT_ANALOG_T    ai_adc_conf;
        TAL_AUDIO_INPUT_I2S_T       ai_i2s_conf;
        TAL_AUDIO_INPUT_UAV_T       ai_uav_conf;
        TAL_AUDIO_INPUT_DMIC_T      ai_dmic_conf;
    } dev;

    TUYA_AUDIO_SAMPLE_BITS_E sample_bits;
    UINT32_T sample_rate;
    UINT32_T frame_time_ms;

    TAL_AUDIO_INPUT_CB audio_input_cb;
    VOID_T *args;
} TAL_AUDIO_INPUT_CFG_T;
```

关键参数说明：

- `type`：输入设备类型
- `dev`：对应设备的硬件配置（见下方各设备配置结构说明）
- `sample_bits`：采样位宽，接口参数检查接受 8/16/24/32 bit 枚举值；但当前 T5 常用路径和测试代码均使用 16 bit
- `sample_rate`：采样率，不能为空
- `frame_time_ms`：单帧时长，不能为空
- `audio_input_cb`：采集数据回调，不能为空
- `args`：回调私有参数，会原样传回给 `audio_input_cb`

各设备配置结构：

ADC 配置 `TAL_AUDIO_INPUT_ANALOG_T`：

```c
typedef struct {
    TUYA_AUDIO_ADC_PORT_E port;
    TUYA_AUDIO_ADC_CHAN_E chan;
} TAL_AUDIO_INPUT_ANALOG_T;
```

- `port`：ADC 端口
- `chan`：声道选择，`TUYA_AUDIO_ADC_CHANNEL_L` / `TUYA_AUDIO_ADC_CHANNEL_R` 为单声道，`TUYA_AUDIO_ADC_CHANNEL_LR` 为双声道

DMIC 配置 `TAL_AUDIO_INPUT_DMIC_T`：

```c
typedef struct {
    TUYA_AUDIO_DMIC_PORT_E port;
    TUYA_AUDIO_DMIC_CHAN_E chan;
} TAL_AUDIO_INPUT_DMIC_T;
```

- `port`：DMIC 端口
- `chan`：声道选择，与 ADC 类似分为单声道和双声道

I2S 配置 `TAL_AUDIO_INPUT_I2S_T`：

```c
typedef struct {
    TUYA_I2S_NUM_E              port;
    TUYA_I2S_MODE_E             mode;
    TUYA_I2S_COMM_FORMAT_E      i2s_protocol;
    TUYA_I2S_CHANNEL_FMT_E      data_format;
    BOOL_T                      use_dma;
} TAL_AUDIO_INPUT_I2S_T;
```

- `port`：I2S 端口号
- `mode`：主从模式（`TUYA_I2S_MODE_MASTER` / `TUYA_I2S_MODE_SLAVE`），需与 `TUYA_I2S_MODE_RX` 按位或组合
- `i2s_protocol`：I2S 通信协议格式
- `data_format`：声道格式，决定单声道或双声道及声道分配，目前T5平台仅支持双声道/16位深
- `use_dma`：是否使用 DMA 传输

### 1.3 输入接口列表

#### `tal_audio_input_init`

用途：初始化输入设备并返回句柄。

说明：

- 入参非法时返回 `NULL`
- 初始化时会创建设备控制块和内部资源
- 初始化成功后，句柄可用于后续 `set_volume/start/stop/deinit`

#### `tal_audio_input_set_volume`

用途：设置输入音量。

说明：

- 音量范围为 `0~100`
- `handle == NULL` 或音量越界时返回错误

#### `tal_audio_input_start`

用途：启动采集。

说明：

- 启动成功后内部 `started` 状态被置为 `TRUE`
- 只有启动后，底层采集到的数据才会继续转发给业务回调

#### `tal_audio_input_stop`

用途：停止采集。

说明：

- 调用后内部 `started` 会先置为 `FALSE`
- 停止后新的音频帧不会再转发给业务层回调

#### `tal_audio_input_deinit`

用途：反初始化输入设备并释放句柄资源。

说明：

- 调用后句柄立即失效，不可再次使用
- 建议业务层在释放后主动将本地 `handle` 变量置为 `NULL`

### 1.4 输入回调说明

输入数据通过 `TAL_AUDIO_INPUT_CB` 回调上送，回调参数为帧描述结构体 `TAL_AUDIO_FRAME_T`：

```c
typedef struct {
    TUYA_AUDIO_FRAME_EVT_E event;
    UINT8_T    *buf;
    UINT32_T    len;
    UINT32_T    seq_no;
    SYS_TIME_T  time_stamp;
} TAL_AUDIO_FRAME_T;

typedef VOID_T (*TAL_AUDIO_INPUT_CB)(TAL_AUDIO_FRAME_T *frame, VOID_T *args);
```

字段说明：

- `event`：帧事件类型，标识数据来源设备（`TUYA_AUDIO_FRAME_EVENT_ADC_RX` 表示 ADC，`TUYA_AUDIO_FRAME_EVENT_DMIC_RX` 表示 DMIC）
- `buf`：当前帧 PCM 数据指针
- `len`：当前帧长度，单位为字节
- `seq_no`：帧序号，从 0 开始递增，每次 DMA 中断触发后加 1，用于多通道帧配对
- `time_stamp`：帧生成时刻的系统毫秒时间戳（`tkl_system_get_millisecond()`），用于初始偏移校准和异常监测

回调使用约束：

- 回调可能运行在底层硬件/中断相关上下文
- 回调函数必须尽快返回，不能做长时间阻塞操作
- 不建议在回调里直接执行复杂业务、耗时存储、网络发送
- 推荐在回调里只做数据拷贝或发消息，再交给业务线程处理
- `frame` 指针仅在回调执行期间有效，回调返回后不可继续引用

多通道帧同步说明：

当业务层同时初始化 ADC 和 DMIC 两路输入时，可利用 `seq_no` 进行帧配对。在两路使用相同时钟源、相同采样率/位深/帧长的前提下，`seq_no` 相同的帧对应同一个 DMA 周期，时间上高度重叠，可直接配对送入 AEC 等处理算法。`time_stamp` 可在首帧时记录两路的初始偏移量，供算法参考。

双通道数据排列格式：

当配置为双通道（stereo）时，`frame->buf` 中的 PCM 采样按 **交织（interleaved）** 方式排列，即左右声道采样交替存储：

```text
| L0 | R0 | L1 | R1 | L2 | R2 | ... | Ln | Rn |
```

- 每个采样的字节数由 `sample_bits` 决定（例如 16 bit = 2 字节）
- 以 16 bit 双通道为例，`frame->buf` 中每 4 字节为一组：前 2 字节是左声道采样，后 2 字节是右声道采样
- 单帧总字节数 = `sample_rate * (sample_bits / 8) * channel_num * frame_time_ms / 1000`
- 如需分离左右声道，按步长 `2 * (sample_bits / 8)` 取对应偏移即可

16 bit 双通道拆分示例：

```c
INT16_T *pcm = (INT16_T *)frame->buf;
UINT32_T sample_count = frame->len / sizeof(INT16_T);
for (UINT32_T i = 0; i < sample_count; i += 2) {
    INT16_T left  = pcm[i];
    INT16_T right = pcm[i + 1];
    /* process left / right */
}
```

### 1.5 输入侧推荐调用顺序

```text
tal_audio_input_init
    -> tal_audio_input_set_volume（可选）
    -> tal_audio_input_start
    -> 音频数据回调
    -> tal_audio_input_stop
    -> tal_audio_input_deinit
```

## 2. 输出接口说明

### 2.1 适用场景

音频输出接口用于把业务侧 PCM 数据写入内部缓冲区，再由内部工作线程持续喂给底层播放设备。

当前接口句柄类型：

```c
typedef VOID_T *TAL_AUDIO_OUTPUT_HANDLE;
```

### 2.2 配置结构

输出初始化使用 `TAL_AUDIO_OUTPUT_CFG_T`：

```c
typedef struct {
    TAL_AUDIO_OUTPUT_TYPE_E type;
    union {
        TAL_AUDIO_DAC_OUTPUT_CHAN_T   dac_config;
        TAL_AUDIO_OUTPUT_I2S_T        i2s_config;
    } dev;
    TUYA_AUDIO_SAMPLE_BITS_E sample_bits;
    UINT32_T sample_rate;
    UINT32_T frame_time_ms;
} TAL_AUDIO_OUTPUT_CFG_T;
```

关键参数说明：

- `type`：输出设备类型
- `dev`：对应设备配置（见下方各设备配置结构说明）
- `sample_bits`：采样位宽，当前实现会检查是否为 8/16/24/32 bit 枚举值
- `sample_rate`：采样率，不能为空
- `frame_time_ms`：单帧时长，不能为空

各设备配置结构：

DAC 配置 `TAL_AUDIO_DAC_OUTPUT_CHAN_T`：

```c
typedef struct {
    TUYA_AUDIO_DAC_PORT_E   port;
    UINT32_T                spk_num;
    TUYA_GPIO_NUM_E         pa_gpio;
    TUYA_GPIO_LEVEL_E       pa_active_level;
} TAL_AUDIO_DAC_OUTPUT_CHAN_T;
```

- `port`：DAC 端口
- `spk_num`：喇叭声道数，至少为 1，同时影响单帧大小计算
- `pa_gpio`：功放（PA）使能 GPIO 引脚号，初始化时自动拉高/拉低控制功放开关
- `pa_active_level`：功放使能有效电平（`TUYA_GPIO_LEVEL_HIGH` 或 `TUYA_GPIO_LEVEL_LOW`）

I2S 配置 `TAL_AUDIO_OUTPUT_I2S_T`：

```c
typedef struct {
    TUYA_I2S_NUM_E              port;
    TUYA_I2S_MODE_E             mode;
    TUYA_I2S_COMM_FORMAT_E      i2s_protocol;
    TUYA_I2S_CHANNEL_FMT_E      data_format;
} TAL_AUDIO_OUTPUT_I2S_T;
```

- `port`：I2S 端口号
- `mode`：主从模式（`TUYA_I2S_MODE_MASTER` / `TUYA_I2S_MODE_SLAVE`），需与 `TUYA_I2S_MODE_TX` 按位或组合
- `i2s_protocol`：I2S 通信协议格式
- `data_format`：声道格式，决定单声道或双声道及声道分配

输出实现说明：

- worker 线程的喂数路径覆盖 `TAL_AUDIO_OUTPUT_DAC` 和 `TAL_AUDIO_OUTPUT_I2S`
- 当 ring buffer 中不足一帧数据时，内部会自动补 `0x00` 静音数据，避免底层继续播放脏数据
- `frame_time_ms` 同时影响单帧大小、队列等待超时和设备重试节奏
- I2S 输出初始化时会强制开启 DMA（`i2s_dma_flags = 1`）

### 2.3 输出接口列表

#### `tal_audio_output_init`

用途：初始化输出设备，创建输出控制块和内部工作线程。

说明：

- 初始化成功后返回输出句柄
- 输出句柄内部持有工作线程、消息队列、信号量和 ring buffer 等资源
- 初始化失败返回 `NULL`

#### `tal_audio_output_start`

用途：启动播放设备。

说明：

- 必须先调用该接口，之后才能调用 `tal_audio_output_write`
- 启动成功后内部 `started` 状态置为 `TRUE`

#### `tal_audio_output_write`

用途：写入一段 PCM 数据用于播放。

说明：

- `handle`、`buf` 不能为空，`len` 不能为 `0`
- 如果当前未启动，接口会直接返回错误
- 此接口会把写请求投递到内部工作线程
- 此接口是阻塞接口，直到本次数据全部写入内部 ring buffer 才返回
- 如果等待期间其他线程调用了 `stop` 或 `deinit`，本次写入可能被提前唤醒并返回错误

#### `tal_audio_output_set_volume`

用途：设置输出音量。

说明：

- 音量范围为 `0~100`
- `handle == NULL` 或音量越界时返回错误

#### `tal_audio_output_stop`

用途：停止播放。

说明：

- 会停止底层设备
- 会清空内部 ring buffer
- 会中止仍在等待 ring buffer 空间的写请求

#### `tal_audio_output_deinit`

用途：反初始化输出设备并释放所有资源。

说明：

- 会先执行设备停止和反初始化
- 再通知内部工作线程退出，并等待线程退出完成
- 调用完成后句柄失效，不可再次使用
- 建议业务层在释放后主动将本地 `handle` 变量置为 `NULL`

### 2.4 输出侧推荐调用顺序

```text
tal_audio_output_init
    -> tal_audio_output_set_volume（可选）
    -> tal_audio_output_start
    -> tal_audio_output_write（可重复调用）
    -> tal_audio_output_stop
    -> tal_audio_output_deinit
```

## 3. 多线程使用约束

### 3.1 必须对同一个 handle 做业务侧加锁保护

虽然输入实现内部自带了互斥锁，但从整体接口使用上看，`handle` 仍然应该由业务层按“单拥有者”方式管理；输出实现中 `start/stop/set_volume/write/deinit` 之间也没有统一的外部串行保护。因此：

- 同一个 `handle` 不要被多个线程无保护地同时调用
- 如果多个线程可能访问同一个 `handle`，业务层必须自行加锁
- 推荐为每个输入/输出 `handle` 配一把独立互斥锁
- 所有针对同一 `handle` 的生命周期接口都应在同一把锁下串行执行

建议保护的接口包括：

- 输入侧：`tal_audio_input_set_volume`、`tal_audio_input_start`、`tal_audio_input_stop`、`tal_audio_input_deinit`
- 输出侧：`tal_audio_output_start`、`tal_audio_output_write`、`tal_audio_output_set_volume`、`tal_audio_output_stop`、`tal_audio_output_deinit`

### 3.2 为什么需要业务侧加锁

如果不加锁，常见风险包括：

- 线程 A 正在 `tal_audio_output_write` 阻塞等待，线程 B 同时 `stop/deinit`，会导致本次写入异常返回
- 一个线程刚 `deinit` 完成，另一个线程仍继续使用旧 `handle`，会产生非法访问风险
- 多个线程交叉调用 `start/stop`，会让设备状态和业务状态不同步

### 3.3 推荐做法

- 为每个 `handle` 维护一个状态结构体，例如“句柄 + 互斥锁 + 是否已初始化”
- 先加锁，再判断 `handle` 和状态是否有效，然后调用 TAL 接口
- `deinit` 成功后立即把业务侧保存的 `handle` 清空
- 不要在一个线程里 `deinit`，同时让另一个线程继续 `write/start/stop`

示例伪代码：

```c
typedef struct {
    TKL_MUTEX_HANDLE mutex;
    TAL_AUDIO_OUTPUT_HANDLE handle;
    BOOL_T inited;
} APP_AUDIO_OUT_CTX_T;

OPERATE_RET app_audio_out_write(APP_AUDIO_OUT_CTX_T *ctx, UINT8_T *buf, UINT32_T len)
{
    OPERATE_RET ret;

    tkl_mutex_lock(ctx->mutex);

    if ((ctx->inited == FALSE) || (ctx->handle == NULL)) {
        tkl_mutex_unlock(ctx->mutex);
        return OPRT_INVALID_PARM;
    }

    ret = tal_audio_output_write(ctx->handle, buf, len);

    tkl_mutex_unlock(ctx->mutex);
    return ret;
}
```

### 3.4 回调线程注意事项

输入回调 `audio_input_cb` 不建议直接去操作同一个输入 `handle`，尤其不要在回调里做以下动作：

- 直接调用 `tal_audio_input_stop`
- 直接调用 `tal_audio_input_deinit`
- 获取可能被其他业务线程长期持有的重锁

推荐方式：

- 回调里只做快速投递
- 由业务工作线程统一持锁后处理 `stop/deinit`

## 4. 使用建议

- 输入和输出的 `sample_rate`、`sample_bits`、声道配置应与底层设备能力一致
- `frame_time_ms` 不宜设置过大，否则会增加延迟
- 输出写入是阻塞行为，业务线程需要预留足够执行时间
- 如对实时性要求较高，建议把采集处理线程和播放写入线程与其他重负载任务隔离
- 任何异常路径下都应保证最终执行 `stop/deinit`，避免资源泄漏

## 5. 测试接口

`src/test/test_audio_onboard.c` 中提供了板载 ADC/DAC/DMIC 联调测试入口，`src/test/test_audio_i2s.c` 中提供了 I2S 联调测试入口：

### 5.1 板载 ADC 录音测试

入口函数：`tal_audio_onboard_mic_test`

行为：

- 从板载 ADC Mic 采集音频
- 通过 ring buffer 缓冲后写入 SD 卡
- 默认输出文件为 `/sdcard/tal_audio_input_adc.pcm`

默认测试参数：

- 采样率：`16000`
- 位宽：`16 bit`
- 声道数：`2`
- 帧长：`20 ms`
- 录制时长：`20 s`

### 5.2 板载 DAC 播放测试

入口函数：`tal_audio_onboard_spk_test`

行为：

- 默认从 `/sdcard/dingdong_zh.pcm` 读取 PCM 数据（可通过 `TAL_AUDIO_TEST_SPK_FILE` 宏切换为生成信号模式）
- 通过 `tal_audio_output_write` 写入播放链路
- 用于验证 DAC 输出和喇叭链路

### 5.3 ADC + DMIC 双路同步测试

入口函数：`tal_audio_digital_dual_mic_input_sync_test`

行为：

- 同时启动 ADC 和 DMIC 两路输入
- 通过 `seq_no` 配对双路音频帧
- 分别输出参考信号和 DMIC 信号到文件，便于做 AEC / 对齐分析
- ADC 参考信号写入 `/sdcard/sync_adc_ref.pcm`，DMIC 拾音信号写入 `/sdcard/sync_dmic_pickup.pcm`

使用该测试时建议重点关注：

- 两路采样率、位宽、帧长必须一致
- 配对逻辑依赖 `seq_no`
- DMIC 路默认会做简单高通处理（截止频率 60 Hz）和增益放大（默认 6 倍）

### 5.4 I2S 输入/输出测试

入口函数：`tal_audio_i2s_test(port, is_master, direction)`

行为：

- 支持按端口号、主从模式、方向（RX/TX/双向）启动 I2S 测试
- `direction` 参数：`0` = 仅 RX，`1` = 仅 TX，`2` = RX + TX 同时启动
- RX 模式：通过回调接收 I2S 数据，可选转发到板载 DAC 播放
- TX 模式：生成 PCM 数据或从文件读取后通过 I2S 发送
- 支持多端口多实例同时运行

默认测试参数：

- 采样率：`16000`
- 位宽：`16 bit`
- 声道格式：双声道（`TUYA_I2S_CHANNEL_FMT_RIGHT_LEFT`）
- 帧长：`20 ms`
- I2S 协议：标准 I2S（`I2S_COMM_FORMAT_STAND_I2S`）

## 6. 总结

`tal_audio_input` 适合做音频采集，核心是"初始化后启动，通过回调收帧"；`tal_audio_output` 适合做 PCM 播放，核心是"初始化后启动，通过阻塞写接口送数"。当前已实现的设备类型包括输入侧的 ADC、DMIC、I2S 和输出侧的 DAC、I2S，UAV 类型仅预留。在多线程场景下，业务层必须把同一个 `handle` 作为共享资源统一加锁管理，避免 `write/start/stop/deinit` 并发调用导致状态竞争和句柄失效问题。

## 7. 音频机制切换（平台机制 / 涂鸦机制）

`tal_audio` 提供两种音频采集处理机制，可根据应用需求在编译时选择。

### 7.1 两种机制对比

| 维度 | 涂鸦机制（wukong pipeline） | 平台机制（默认） |
| --- | --- | --- |
| Kconfig 选项 | `ENABLE_WUKONG_AUDIO_PIPELINE=y` | `ENABLE_WUKONG_AUDIO_PIPELINE=n` |
| 芯片适配 | T5（Kconfig `depends on TUYA_MODULE_T5`） | T5（tal_audio 为 T5 板载音频专属；T1/T2 等平台音频为外挂方案走 UART 链路，不经 tal_audio） |
| 采集驱动 | `tkl_aud_adc`（新驱动） | vendor 旧链路（如 T5 `bk_voice`，经 `tkl_ai_*`） |
| AEC/AFE 位置 | 应用层（wukong process task） | 驱动内（`tkl_ai_set_vad_aec_algorithm` 注册） |
| 输入回调帧语义 | 原始双声道 LR 交织 | AFE 处理后单声道 |
| 支持的输入类型 | ADC / DMIC / I2S | 仅 ADC（board） |
| 支持的输出类型 | DAC / I2S | 仅 DAC（board） |
| output stop 语义 | 真停（中断阻塞 `write`） | BOARD no-op 透传（写通路常开） |
| output 生命周期 | 独立句柄 | 寄生于 input（`tkl_ai_init` 实底初始化，input 未 init 前 `write` 返回 `RESOURCE_NOT_READY`） |

### 7.2 机制切换方法

通过 Kconfig 菜单选择机制：

```bash
make app_menuconfig APP_NAME=tuyaos_demo_wukong_ai
```

然后在菜单中选择：**Tal audio function Config** → 勾选/取消 **ENABLE_WUKONG_AUDIO_PIPELINE**（勾选=涂鸦机制/wukong pipeline，取消=平台机制）

确认后执行：

```bash
make app_config APP_NAME=tuyaos_demo_wukong_ai
make app APP_NAME=tuyaos_demo_wukong_ai
```

> **说明**：机制互斥靠 local.mk 源文件级选择（tal_audio 驱动层与 wukong 采集层各一处、同一开关）。
> 增量构建不会清除被排除实现的残留目标文件，且 ar 归档为增量更新（旧成员不随 .o 删除而剔除），
> 残留曾导致机制串台（平台模式下 640B mono 帧被残留 pipeline 拆半成 320B 丢弃）。
> **切换后无需手工清理**：app 顶层 local.mk 内置"配置变更自愈"——`tuya_app_config.h` 内容
> （md5 戳记，`output/*/.tuya_app_config.md5`）变化时自动清除本 app 目标产物与库归档并重编，
> 编译输出中可见 `[app-config-guard] ... purged` 提示。

### 7.3 平台机制的约束

1. **芯片范围**：tal_audio（含两种机制）为 T5 板载音频专属——板载采集 + 算法链路只在 T5 上跑；T1/T2 等平台的音频为外挂方案（UART 链路），不经 tal_audio（app local.mk 中 tal_audio 段在 TUYA_MODULE_T5 守卫内）
2. **与 SPRS 互斥**：SPRS 双麦前端（`USING_SPRS_AUDIO_FRONTEND`）必须 `ENABLE_WUKONG_AUDIO_PIPELINE=y`（Kconfig 默认联动 + `#error` 兜底）
3. **配置参数忽略**：
   - 输入配置中的 `port`、`chan`、`frame_time_ms` 被忽略
   - 输出配置中的 `port`、`spk_num`、`frame_time_ms` 被忽略
4. **定位**：平台机制主要用于音质/AEC 效果对比与回退，不是长期形态

### 7.4 实现文件索引

- **平台机制实现**：`src/platform_mode/`
  - `tal_audio_platform.h` — 平台机制头文件
  - `tal_audio_input_platform.c` — 平台机制输入实现
  - `tal_audio_output_platform.c` — 平台机制输出实现

- **涂鸦机制实现**：`src/` 与 `src/audio_devices/`
  - `tal_audio_input.c` — 涂鸦机制输入实现
  - `tal_audio_output.c` — 涂鸦机制输出实现
  - `src/audio_devices/tal_audio_adc.c`、`tal_audio_dmic.c`、`tal_audio_i2s.c` — 各类设备驱动（仅涂鸦机制使用）

两套实现按 `CONFIG_ENABLE_WUKONG_AUDIO_PIPELINE` 做源文件级互斥编译（公共符号同名二选一），选源发生在**使用方 app 的顶层 local.mk**（如 `apps/tuyaos_demo_wukong_ai/local.mk` 的 tal_audio 段；本组件 local.mk 不参与 app 构建）：`=y` 编涂鸦机制——新驱动 `tal_audio_input.c`/`tal_audio_output.c` 及其设备层 `audio_devices/`；`=n` 编 `platform_mode/` 下两个同名接口实现（tkl 旧链路直通，不需要 `audio_devices/`）。涂鸦机制源文件零改动；平台机制两个 `.c` 内部另有 `#if` 宏守卫与 SPRS `#error` 作纵深兜底。
