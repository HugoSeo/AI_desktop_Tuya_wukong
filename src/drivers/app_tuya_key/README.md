## 概述

按键检测与事件上报（单击/连击/长按/释放）的通用服务，对 GPIO 输入做软件去抖与计时，非某个具体
板子专属，被 `tuya_ai_toy_key.c`、`tuya_ai_toy.c`、`mftest`/`codec_bench` 等多处业务代码使用，是
按键最底层的服务。`key_init` 用固定表（`KEY_USER_DEF_S` 数组）一次性注册按键；`reg_proc_key` 支持
运行时动态追加/更新按键（走独立链表）。业务方填好 `KEY_USER_DEF_S`（GPIO、触发电平、长按类型、
长按/连击时间窗）和回调后调用一次 `key_init` 完成接入，后续按键事件从回调
`KEY_CALLBACK(port, type, cnt)` 收到。

## 设计要点与坑

- ISR 只做唤醒：GPIO 中断 `__gpio_for_key_irq` 只 post 一个信号量，不做任何去抖/状态机逻辑；真正
  的去抖状态机（`__key_ent_proc`：`KEY_DOWN → KEY_DOWN_CONFIRM → KEY_DOWNNING → KEY_UP_CONFIRM →
  KEY_UPING → KEY_FINISH`）全部跑在独立线程 `key_handle_thrd` 里，按 `timer_space`（默认 20ms，
  最大 100ms）周期轮询所有按键（固定表 + 动态注册链表都会遍历）。
- 低功耗联动：按键线程收到中断后会调用 `tal_cpu_lp_disable()` 关闭 tickless 低功耗，进入 20ms
  周期轮询；只有连续 `keep_time_val`（默认约 10.5s）无新中断后，tickless 定时器回调才会重新打开
  低功耗。也就是说频繁按键会让 CPU 持续停留在非 tickless 模式——这是为了保证去抖计时精度，代价是
  功耗，可通过 `key_set_keep_time` 调整该阈值。
- 长按类型 `KEY_LONG_PRESS_TP_E` 有 5 种互斥语义（一次性长按触发 / 超过阈值后每 300ms 连续触发 /
  按下立即触发普通或长按），接入新按键时要选对类型，不要混用假设。
- 坑：`key_init` 只在首次调用时生效——如果 `key_mag` 已存在会直接返回 `OPRT_OK`，不会用新传入的
  `p_tbl` 覆盖，容易误以为"重新 init"生效了。
- 固定表（`p_tbl`）和动态注册（`reg_proc_key` 产生的链表）是两套并行的按键集合，`__key_handle`
  每次轮询都会各遍历一次；除存储结构外行为一致。

## 改动指南

- 新增/修改按键行为：业务层构造 `KEY_USER_DEF_S` 并调用 `key_init`（启动阶段的固定按键）或
  `reg_proc_key`（运行期动态按键，参考 `tuya_ai_toy_key.c` 的用法）。
- 调整去抖/连击/长按时间：改对应 `KEY_USER_DEF_S` 里的 `long_key_time` / `seq_key_detect_time`，
  或全局轮询周期 `timer_space`（`key_init` 第三个参数，需 ≤ 100ms）。
- 调整低功耗策略：`key_set_keep_time` / `key_get_keep_time`。
- 状态机改动集中在 `src/drivers/app_tuya_key/src/tuya_key.c` 的 `__key_ent_proc`，改之前建议先画一遍现有状态转换，避免
  破坏连击/长按互斥逻辑。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
