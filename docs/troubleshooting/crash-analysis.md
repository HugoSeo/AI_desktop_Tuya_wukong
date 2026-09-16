# T5 崩溃日志定位入门

本指南面向第一次遇到 T5（Cortex-M33，芯片型号 BK7258）设备崩溃/重启、还不熟悉嵌入式崩溃定位流程的开发者，说明：崩溃日志长什么样、去哪里找 `.elf`/工具链、怎么把日志里的十六进制地址还原成源码文件和行号，以及几种最常见的崩溃模式该往哪个方向排查。

本文路径均以 **SDK 根目录**（`tuyaos-ai/`，即 `apps/tuyaos_demo_wukong_ai` 的上三级目录）为基准。

---

## 现象/日志形态

设备崩溃或触发 Fault 后，串口会打印一段现场信息，大致按以下顺序出现（该顺序及具体文案来自芯片底层 `vendor/T5/t5_os/cp/middleware/arch/cm33/trap_base.c` 与协议栈自带的 CmBacktrace 库 `vendor/T5/t5_os/cp/components/coredump/cm_backtrace/`）：

1. **故障类型提示**：如 `HardFault` / `MemFault` / `BusFault` / `UsageFault` / `Watchdog` / `SecureFault` / `DebugFault` 之一（见 `trap_base.c` 中的 `fault_type[]` 表）。
2. **寄存器现场**：`r0`~`r12`、`sp`、`lr`、`pc`、`xpsr`、`msp`、`psp`、`primask`、`basepri`、`faultmask`、`fpscr`，以及 `MMFAR`/`BFAR`/`CFSR`/`HFSR` 几个 Fault 状态寄存器（同样在 `trap_base.c` 中打印）。**其中 `pc`（程序计数器，崩溃发生时正在执行的指令地址）和 `lr`（链接寄存器，函数返回地址）是定位问题最直接的两个值。**
3. **CmBacktrace 现场描述**：固件/硬件/软件版本一行（`Firmware name: %s, hardware version: %s, software version: %s`）、故障发生在哪个线程（`Fault on thread %s`）或裸机/中断环境、以及对故障原因的文字解释，例如：
   - `Usage fault is caused by indicates that a stack overflow (hardware check) has taken place`
   - `Bus fault is caused by precise data access violation`
   - `The bus fault occurred address is %08x`
   - `Error: Thread stack(%08x) was overflow`
4. **调用栈地址列表 + 现成的 addr2line 提示行**，形如：

   ```text
   Show more call stack info by run: arm-none-eabi-addr2line -e <固件名>.elf -a -f 02139abc 02138ef0 02137a10 ...
   ```

> **注意**：本工程 CmBacktrace 的语言配置是英文（`vendor/T5/t5_os/cp/components/coredump/cm_backtrace/cmb_cfg.h` 中 `CMB_PRINT_LANGUAGE` 设为 `CMB_PRINT_LANGUAGE_ENGLISH`），所以真机日志里这些提示都是英文原文，不代表刷错了固件语言。

### 怎么抓日志

