# Setup — getting the vendor SDK in place

This repository contains **only the project's own code** (`src/`, `tools/`,
`docs/`, `Makefile`, the MRS project files). The WCH chip-support code and the
WCH evaluation package are **not redistributed here** — they are Nanjing Qinheng
Microelectronics (WCH) copyright. You must add them yourself before the project
will build. This is a one-time step.

## 1. What is missing

| Path | What it is | Where to get it |
|---|---|---|
| `StdPeriphDriver/` | CH57x peripheral driver (`CH57x_*.c/.h`) **+ `libISP572.a`** | A fresh MounRiver Studio project for the CH570D / CH572, or the CH570/CH572 EVT package (`EVT/EXAM/SRC/StdPeriphDriver/`). |
| `Startup/startup_CH572.S` | Reset vector + startup | same |
| `RVMSIS/` | `core_riscv.h` etc. (QingKe V4 CMSIS) | same (`EVT/EXAM/SRC/RVMSIS/`) |
| `Ld/Link.ld` | Linker script | same (`EVT/EXAM/SRC/Ld/`) |
| `EVT/` | The full WCH **CH570/CH572 EVT** evaluation package: peripheral examples, the RF / BLE / USB-host libraries, board-reference PDFs and schematics. Only needed as *reference* for Phases 3–6 (USB composite, RF, IAP…). The core build does **not** need it. | <https://www.wch.cn/products/CH572.html> → "开发资料" / "EVT", or <https://www.wch-ic.com/>. |

`docs/vendor/` — put third-party datasheets there for offline reference; that
folder is `.gitignore`d (see [vendor/README.md](vendor/README.md)).

## 2. Toolchain

The RISC-V GCC with WCH's `xw` extension ships inside **MounRiver Studio**
(GCC 12 — `riscv-wch-elf-gcc`). Install MRS (macOS / Windows / Linux) from
<http://www.mounriver.com/>. The `Makefile` auto-detects the macOS MRS path;
override if needed:

```bash
make TOOLCHAIN="/path/to/RISC-V Embedded GCC12/bin"
```

See [BUILD.md](BUILD.md) §1 for details.

## 3. Verify

```bash
make check      # every src/ TU compiles clean at -Wall -Wextra
make            # full firmware -> build/CH570D.hex
make test       # host unit tests for src/link + src/util  (no toolchain needed)
```

`make test` needs only a host C compiler, so it works even before the vendor SDK
is in place.

## 4. MounRiver Studio users

Open `CH570D.wvproj`. MRS's `.cproject` treats the project root as a recursive
source path (only `EVT/` + a few CH59x files excluded), so `src/**` is picked up
automatically. Output lands in `obj/`. See [BUILD.md](BUILD.md) §3.2.
