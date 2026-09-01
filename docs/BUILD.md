# Build, flash, logs

Read [README.md](README.md) first.

---

## 1. Toolchain

WCH ship the toolchain inside MounRiver Studio. On this machine:

```
TC="/Applications/MounRiver Studio 2.app/Contents/Resources/app/resources/darwin/components/WCH/Toolchain/RISC-V Embedded GCC12/bin"
"$TC/riscv-wch-elf-gcc" --version   # xPack GNU RISC-V Embedded GCC 12.2.0
```

`riscv-wch-elf-gcc` is a normal GCC with WCH's `xw` custom-instruction support
(`-march=...xw`). Anything GCC 12 accepts, it accepts.

There is also GCC 15 in the bundle (`RISC-V Embedded GCC15`) and an OpenOCD build
(`.../components/WCH/OpenOCD/OpenOCD`). Stick to GCC12 — it is what
`obj/**/subdir.mk` was generated with.

---

## 2. Compile-check (what an agent should do after every change)

You cannot flash. Your bar is **compiles with zero warnings at `-Wall -Wextra`**.

```bash
make check     # compiles every src/ TU with -Wall -Wextra, no link
make           # full build + link -> build/CH570D.hex, prints size
make test      # host unit tests for src/link/ + src/util/ (see §7)
```

The top-level `Makefile` prepends the bundled toolchain to `PATH` itself.
Override if it moved: `make TOOLCHAIN="/path/to/RISC-V Embedded GCC12/bin"`.
`-Wall -Wextra` is applied to `src/` only — the WCH SDK does not build clean
under it.

---

## 3. Full build

### 3.1 Via the Makefile (agents, CI, anyone without MRS)

```bash
make            # -> build/CH570D.{elf,hex,lst,map} + size report
make clean
```
Build output goes to `build/` and never touches the MRS output in `obj/`.
Phase 0 result: FLASH ~16.1 KB / 240 KB, RAM ~3.4 KB / 12 KB.

### 3.2 Via MounRiver Studio (the user's normal path)

Open `CH570D.wvproj` in MRS and Build. MRS's `.cproject` treats the whole
project root as a recursive source path (excluding `EVT/` and a few CH59x
files), so the `src/**` subfolders added in Phase 0 are discovered automatically
on the next build. Output lands in `obj/`:

| File | What |
|---|---|
| `obj/CH570D.elf` | linked image (for OpenOCD / debug) |
| `obj/CH570D.hex` | Intel HEX (for the WCH flash tools / ISP) |
| `obj/CH570D.map` | symbol + memory map |
| `obj/CH570D.lst` | disassembly |

### 3.3 The generated MRS make project

`obj/` also contains MRS's own generated GNU Make project (regenerated each MRS
build):

```bash
cd obj && PATH="$TC:$PATH" make -j4
```

Compile flags (per `obj/src/subdir.mk`):

```
-march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany
-msmall-data-limit=8 -mno-save-restore -fmax-errors=20 -Os -fmessage-length=0
-fsigned-char -ffunction-sections -fdata-sections -fno-common
--param=highcode-gen-section-name=1 -g -DDEBUG=1 -std=gnu99
-I StdPeriphDriver/inc -I src -I RVMSIS
```

Link flags (per `obj/makefile`):

```
-T Ld/Link.ld -nostartfiles -Xlinker --gc-sections
-L . -L StdPeriphDriver
--specs=nano.specs --specs=nosys.specs
-Wl,-Map,CH570D.map
LIBS = -lISP572 -lm          (libISP572.a is in StdPeriphDriver/)
```

Startup: `Startup/startup_CH572.S`. Linker script: `Ld/Link.ld` (do not edit).

### 3.4 Adding a source file / directory

- **The `Makefile` needs nothing** — it does `find src -name '*.c'`. Just create
  the file.
- **MRS** also needs nothing: `.cproject` makes the project root a recursive
  source path (only `EVT/` + some CH59x files excluded), so anything new under
  `src/**` is compiled on the next MRS build. `obj/**/subdir.mk` etc. are
  MRS-auto-generated ("Do not edit") and regenerated then.