用串口工具连接开发板并保存输出到文本文件即可，例如 Tuya Wind IDE 自带的串口终端，或涂鸦官方串口工具 [tyuTool](https://github.com/tuya/tyutool/tree/master)；连接方式与串口号可参考 [快速开始](../quickstart.md) 「连接设备」一节（虚拟机场景一般为 `ttyACM0`）。抓日志时尽量从设备刚上电/复位开始录，避免只截到崩溃后半段，导致故障发生前的业务日志丢失。

---

## 准备工作（elf / 工具链路径）

定位地址需要两样东西：**崩溃时那个版本对应的 `.elf`** 和 **交叉工具链里的 `addr2line`**。

### 1. 构建产物（.elf / .map）

执行 `make app APP_NAME=tuyaos_demo_wukong_ai` 构建后，产物按版本号落在：

```text
apps/tuyaos_demo_wukong_ai/output/<版本号>/debug/<核心目录>/
├── app.elf     # 带符号的可执行文件，addr2line 要用这个，不是 .bin
├── app.map     # 链接器内存布局，可按地址/符号搜索
├── app.nm      # 符号表（已按 文件:行号 标注，可作为 addr2line 的辅助/备查）
└── ...
```

例如实测生成过 `apps/tuyaos_demo_wukong_ai/output/1.0.67/debug/bk7258_ap/app.elf` 和 `apps/tuyaos_demo_wukong_ai/output/1.0.67/debug/bk7258/app.elf`（T5/BK7258 是多核芯片，`<核心目录>` 会因工程实际的核心划分出现 `bk7258_ap`、`bk7258` 等不同名字）。

**版本号必须和崩溃设备上刷的固件严格对应**——用错版本的 `app.elf` 去解析地址，符号名和行号会完全对不上，且不会报错提示你选错了文件，这是最容易踩的坑。烧录用的 `tuyaos_demo_wukong_ai_QIO_<版本号>.bin` 与调试用的 `app.elf` 在同一次构建中一起产出，版本号保持一致即可配对。

### 2. 交叉工具链

`./prepare.sh` 会下载好 T5 平台的交叉工具链，`addr2line` 的实际路径是：

```text
vendor/T5/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line
```

可以先确认它存在并可执行：

```bash
vendor/T5/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line --version
```

---

## 定位步骤（可复制命令）

1. **确认版本与产物路径**，假设日志显示的固件版本是 `1.0.67`，且崩溃发生在 `bk7258_ap` 核心：

   ```bash
   ELF=apps/tuyaos_demo_wukong_ai/output/1.0.67/debug/bk7258_ap/app.elf
   ADDR2LINE=vendor/T5/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line
   ```

2. **直接复用日志里现成的命令**：崩溃日志末尾那行 `Show more call stack info by run: arm-none-eabi-addr2line -e <固件名>.elf -a -f <地址列表>` 已经把参数拼好了，把可执行文件换成上面确认过的完整路径、`-e` 后面换成本地 `$ELF`，其余原样照抄：

   ```bash
   $ADDR2LINE -e $ELF -a -f 02139abc 02138ef0 02137a10
   ```

   - `-a`：先打印传入的地址本身，方便和日志对照
   - `-f`：打印函数名（否则只有文件:行号）
   - 建议再加一个 `-p`（单地址单行输出，更易读，这个参数是提效技巧，不是日志原文要求）：

   ```bash
   $ADDR2LINE -e $ELF -a -f -p 02139abc 02138ef0 02137a10
   ```

3. **只想查 HardFault 现场的 PC / LR**（即寄存器 dump 里的 `pc` 和 `lr` 两个值）：

   ```bash
   $ADDR2LINE -e $ELF -a -f -p 0x<PC地址> 0x<LR地址>
   ```

   PC 是崩溃瞬间正在执行的指令，LR 是调用它的返回地址——两个都定位一下，能立刻知道"死在哪个函数"以及"是谁调用到这里的"。

4. **解析结果是 `?? ??:0`（找不到符号）时**，按下面顺序排查：
   - 先确认 `$ELF` 的版本号是否真的和崩溃固件一致（最常见原因）；
   - 打开同目录下的 `app.map` 或 `app.nm`，按地址前缀搜索附近的符号，`app.nm` 里每个符号已经带了 `文件:行号` 标注，可以人工找到地址落在哪个函数范围内；
   - 地址本身就是非法值（如 `0x00000000` 附近）本来就无法解析出符号，这本身就是一个有意义的定位信息（见下面"PC=0"模式）。

---

## 常见崩溃模式

| 现象特征 | 可能原因 | 定位线索 |
|---|---|---|
| `pc`（或 addr2line 解出的调用栈顶）落在 `0x0` 附近或明显不在代码段范围 | 通过空/悬空函数指针发起调用（回调注册前被调用、对象释放后仍被调用等） | PC 本身往往解析不出符号；改从 `lr`/调用栈里更靠外层的地址用 addr2line 定位"是谁发起了这次调用"，重点排查该处的函数指针/回调是否已被置空或提前释放 |
| 日志出现 `Error: Thread stack(%08x) was overflow`，或 UsageFault 提示 `... stack overflow (hardware check) has taken place` | 某线程栈空间开小了、递归过深、栈上分配了较大的局部数组/结构体 | 结合 `Fault on thread %s` 确认是哪个线程，检查该线程创建时配置的栈大小；必要时调大栈或把大对象改为堆分配 |
| BusFault/MemFault 提示访问越界或未对齐，如 `Bus fault is caused by precise/imprecise data access violation`、`... an unaligned access fault`，并打印出 `The bus fault occurred address is %08x` / `The memory management fault occurred address is %08x` | 野指针解引用、数组越界读写、结构体强转指针类型后未对齐访问 | 对照打印出的 `BFAR`/`MMAR` 地址判断范围是否合理（是否落在明显异常的地址上），再结合调用栈定位到具体的读写语句 |
| 设备直接复位、串口只打出一次 `Watchdog` 提示，看不到完整的寄存器/调用栈现场 | 某任务长时间未执行到喂狗点，通常是死循环、无限等待一个不会触发的信号量/队列、或中断里做了过长时间的处理 | 因为复位前的 PC/LR 现场往往拿不到，主要靠看复位前最后一段业务日志判断卡在哪一步；如果能复现，可先怀疑最近改动过的、涉及阻塞等待的代码路径 |

> 以上是几类最常见、最典型的模式；实际项目里的崩溃成因五花八门，遇到和上表都对不上号的现象是正常的，不要生搬硬套。

---

## 支持

如果照上面的步骤走完，仍然无法确定崩溃原因，建议整理好以下信息再去论坛提问，能大幅提高被解答的效率：完整的崩溃日志（含寄存器 dump 和调用栈地址）、固件版本号、addr2line 解析出的调用栈（哪怕只解出了一部分）、以及最近做过的代码改动。

在开发过程遇到问题，可以到 TuyaOS 开发者论坛 [联网单品开发版块](https://www.tuyaos.com/viewforum.php?f=11) 发帖咨询。
