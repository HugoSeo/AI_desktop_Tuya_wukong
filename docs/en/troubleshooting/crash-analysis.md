<!-- Auto-translated from ../../troubleshooting/crash-analysis.md. Do not edit manually. -->
# T5 Crash Log Analysis Primer

This guide is for developers who are dealing with a T5 (Cortex-M33, chip model BK7258) device crash/reboot for the first time and aren't yet familiar with the embedded crash-analysis workflow. It explains: what a crash log looks like, where to find the `.elf`/toolchain, how to turn the hex addresses in the log back into source file names and line numbers, and which direction to investigate for the most common crash patterns.

All paths in this document are relative to the **SDK root** (`tuyaos-ai/`, i.e. three directories above `apps/tuyaos_demo_wukong_ai`).

---

## What It Looks Like

When the device crashes or a fault is triggered, the serial port prints a block of state information, roughly in the following order (this order and the exact wording come from the chip's low-level `vendor/T5/t5_os/cp/middleware/arch/cm33/trap_base.c` and the stack's bundled CmBacktrace library `vendor/T5/t5_os/cp/components/coredump/cm_backtrace/`):

1. **Fault type**: one of `HardFault` / `MemFault` / `BusFault` / `UsageFault` / `Watchdog` / `SecureFault` / `DebugFault` (see the `fault_type[]` table in `trap_base.c`).
2. **Register dump**: `r0`–`r12`, `sp`, `lr`, `pc`, `xpsr`, `msp`, `psp`, `primask`, `basepri`, `faultmask`, `fpscr`, plus the fault status registers `MMFAR`/`BFAR`/`CFSR`/`HFSR` (also printed in `trap_base.c`). **Of these, `pc` (program counter — the instruction address being executed when the crash occurred) and `lr` (link register — the return address of the calling function) are the two most directly useful values for locating the problem.**
3. **CmBacktrace context description**: a firmware/hardware/software version line (`Firmware name: %s, hardware version: %s, software version: %s`), which thread the fault occurred on (`Fault on thread %s`) or whether it was bare-metal/interrupt context, and a text explanation of the likely cause, for example:
   - `Usage fault is caused by indicates that a stack overflow (hardware check) has taken place`
   - `Bus fault is caused by precise data access violation`
   - `The bus fault occurred address is %08x`
   - `Error: Thread stack(%08x) was overflow`
4. **A call-stack address list plus a ready-made addr2line hint line**, looking like:

   ```text
   Show more call stack info by run: arm-none-eabi-addr2line -e <firmware name>.elf -a -f 02139abc 02138ef0 02137a10 ...
   ```

> **Note**: this project's CmBacktrace is configured for English output (`CMB_PRINT_LANGUAGE` is set to `CMB_PRINT_LANGUAGE_ENGLISH` in `vendor/T5/t5_os/cp/components/coredump/cm_backtrace/cmb_cfg.h`), so these messages appear in English on real hardware regardless — it does not mean the wrong firmware language was flashed.

### Capturing the Log

