## 概述

SC7A20(H) 三轴加速度计的 I2C 驱动 + 姿态(倾斜)检测任务，用于判断设备摆放/倾斜姿态（如桌面机器人
被扶正/倒置/侧倾）。`tuya_imu_i2c.c` 提供通用的 I2C 寄存器读写（`drv_read_regs` / `drv_write_regs`），
`tuya_imu.c` 在此基础上完成 SC7A20 寄存器配置、GPIO 中断唤醒、独立线程轮询姿态。当前仅被
`T5AI_BOARD_DESKTOP` 板级代码使用（`tuya_device_board.c` 调用 `tuya_imu_init`）。板级代码在初始化
时调用 `tuya_imu_init(i2c_num, gpio_num)`，姿态变化目前只通过内部线程打日志，没有对外回调，如需
上报给业务层需自行扩展。

## 设计要点与坑

- 中断 + 超时轮询结合：SC7A20 的 INT1 引脚下降沿触发 GPIO 中断，`imu_irq_notify` 只做一次
  semaphore post；真正的寄存器读取和姿态判断在独立线程 `imu_status_check_task` 里做，信号量等待
  超时固定 1000ms，即便没有中断也会每秒兜底读一次加速度值。
- 坑：I2C 端口号被硬编码。`tuya_imu_init(TUYA_I2C_NUM_E i2c_num, ...)` 接收 `i2c_num` 参数并用它
  做 `tkl_i2c_init`，但 `imu_read_reg` / `imu_write_reg` 内部固定使用宏 `IMU_I2C_ID`
  （`TUYA_I2C_NUM_0`），并没有使用传入的 `i2c_num`。如果某块板子用非 I2C0 的端口调用
  `tuya_imu_init`，实际的寄存器读写仍然走 I2C0——初始化/读数据失败但参数看起来是对的，排查时容易
  被参数误导。
- 数据格式：`ty_drv_sc7a20_read_accel` 读回 6 字节原始寄存器值后对 `int16` 做 `>>=6`，这是按
  SC7A20 10bit 有效位、数据左对齐的格式换算，不要当成通用移位逻辑照搬到其他型号 IMU。
- `drv_sc7a20h_cfg` 里 `#if 1 / #else` 保留了一份带点击(click)检测配置的历史版本，目前是死代码，
  只做参考，不要同时维护两份配置。
- 阈值 `SC7A20_TILT_THR = 200` 及各轴倾斜方向判断是按当前场景（寄存器配置 ±2g 量程）调的经验值，
  换板子或换灵敏度需求要重新标定。

## 改动指南

- 调整倾斜灵敏度/量程：改 `tuya_imu.h` 里的 `SC7A20_TILT_THR`、`drv_sc7a20h_cfg` 中的
  `SC_CTRL_REG4`（量程配置）。
- 换 I2C 端口：先修复"I2C 端口硬编码"的坑（把 `IMU_I2C_ID` 替换为运行时保存的 `i2c_num`），否则
  修改 `tuya_imu_init` 传参不会生效。
- 接入新板子：在对应 `boards/<BOARD>/tuya_device_board.c` 里调用 `tuya_imu_init`，并确认该板
  GPIO 中断引脚、I2C 引脚已在 pinmux 上配置好。

## 支持

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
