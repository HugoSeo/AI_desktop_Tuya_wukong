# Wukong Storage 层

`src/wukong/storage/` 是 wukong 唯一的存储模块，分两层：

- **介质层**：管理**外部大容量存储介质**（SD 卡 / 外挂 QSPI Flash）——编译期选定唯一介质、
  全 app 唯一一次挂载（开机异步、不挡启动）、导出统一根路径宏。它**不包装读写**——大块
  数据（录音/相册/本地音乐等）的使用者拿 `WUKONG_STORAGE_ROOT` 拼路径后直接用
  `tkl_fs`/`tal_fs` 访问。
- **记录层**（`wukong_storage_record.c`）：`(ns, name)` 两级键的小记录持久化
  （`wukong_storage_{write,read,delete,free}()`），当前使用方是 `tm`（闹钟/提醒）与
  `music/cloud`（云端播放列表）。数据落在 `<WUKONG_STORAGE_ROOT>/tuyaos/<ns>/<name>`，
  写入原子（详见下文契约）。

SDK 内置 KV（`tuya_ws_db`）与本层无关。

## 文件一览

| 文件 | 作用 |
| --- | --- |
| `wukong_storage.h` | 唯一公共头：`WUKONG_STORAGE_ENABLE` / `WUKONG_STORAGE_ROOT` 宏、介质层四个函数、就绪事件名、记录层四个函数 |
| `wukong_storage.c` | 介质层：异步挂载状态机；NONE 后端下整体退化为惰性桩 |
| `wukong_storage_record.c` | 记录层：`(ns, name)` 小记录读写删（`tkl_fs` 之上）；NONE 后端下退化为惰性桩 |
| `Kconfig` | 后端 choice（SDCARD / EXT_FLASH / NONE） |
| `tests/`、`../tests_common/` | 记录层 host 单测（pytest 驱动，见文末） |

## 介质层 API

```c
OPERATE_RET wukong_storage_init(VOID);    /* 异步发起挂载，立即返回；NONE 下空实现返 OK */
BOOL_T      wukong_storage_ready(VOID);   /* 卷是否已挂载可用；NONE 恒 FALSE */
CONST CHAR_T *wukong_storage_root(VOID);  /* 运行期取根路径；NONE 返 NULL */
OPERATE_RET wukong_storage_umount(VOID);  /* 预留：热插拔 / 格式化前使用 */
```

`wukong_storage_init()` 由 `tuya_app_main.c` 的 `user_main()` 在开机早期调用一次，把挂载
任务丢到 `WORKQ_SYSTEM` 就返回；无卡时约 2.5s 的 mount 超时发生在工作队列线程，不阻塞启动。

## 记录层 API 与契约

```c
OPERATE_RET wukong_storage_write(CONST CHAR_T *ns, CONST CHAR_T *name, CONST BYTE_T *data, UINT_T len);
OPERATE_RET wukong_storage_read(CONST CHAR_T *ns, CONST CHAR_T *name, BYTE_T **data, UINT_T *len);
OPERATE_RET wukong_storage_delete(CONST CHAR_T *ns, CONST CHAR_T *name);
VOID        wukong_storage_free(BYTE_T *data);
```

### 1. 路径规则

数据落在 `<WUKONG_STORAGE_ROOT>/tuyaos/<ns>/<name>`（SD 板即 `/sdcard/...`），例如
`/sdcard/tuyaos/tm/wk_tm_alarms`、`/sdcard/tuyaos/music/cloud/playlist.json`——后者与
历史 ui_fs 实现的路径字节兼容，OTA 后旧存档直接可读（`ns` 里的 `/` 就是普通目录层级）。
**没有 init 函数**：ns 目录在首次 write 时懒建（`tkl_fs_mkdir_r`）。

### 2. "不存在" 的语义：区分 OPRT_NOT_FOUND 与真实错误

- `wukong_storage_read()`：条目不存在（或介质不可用）一律返回 `OPRT_NOT_FOUND`，同时
  `*data` 置 `NULL`、`*len` 置 `0`——调用方应将其当作"空表"处理，而不是报错；真正的
  I/O 错误返回其他非 `OPRT_OK` 的错误码。
- `wukong_storage_delete()` 对已经不存在的条目仍返回 `OPRT_OK`——delete 是 best-effort
  且幂等的。
- `wukong_storage_write()` 是覆盖语义（同一 `(ns,name)` 第二次写入替换而不是追加）。

### 3. read 返回的缓冲区：堆分配 + `wukong_storage_free` 释放 + 末尾补 `'\0'`

成功时 `*data` 指向一块堆缓冲区，大小为 `*len + 1` 字节：前 `*len` 字节是数据本身，
第 `*len` 个字节（下标 `[len]`）额外补 `'\0'`，方便把 JSON 之类的文本负载直接当 C 字符
串使用而不必再拷贝一次。这个多出来的 `'\0'` **不计入** `*len`。调用方必须且只能用
`wukong_storage_free()` 释放这块缓冲区（就是 `tal_free()`，对 `NULL` 安全）。

### 4. 原子写与 `.tmp` 恢复