Connect to the dev board with a serial tool and save the output to a text file — for example, the Tuya Wind IDE's built-in serial terminal, or Tuya's official serial tool [tyuTool](https://github.com/tuya/tyutool/tree/master); see the "Connecting the Device" section of the [Quick Start](../quickstart.md) for connection details and the port name (typically `ttyACM0` in a VM). Try to capture the log starting right from power-up/reset rather than only the tail end after the crash, so you don't lose the business logs leading up to the fault.

---

## Preparation (elf / toolchain paths)

Locating an address requires two things: **the `.elf` matching the exact version that crashed**, and **`addr2line` from the cross toolchain**.

### 1. Build Artifacts (.elf / .map)

After running `make app APP_NAME=tuyaos_demo_wukong_ai`, the artifacts land under the version number:

```text
apps/tuyaos_demo_wukong_ai/output/<version>/debug/<core dir>/
├── app.elf     # the symbol-carrying executable — use this with addr2line, not the .bin
├── app.map     # linker memory layout, searchable by address/symbol
├── app.nm      # symbol table (already annotated with file:line, usable as an addr2line cross-check)
└── ...
```

For example, a real build has produced `apps/tuyaos_demo_wukong_ai/output/1.0.67/debug/bk7258_ap/app.elf` and `apps/tuyaos_demo_wukong_ai/output/1.0.67/debug/bk7258/app.elf` (T5/BK7258 is a multi-core chip, so `<core dir>` may show up as `bk7258_ap`, `bk7258`, or other names depending on the project's actual core split).

**The version number must exactly match the firmware flashed on the crashing device** — resolving addresses against the wrong version's `app.elf` produces completely mismatched symbol names and line numbers, with no warning that you picked the wrong file. This is the easiest mistake to make. The `tuyaos_demo_wukong_ai_QIO_<version>.bin` used for flashing and the `app.elf` used for debugging are produced together in the same build, so just keep the version numbers matched.

### 2. Cross Toolchain

`./prepare.sh` downloads the T5 platform's cross toolchain; the actual path to `addr2line` is:

```text
vendor/T5/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line
```

You can confirm it exists and is executable first:

```bash
vendor/T5/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line --version
```

---

## Steps to Locate an Address (copy-paste commands)

1. **Confirm the version and artifact path.** Say the log shows firmware version `1.0.67`, and the crash occurred on the `bk7258_ap` core:

   ```bash
   ELF=apps/tuyaos_demo_wukong_ai/output/1.0.67/debug/bk7258_ap/app.elf
   ADDR2LINE=vendor/T5/toolchain/gcc-arm-none-eabi-10.3-2021.10/bin/arm-none-eabi-addr2line
   ```

2. **Reuse the ready-made command already in the log.** The line at the end of the crash log, `Show more call stack info by run: arm-none-eabi-addr2line -e <firmware name>.elf -a -f <address list>`, already has the arguments assembled — just swap the executable for the full path you confirmed above, swap `-e`'s argument for your local `$ELF`, and copy the rest as-is:

   ```bash
   $ADDR2LINE -e $ELF -a -f 02139abc 02138ef0 02137a10
   ```

   - `-a`: print the address itself first, for easy comparison against the log
   - `-f`: print the function name (otherwise you only get file:line)
   - It's worth also adding `-p` (one address per line, more readable — this is an efficiency tip, not something required by the log's own text):

   ```bash
   $ADDR2LINE -e $ELF -a -f -p 02139abc 02138ef0 02137a10
   ```

3. **If you just want the HardFault site's PC / LR** (i.e. the `pc` and `lr` values from the register dump):

   ```bash
   $ADDR2LINE -e $ELF -a -f -p 0x<PC address> 0x<LR address>
   ```

   PC is the instruction executing at the moment of the crash, and LR is the return address of whoever called it — resolving both immediately tells you "which function it died in" and "who called into it."

4. **If the resolved result is `?? ??:0` (symbol not found)**, check in this order:
   - First confirm whether `$ELF`'s version really matches the crashing firmware (the most common cause);
   - Open `app.map` or `app.nm` in the same directory and search by address prefix — every symbol in `app.nm` is already annotated with `file:line`, so you can manually find which function's range the address falls in;
   - The address itself may simply be invalid (e.g. near `0x00000000`), in which case it inherently can't resolve to a symbol — and that fact alone is meaningful diagnostic information (see the "PC=0" pattern below).

---

## Common Crash Patterns

| Symptom | Likely Cause | Diagnostic Clue |
|---|---|---|
| `pc` (or the top of the call stack resolved by addr2line) lands near `0x0` or is clearly outside the code segment | A call was made through a null/dangling function pointer (invoked before the callback was registered, invoked after the object was freed, etc.) | PC itself usually can't be resolved to a symbol; instead use addr2line on `lr` or an address further out in the call stack to find "who initiated this call," and focus on checking whether that function pointer/callback was already cleared or freed early |
| The log shows `Error: Thread stack(%08x) was overflow`, or a UsageFault reporting `... stack overflow (hardware check) has taken place` | A thread's stack size is too small, recursion goes too deep, or a large local array/struct was allocated on the stack | Cross-reference `Fault on thread %s` to identify the thread, then check the stack size configured when that thread was created; enlarge the stack or move large objects to heap allocation as needed |
| BusFault/MemFault reports an out-of-bounds or misaligned access, e.g. `Bus fault is caused by precise/imprecise data access violation`, `... an unaligned access fault`, along with `The bus fault occurred address is %08x` / `The memory management fault occurred address is %08x` | A wild pointer dereference, an out-of-bounds array read/write, or a misaligned access after casting a struct to a pointer of another type | Check whether the printed `BFAR`/`MMAR` address falls in a reasonable range (or is obviously an anomalous address), then use the call stack to locate the specific read/write statement |
| The device just resets, the serial port only prints a single `Watchdog` message, and there's no full register/call-stack context | Some task went too long without reaching a watchdog-feed point — usually an infinite loop, an indefinitely blocked wait on a semaphore/queue that never gets signaled, or an interrupt handler that ran too long | Since the PC/LR context before the reset is often unavailable, rely mainly on the last stretch of business logs before the reset to judge where it got stuck; if reproducible, start by suspecting recently changed code paths involving blocking waits |

> The above are just the most common, most typical patterns — real-world crash causes vary widely, and it's normal to run into a case that doesn't match any row in this table. Don't force-fit it.

---

## Support

If you've gone through the steps above and still can't pin down the cause, it will greatly help to gather the following before posting on the forum: the complete crash log (including the register dump and call-stack addresses), the firmware version number, the addr2line-resolved call stack (even a partial one), and the code changes made most recently.

If you encounter issues during development, you can post on the TuyaOS Developer Forum [Connected Device Section](https://www.tuyaos.com/viewforum.php?f=11) for help.
