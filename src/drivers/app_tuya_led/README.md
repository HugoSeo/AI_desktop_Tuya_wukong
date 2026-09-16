## 概述

LED 输出控制（常亮/常灭/闪烁）的轻量多路管理服务，被 `tuya_ai_toy_led.c` 用作设备状态指示灯。每
路 LED 用 `tuya_create_led_handle` 注册一个 GPIO 输出得到 handle，之后用
`tuya_set_led_light_type` 切换该 handle 的输出状态（常高/常低/两种起始极性的闪烁）。所有 handle
共用一个 100ms 周期的全局软件定时器驱动闪烁状态机；业务层只需持有 handle 调用
`tuya_set_led_light_type`，无需关心定时器细节。

## 设计要点与坑

- 单定时器驱动多路 LED：只有一个全局 100ms 周期定时器（`LED_TIMER_VAL_MS`），链表遍历所有注册的
  LED 逐个推进闪烁状态，避免每路 LED 各起一个定时器；代价是闪烁周期（`flash_cycle`）和总闪烁时长
  （`flh_ms_sumtime`）都以 100ms 为粒度，设小于 100ms 的值没有意义。
- `flh_ms_sumtime == 0xFFFF` 表示"一直闪烁"，其他值表示总闪烁时长（ms），到期后自动切回闪烁前的
  `OL_LOW` / `OL_HIGH` 电平。
- 坑：没有销毁/反注册接口。`tuya_create_led_handle` 分配的节点会一直挂在 `led_cntl_list` 上，没有
  对应的 free/移除函数；这份实现假定 LED 是板级常驻资源（数量少、生命周期等于设备运行期），不要在
  运行时频繁创建临时 LED handle，否则会持续泄漏。
- `tuya_create_led_handle_select` 在头文件里声明并导出，但源码实现整体被 `#if 0` 注释掉，是死代码/
  历史遗留，当前调用会链接不到符号；改动前先确认没人在依赖它。
- 全局 `mutex` 同时保护链表结构和每个 `LED_CNTL_S` 的字段读写，`__led_timer_cb`（定时器线程）和
  `tuya_set_led_light_type`（业务线程调用）共用同一把锁，二者互斥，没有其他并发写者。

## 改动指南

- 新增一路 LED：调用 `tuya_create_led_handle(pin, 默认电平, &handle)`，通常在板级初始化里做一次。
- 修改闪烁频率/时长：调用
  `tuya_set_led_light_type(handle, OL_FLASH_LOW/OL_FLASH_HIGH, 闪烁周期ms, 总时长ms)`，注意两个
  时间参数都会被按 100ms 粒度取整。
- 如需销毁/反注册能力：当前实现没有，需新增从链表摘除 + free 的逻辑，改动时留意
  `__led_timer_cb` 正在遍历链表的并发场景。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
