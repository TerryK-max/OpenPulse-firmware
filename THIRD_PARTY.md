# Third-party components

The project's own code (`src/`, `tools/`, `docs/`, `Makefile`, MRS project
files) is licensed under **Apache-2.0** — see [LICENSE](LICENSE) and
[NOTICE](NOTICE).

Building the firmware also needs the components below. **None of them are in
this repository** — you obtain them yourself ([docs/SETUP.md](docs/SETUP.md)),
and each is governed by its own licence, not by this project's.

| Component | Owner | In repo? | Notes |
|---|---|---|---|
| CH57x peripheral driver (`StdPeriphDriver/`, incl. `libISP572.a`), startup (`Startup/`), CMSIS (`RVMSIS/`), linker script (`Ld/`) | Nanjing Qinheng Microelectronics Co., Ltd. (WCH) | No — `.gitignore`d | © 2021 WCH. Shipped with MounRiver Studio and the CH570/CH572 EVT package. Redistribution terms are set by WCH; get it from the source. |
| WCH evaluation package (`EVT/`): peripheral examples, RF / BLE / USB-host libraries (`libCH57xRF.a`, `libCH572BLE_PERI.a`, `libRV3UFI.a`), board-reference PDFs, schematics | WCH | No — `.gitignore`d | Reference material for Phases 3–6 only; the core build does not use it. `EVT/DOCS/` and `EVT/DRV2605_interface.md` are AI-assisted Markdown conversions of the WCH CH572 and TI DRV2605 datasheets — derivative of those copyrighted works. |
| TI **DRV2605L** datasheet (SLOS854C) | Texas Instruments (contains Immersion-licensed TouchSense content) | No — `docs/vendor/*.pdf` is `.gitignore`d | <https://www.ti.com/lit/ds/symlink/drv2605l.pdf>. Only the specific register facts this project depends on are transcribed, with citations, into `docs/HARDWARE.md` §2.2 and `src/drv2605/drv2605_regs.h`. |
| RISC-V GCC 12 toolchain with WCH `xw` extension | WCH / xPack / GNU | No | Ships inside MounRiver Studio (<http://www.mounriver.com/>). |

If you believe a file in this repository infringes your rights, open an issue and
it will be removed.
