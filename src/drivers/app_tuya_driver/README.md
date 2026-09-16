## 概述

提供 GPIO / UART 的 TAL 层实现，以及触摸(touch)的 TKL 层封装。SDK 组件 `components/tal_driver` 在
os 模式（非 `CONFIG_OPERATING_SYSTEM=3`）下只编译 `tal_rtc.c` / `tal_watchdog.c` / `tal_flash.c`，
不包含 GPIO/UART；这里是 app 层对该空缺的补充实现，被顶层 `local.mk` 通过
`CONFIG_ENABLE_GPIO` / `CONFIG_ENABLE_UART` 两个开关选择性编译 `src/drivers/app_tuya_driver/src/os/tal_gpio.c` /
`src/drivers/app_tuya_driver/src/os/tal_uart.c`。`include/` 被加入顶层头文件搜索路径，调用方直接 `#include tal_gpio.h` /
`tal_uart.h` / `tkl_touch.h`，无需感知实现来自 app 层还是 SDK 组件层。

## 设计要点与坑

- `tal_gpio.c` 只是对 `tkl_gpio_*` 的直通转发，没有额外状态，不做加锁/去抖，并发安全由调用方自己保证。
- `tal_uart.c` 用一个全局单链表 `g_uart_list` 记录已 `init` 的端口，一把全局互斥锁保护链表增删查
  （查找是线性遍历，端口数量少可接受）；每个端口有独立的 rx 环形缓冲区 + 信号量，支持阻塞读
  （`open_mode & O_BLOCK`）。
- 坑：异步发送两处判断用的宏名不一致——`tal_uart_init` 用 `CONFIG_UART_ASYNC_WRITE` 决定是否创建
  `tx_ring` 并注册 tx 中断回调；而 `tal_uart_write` / `uart_async_write` 判断走异步分支用的是
  `CONFIG_UART_WRITE_ASYNC`。只打开其中一个宏时，异步发送路径实际不会按预期工作（`tx_ring` 建了
  用不上，或者走到异步分支时 `tx_ring` 未初始化）。改动异步写相关逻辑前先确认这两个宏的定义是否一致。
- `tkl_touch.c` 是对 `bk_touch_*` 的直通转发，强绑定 Beken 芯片的触摸外设，非 Beken 平台不要接入。

## 改动指南

- 新增/修改 GPIO、UART 行为：改 `src/drivers/app_tuya_driver/src/os/tal_gpio.c`、`src/drivers/app_tuya_driver/src/os/tal_uart.c`；是否参与编译由
  `local.mk` 里的 `CONFIG_ENABLE_GPIO` / `CONFIG_ENABLE_UART` 决定（menuconfig / app_config 打开
  对应开关）。
- 触摸相关改动：`src/drivers/app_tuya_driver/src/os/tkl_touch.c`，仅对 Beken 触摸外设生效。
- 若目标平台切到 non-os 模式或 SDK 组件 `tal_driver` 原生支持了 GPIO/UART，评估是否还需要继续
  维护这份 app 层实现，避免重复定义导致链接冲突。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
