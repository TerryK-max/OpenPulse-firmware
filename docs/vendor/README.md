# docs/vendor/

Third-party reference material. **Nothing in this folder is committed** except
this README — the files here are other people's copyright. Everything matching
`*.txt` / `*.pdf` is `.gitignore`d. Fetch them yourself for offline reference;
the facts this project actually depends on are transcribed (with citations) into
the tracked docs and headers.

| File to place here | Where to get it | What the project uses it for |
|---|---|---|
| `drv2605l.pdf` (TI **DRV2605L** datasheet, SLOS854C, May 2014 rev Sept 2014) | <https://www.ti.com/lit/ds/symlink/drv2605l.pdf> | The authoritative register map. Every `§8.6.x` citation in `docs/` and `src/drv2605/drv2605_regs.h` points here. Key values are transcribed into [../HARDWARE.md](../HARDWARE.md) §2.2. |

Also referenced but not here:

- **CH572/CH570 MCU datasheet** — converted to per-peripheral Markdown under
  `EVT/DOCS/` (part of the WCH evaluation package; see [../SETUP.md](../SETUP.md)).
- **DRV2605 (non-L) interface doc** — `EVT/DRV2605_interface.md`. ⚠️ Wrong
  variant for the L-specific registers; see [../HARDWARE.md](../HARDWARE.md) §2.1.

Text extractions of datasheets have artefacts (garbled equations, `±` for
exponents/arrows). When a formula matters, open the original PDF.
