# OpenPulse firmware — standalone build (no MounRiver Studio required)
#
# Mirrors the compiler/linker flags MRS generated in obj/**/subdir.mk and
# obj/makefile (MRS 2.5.0). Output goes to build/ so it never clashes with the
# MRS output in obj/. See docs/BUILD.md.
#
#   make            - build build/CH570D.elf + .hex + .lst + size
#   make check      - compile every src/ TU with -Wall -Wextra, no link (agent gate)
#   make clean
#   make flash      - user-only; needs a WCH-Link probe + openocd (see docs/BUILD.md)
#
# Override the toolchain bin directory if it moves:
#   make TOOLCHAIN="/path/to/RISC-V Embedded GCC12/bin"

TOOLCHAIN ?= /Applications/MounRiver Studio 2.app/Contents/Resources/app/resources/darwin/components/WCH/Toolchain/RISC-V Embedded GCC12/bin
CROSS     := riscv-wch-elf-

# The bundled toolchain path contains spaces. Rather than fight make's direct
# exec / $(wildcard) tokenisation, every recipe runs through /bin/sh with the
# toolchain prepended to PATH (a quoted, colon-separated entry may contain
# spaces) and invokes the tools by bare name.
R := PATH="$(TOOLCHAIN):$$PATH"

CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size

BUILD  := build
TARGET := $(BUILD)/CH570D

# ---- sources -------------------------------------------------------------
SRC_C   := $(shell find src -name '*.c')
SPD_C   := $(wildcard StdPeriphDriver/*.c)
STARTUP := Startup/startup_CH572.S

C_SRCS  := $(SRC_C) $(SPD_C)
OBJS    := $(patsubst %,$(BUILD)/%.o,$(C_SRCS)) $(BUILD)/$(STARTUP).o

# ---- flags (verbatim from MRS subdir.mk / makefile) ----------------------
ARCH    := -march=rv32imc_zba_zbb_zbc_zbs_xw -mabi=ilp32 -mcmodel=medany \
           -msmall-data-limit=8 -mno-save-restore

INCS    := -Isrc -IStdPeriphDriver/inc -IRVMSIS

CFLAGS  := $(ARCH) -Os -g -std=gnu99 \
           -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections \
           -fno-common --param=highcode-gen-section-name=1 \
           -DDEBUG=1 $(INCS)

# The project's own code must be clean at -Wall -Wextra. The WCH SDK is not, so
# those flags apply only to src/ (see the two pattern rules below).
WARN_PROJECT := -Wall -Wextra

LDFLAGS := $(ARCH) -Os -g -T Ld/Link.ld -nostartfiles -Xlinker --gc-sections \
           -LStdPeriphDriver -Xlinker --print-memory-usage \
           -Wl,-Map,$(TARGET).map --specs=nano.specs --specs=nosys.specs
LDLIBS  := -lISP572 -lm

# ---- rules --------------------------------------------------------------
.PHONY: all check clean flash
all: $(TARGET).hex $(TARGET).lst
	@$(R) $(SIZE) --format=berkeley $(TARGET).elf

$(BUILD)/src/%.c.o: src/%.c
	@mkdir -p $(dir $@)
	$(R) $(CC) $(CFLAGS) $(WARN_PROJECT) -MMD -MP -c -o $@ $<

$(BUILD)/StdPeriphDriver/%.c.o: StdPeriphDriver/%.c
	@mkdir -p $(dir $@)
	$(R) $(CC) $(CFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/$(STARTUP).o: $(STARTUP)
	@mkdir -p $(dir $@)
	$(R) $(CC) $(ARCH) -g -x assembler-with-cpp -c -o $@ $<

$(TARGET).elf: $(OBJS)
	@mkdir -p $(dir $@)
	$(R) $(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(TARGET).hex: $(TARGET).elf
	$(R) $(OBJCOPY) -O ihex $< $@

$(TARGET).lst: $(TARGET).elf
	$(R) $(OBJDUMP) --source --all-headers --demangle -M xw --line-numbers --wide $< > $@

# Compile-only gate for agents: every project TU must build warning-free.
check: $(patsubst %,$(BUILD)/%.o,$(SRC_C))
	@echo "check: all src/ translation units compile clean"

# Host unit tests for the transport-agnostic layers (link/, util/). Uses the
# host compiler, not the cross toolchain. See tools/test/.
.PHONY: test
test:
	$(MAKE) -C tools/test test

clean:
	rm -rf $(BUILD)

flash: $(TARGET).hex
	@echo "Flashing is user-operated - see docs/BUILD.md section 4."
	@echo "e.g.: openocd -f wch-riscv.cfg -c 'program $(TARGET).hex verify reset exit'"

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