For **Phase 4** you also need the RF library: `EVT/EXAM/RF/LIB/libCH57xRF.a`
(add `-L EVT/EXAM/RF/LIB -lCH57xRF` and the matching headers under
`EVT/EXAM/SRC/RVMSIS` / the RF example's `include/`).

---

## 4. Flashing (user-operated — an agent cannot do this)

Requires the physical board + a **WCH-Link** probe (or the on-chip USB
bootloader). Options:

- **MounRiver Studio**: Download button (uses the WCH-Link).
- **OpenOCD** from the bundle:
  `.../components/WCH/OpenOCD/OpenOCD/bin/openocd` with the WCH `wch-riscv.cfg`
  → `program obj/CH570D.hex verify reset exit`.
- **WCH ISP** (`WCHISPTool` / `wchisp`): hold the boot condition at power-up,
  flash `obj/CH570D.hex` over USB. Note: `EVT/EXAM/*/Main.c` headers warn *"when
  downloading via ISP, do not enable the RST pin / watchdog"* — relevant once
  IWDG lands (Phase 6.1).

When you (agent) change anything that touches the DRV2605, timing, or the
protocol, hand the user a one-line summary of **what to look/feel for** so their
bench test is meaningful, and record the result in the relevant doc.

---

## 5. Logs

### Now (bring-up firmware — CDC-ACM virtual serial port)

The box enumerates as a USB serial port. 115200 8N1 (baud is ignored by CDC but
terminals want a number).

| OS | Command |
|---|---|
| macOS | `screen /dev/tty.usbmodem* 115200`  (exit: `Ctrl-A` `K`) |
| Linux | `screen /dev/ttyACM0 115200`  or `picocom -b 115200 /dev/ttyACM0` |
| Windows | open the `COMx` (CDC ACM) port in PuTTY / TeraTerm |

The bring-up prints a boot banner, the DRV2605 probe, the resonance sweep, and
the `demo_simracing` telemetry. See `src/Main.c` `USB_Log_*`.

### After Phase 3

- **CDC-ACM stays** (composite device) — same `screen` command, for quick dev
  checks and the boot report.
- **Primary log channel** becomes `TYPE_LOG` frames on the vendor bulk IN
  endpoint, printed/recorded by `tools/pc_sender`.
- **`stats` via `STATUS_REQ`** is the real telemetry-about-the-box channel during
  streaming (dashboard in `tools/pc_sender`), not text.
- **UART TX on PA7** (`115200 8N1`, needs a USB-serial adapter on that pin) is
  the fallback for debugging the USB stack itself, ISR problems, and hardfaults —
  the one channel that works when USB doesn't.

### After Phase 4 (RF)

`log` sink switches to a `TYPE_LOG` frame on the RF back-channel or the PA7 UART
(`log_set_sink()` only — no application changes).

---

## 6. Host unit tests (`tools/test/`)

The transport-agnostic layers (`src/link/`, `src/util/`) are pure C with no
target dependencies, so they are tested on the host:

```bash
make test                 # from repo root  (delegates to tools/test/)
# or:  cd tools/test && make
```

`make test` does three things:

1. **isolation grep** — fails if `src/link/` `#include`s anything from
   `transport/` or `usb/` (the link layer must stay transport-agnostic —
   [ROADMAP.md](ROADMAP.md) 2.5).
2. **proto-sync `diff`** — fails if `src/link/proto.h` and `tools/proto/proto.h`
   are not byte-identical (the PC keeps its own copy; they must never drift —
   [README.md](README.md) rule 3).
3. builds `run_tests` with the **host** `cc` (not the cross toolchain,
   `-std=c11 -Wall -Wextra -Werror`) from `src/link/{link,link_control}.c`,
   `src/util/crc.c`, `tools/test/mock_engine.c` (a `haptic_engine.h` double using
   the real `haptic_fifo.h`), `tools/test/test_link.c`, and runs it.

Add a link/proto test by adding a `t_*()` function in `test_link.c` and calling
it from `main()`. If you touch `proto.h`, `cp src/link/proto.h tools/proto/` in
the same change or `make test` will fail the `diff`.

---

## 7. Debugging without a serial console

The 2-wire hardware debug interface is on **PA8/PA9 = the I²C pins** and is
disabled in firmware. So single-stepping / SDI-Print is **not available while
I²C runs**. Practical debugging is:

1. `stats` counters + `TYPE_LOG` / CDC text.
2. The PA7 debug UART (after Phase 3.3).
3. Toggle a spare GPIO (PA4/PA10) at instrumentation points and scope it — the
   only way to measure real timing/latency (used in Phase 3.5 benchmarking).
4. As a last resort for a debugger session: temporarily remap I²C to PA5/PA6
   (`RB_I2C_PIN = 0b11` in `R16_PIN_ALTERNATE_H`) in a throw-away build to free
   PA8/PA9 for debug — never commit that.