写入走"临时文件 + rename"实现原子性（`<path>.tmp` → `<path>`）。FATFS 等不支持覆盖
rename 的文件系统会先 remove 再 rename——该降级路径本身存在非原子窗口，由读侧闭合：
rename 失败/掉电后完整数据仍留在 `.tmp`（写侧只在临时文件完整写入 + fsync 后才进入该
窗口，且刻意不删 `.tmp`），read 在正式文件缺失时自动从 `.tmp` 恢复并改名回正式名；
delete 会同时删除两个名字，避免已删条目被 `.tmp` 复活。

### 5. 挂载降级

记录层**自己不挂载任何设备**，每次调用只查询 `wukong_storage_ready()`：未 ready（挂载
还没完成、挂载失败、或后端是 NONE）时按固定规则降级——`write()` 返回错误、`read()`
返回 `OPRT_NOT_FOUND`、`delete()` 直接当作"本来就没有"返回 `OPRT_OK`，全程不崩溃。
挂载成功后，下一次任意调用自然看到 `ready == TRUE`，无需本层重试或感知挂载过程。
NONE 后端下四个函数是同一契约的惰性桩，无外部存储的板子照常链接。

## 编译期宏：`WUKONG_STORAGE_ROOT` 与 `WUKONG_STORAGE_ENABLE`

| 后端 | `WUKONG_STORAGE_ENABLE` | `WUKONG_STORAGE_ROOT` | 介质 |
| --- | --- | --- | --- |
| SDCARD | `1` | `"/sdcard"` | FatFs on SDIO（`DEV_SDCARD`） |
| EXT_FLASH | `1` | `"/ext-flash"` | littlefs on 外挂 QSPI flash（`DEV_EXT_FLASH`） |
| NONE | **未定义** | **未定义** | 无 |

挂载点按介质分别命名（不是统一叫 `/sdcard`），这样 SD 板的路径与历史固件完全一致，现网
旧录音 / 相册 / `playlist.json` / `tuyaos/tm/` 全部字节兼容；ext-flash 是全新路径，本来
也没有旧数据。

NONE 时两个宏**都不定义**，依赖外部存储的模块用 `#if` 整块裁剪，不入固件、不占 flash：

```c
#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)
    /* 编译期拼路径是主流用法 */
    tkl_fopen(WUKONG_STORAGE_ROOT "/tuyaos/recording/index.bin", "rb");
#endif
```

需要运行期取路径（例如一段代码要在 NONE 板上照常编译）时用 `wukong_storage_root()`，
NONE 下它返回 `NULL`，调用方自行处理——`ui_page_files.c` 是这种写法的参照。

## 就绪事件：`EVENT_WUKONG_STORAGE_READY`

挂载成功后发布一次（事件名 `"storage.ready"`，13 字符——base_event 的
`EVENT_NAME_MAX_LEN` 是 16，起名别超）。**base_event 的回调在发布者线程同步执行**，而发
布者正是 `WORKQ_SYSTEM` 上的挂载任务，所以订阅回调天然跑在那一个 worker 上。

开机时有读盘需求的模块（一次性加载索引 / 列表 / 配置）必须等这个事件，标准写法是
**先订阅、再 catch-up**：

```c
STATIC BOOL_T s_loaded = FALSE;

STATIC OPERATE_RET __on_storage_ready(VOID *data)   /* 跑在 WORKQ_SYSTEM */
{
    if (s_loaded) return OPRT_OK;   /* 幂等：事件与 catch-up 两条路都可能到 */
    do_load();
    s_loaded = TRUE;
    return OPRT_OK;
}

STATIC VOID __catchup_on_workq(VOID *data) { __on_storage_ready(NULL); }

/* init 里 */
ty_subscribe_event(EVENT_WUKONG_STORAGE_READY, "mymod", __on_storage_ready, SUBSCRIBE_TYPE_NORMAL);
if (wukong_storage_ready()) {                      /* 订阅晚于挂载完成的竞态 */
    tal_workq_schedule(WORKQ_SYSTEM, __catchup_on_workq, NULL);
}
```

catch-up **必须经 workq 转一道**，不能直接调：直接调会让加载跑在 init 的调用线程上，与
事件回调所在的 worker 线程并发，那个裸 `BOOL_T` 幂等标志就挡不住了。现成参照：
`ui_svc_fs.c`、`wukong_picture.c`、`ui_svc_recording.c`、`wukong_playback_ctrl.c`、
`wukong_tm_alarm.c`。

**经记录层持久化的模块**：NONE 板上 `storage.ready` 永不发布、也没有任何东西被持久化，
等下去就是永不加载——所以延迟加载只在有介质时启用，gate 就是介质存在与否：

```c
#if defined(WUKONG_STORAGE_ENABLE) && (WUKONG_STORAGE_ENABLE == 1)
```

NONE 板（以及不定义任何存储宏的 host 单测）走立即加载：读回 `OPRT_NOT_FOUND`、从空表
启动，无害。现成参照：`wukong_tm_internal.h` 的 `WUKONG_TM_STORE_LOAD_DEFERRED` 与
`wukong_playback_ctrl.c` 的 `PLAYBACK_STORAGE_LOAD_DEFERRED`。

## 目录归属：介质层不建任何目录

介质层只负责把卷挂起来。`<ROOT>/tuyaos/` 标准树是 `ui_svc_fs` 的资产、
`<ROOT>/tuyaos/<ns>/` 是记录层的资产（首次 write 时懒建）、`<ROOT>/<album_name>/` 是相册
的（历史遗留，在 `tuyaos/` 之外，为现网既有相册保兼容）。各自在收到 READY 事件后或首次
访问时自己 mkdir。

## 挂载失败与降级

挂载重试 3 次（间隔 1s）后放弃，本次开机不再重试（没插卡就是没插卡；热插拔不在本层范围，
`wukong_storage_umount()` 为将来做热插拔时预留）。失败后 `wukong_storage_ready()` 恒
FALSE，各消费方按各自既定方式降级：记录层写返错 / 读返 `OPRT_NOT_FOUND` / 删返 OK，
相册与录音按次失败，UI 相关页面显示为空。全程不崩。

**SD 后端绝不格式化**——卡是用户资产，挂不上就是挂不上。

## ext-flash 首次格式化钩子

vendor 的 `littlefs_mount()` 不带 auto-mkfs（自动格式化那段是 `#if 0`），所以出厂空白的
外挂 flash 第一次必然挂不上。本层为此声明了弱符号：

```c
OPERATE_RET __attribute__((weak)) wukong_storage_port_mkfs(VOID);   /* 默认返 OPRT_NOT_SUPPORTED */
```

EXT_FLASH 后端首次挂载失败时调用它，成功则重挂一次。T5 的强实现在
`src/boards/common/wukong_storage_port.c`（用 vendor 的 `mkfs()`，参照
`vendor/T5/.../test_littlefs.c` 与 `src/misc/fs_init.c`），只在选中 EXT_FLASH 时编译。
换平台时在自己的板级目录提供同名强实现即可，**不要改 vendor**。

> 当前 T5 配置是 NOR QSPI（`CONFIG_TUYA_USE_MTD` 未开）。若将来用 NAND，照 vendor
> `fs_init.c` 的 `CONFIG_TUYA_USE_MTD` 分支补 `tuya_mtd_device_query()` 那条路径。

## 给设备接入新介质 / 新板子

### 已支持介质，新板子要开 SD

**必须改两处**，缺一不可：

1. `Kconfig` 的 `default WUKONG_STORAGE_SDCARD if ...` 白名单加上板名——这只影响
   `make app_menuconfig` 的交互默认值。
2. **`build/appconfig/<BOARD>` 加一行 `CONFIG_WUKONG_STORAGE_SDCARD=y`** ——这才是决定
   产品行为的那一处。

第 2 步不能省：板级配置走 `conf2h.py` **逐行直译**，不执行 Kconfig 的依赖与默认值求值，
所以 Kconfig 里写的 `default ... if <BOARD>` 对 appconfig 编译路径**完全不生效**。漏了它
的症状很隐蔽——固件编得过、但 `WUKONG_STORAGE_ENABLE` 未定义，整个存储层（介质层、记录
层）连同 `ui_svc_fs` 被编成惰性桩，闹钟/播放列表静默停止持久化。

不打算开外部存储的板**不用加任何行**：两个宏都不定义即等价于 NONE。

### 新增一种介质（inner flash / U 盘 / …）

1. `Kconfig` 的 choice 加一项 `WUKONG_STORAGE_<X>`；
2. `wukong_storage.h` 的宏链加一段 `#elif`，给出该介质的 `WUKONG_STORAGE_ROOT`；
3. `wukong_storage.c` 的 `WUKONG_STORAGE_DEV` 加一段 `#elif` 映射到对应 `FS_DEV_TYPE_T`；
4. 需要首次格式化就实现板级 `wukong_storage_port_mkfs()`。

结构不用动。

## 测试

```bash
python3 -m pytest src/wukong/storage/ -q     # 只跑记录层套件
python3 -m pytest src/wukong/ -q             # 全部套件
```

记录层的 host 套件（`tests/`）测的是**真实实现**：`wukong_storage_record.c` 原样编译，
`tkl_fs` 由 `../tests_common/stub_tkl_fs.c` 1:1 映射到 POSIX，记录树根经
`WUKONG_STORAGE_HOST_TEST` 编译宏 + `wukong_storage_test_set_root()`（仅 host 测试用的
seam，固件永不定义该宏）指到临时目录。覆盖：写读回、`OPRT_NOT_FOUND` 与出参清空、
`'\0'` 补位、覆盖写、delete 幂等、介质不可用降级、原子写产物、`.tmp` 恢复路径，以及
NONE 惰性桩的链接与契约。tm 等功能套件不编记录层，用 `../tests_common/stubs_storage.c`
的 RAM double 顶替同名符号。

介质层没有 host 单测：核心就是一次 `tkl_fs_mount()` 加一个状态机，host 上没有可挂的介
质，桩测等于测桩。真机验证清单（SD 正常/拔卡、ext-flash 空白首启、NONE 板裁剪）见发版
流程。
