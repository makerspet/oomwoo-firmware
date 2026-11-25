# OOMWOO firmware: main-phone board U13 (CH32V307VCT6) and U28 (CH32L103C8T6),
# and the charging dock (CH32V003F4P6).
#
#   scripts/install-toolchain.sh     # once: xPack riscv-none-elf-gcc
#   scripts/fetch-wch-sdk.sh         # once: pinned WCH SDK into third_party/
#   make                             # all three images in build/robot/
#   make u13 | make u28 | make dock
#   make flash-u13 | flash-u28 | flash-dock   # WCH-LinkE + wlink
#
# Plain GNU make, no IDE. Works in WSL2, Linux, macOS and CI.

CROSS   ?= riscv-none-elf-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size
WLINK   ?= wlink

WCH_SDK ?= third_party/wch
# BOARD=robot (default): the main-phone PCB. BOARD=evt: WCH's evaluation boards
# (U13 on the CH32V307V-R1: console on USART1 via its on-board WCH-Link, carpet
# off; U28 on the CH32L103C8T6 board: power-button input pulled up internally).
BOARD   ?= robot
BUILD   ?= build/$(BOARD)
FW_VERSION ?= 1

REPO    := ..
COMMON  := common
PROTO   := $(REPO)/src/oomwoo_protocol.c

CFLAGS_COMMON := -std=gnu11 -Os -g3 -Wall -Wextra -Werror \
  -Wno-unused-parameter \
  -ffunction-sections -fdata-sections -fno-common -msmall-data-limit=8 -mno-save-restore \
  -DFW_VERSION=$(FW_VERSION) -I$(COMMON) -I$(REPO)/include
# Vendor sources are compiled without -Werror and with their own warnings muted.
VENDOR_CFLAGS := -std=gnu99 -Os -g3 -w -ffunction-sections -fdata-sections -fno-common \
  -msmall-data-limit=8 -mno-save-restore
LDFLAGS_COMMON := -nostartfiles -Wl,--gc-sections -Wl,--print-memory-usage -Wl,--no-warn-rwx-segments\
  --specs=nano.specs --specs=nosys.specs -Lld

# Portable logic (also built and tested on the host: make test) ...
LOGIC_SRCS := $(COMMON)/fmt.c $(COMMON)/cli.c $(COMMON)/mcu_link.c $(COMMON)/nec.c \
  $(COMMON)/power_sm.c $(COMMON)/charger_sm.c $(COMMON)/drive_gate.c \
  $(COMMON)/dock_sm.c $(COMMON)/nec_tx.c
# ... and hardware helpers shared by both chips.
COMMON_SRCS := $(LOGIC_SRCS) $(COMMON)/systime.c $(COMMON)/uart.c $(PROTO)

# ---------------------------------------------------------------- U13
U13_SDK   := $(WCH_SDK)/ch32v307/EVT/EXAM/SRC
U13_ARCH  := -march=rv32imafc_zicsr_zifencei -mabi=ilp32f
U13_DEFS  := -DCH32V30x_D8C -DHSE_VALUE=8000000 $(if $(filter evt,$(BOARD)),-DBOARD_EVT)
U13_INC   := -Iu13 -isystem $(U13_SDK)/Peripheral/inc -isystem $(U13_SDK)/Core
U13_PERIPH := gpio rcc misc usart tim adc dma exti i2c iwdg dbgmcu opa flash
U13_SRCS  := $(wildcard u13/*.c) $(COMMON_SRCS)
U13_VSRCS := $(U13_PERIPH:%=$(U13_SDK)/Peripheral/src/ch32v30x_%.c) $(U13_SDK)/Core/core_riscv.c
U13_ASM   := $(U13_SDK)/Startup/startup_ch32v30x_D8C.S

# ---------------------------------------------------------------- U28
U28_SDK   := $(WCH_SDK)/ch32l103/EVT/EXAM/SRC
U28_ARCH  := -march=rv32imac_zicsr_zifencei -mabi=ilp32
U28_DEFS  := -DCH32L103 $(if $(filter evt,$(BOARD)),-DBOARD_EVT)
U28_INC   := -Iu28 -isystem $(U28_SDK)/Peripheral/inc -isystem $(U28_SDK)/Core
U28_PERIPH := gpio rcc misc usart tim adc dma iwdg dbgmcu flash
U28_SRCS  := $(wildcard u28/*.c) $(COMMON_SRCS)
U28_VSRCS := $(U28_PERIPH:%=$(U28_SDK)/Peripheral/src/ch32l103_%.c) $(U28_SDK)/Core/core_riscv.c
U28_ASM   := $(U28_SDK)/Startup/startup_ch32l103.S
U28_VENDOR_EXTRA := -include u28/wch_globals.h

# ---------------------------------------------------------------- dock
DOCK_SDK   := $(WCH_SDK)/ch32v003/EVT/EXAM/SRC
DOCK_ARCH  := -march=rv32ec_zicsr_zifencei -mabi=ilp32e
DOCK_DEFS  := -DCH32V003 -DUART_RX_SIZE=64u -DUART_TX_SIZE=256u
DOCK_INC   := -Idock -isystem $(DOCK_SDK)/Peripheral/inc -isystem $(DOCK_SDK)/Core
DOCK_PERIPH := gpio rcc misc usart tim adc dma i2c iwdg dbgmcu
DOCK_SRCS  := $(wildcard dock/*.c) $(COMMON)/fmt.c $(COMMON)/cli.c $(COMMON)/uart.c \
  $(COMMON)/systime.c $(COMMON)/dock_sm.c $(COMMON)/nec_tx.c
DOCK_VSRCS := $(DOCK_PERIPH:%=$(DOCK_SDK)/Peripheral/src/ch32v00x_%.c) $(DOCK_SDK)/Core/core_riscv.c
DOCK_ASM   := $(DOCK_SDK)/Startup/startup_ch32v00x.S

obj = $(addprefix $(BUILD)/$(1)/,$(addsuffix .o,$(subst ..,_up,$(basename $(2)))))

U13_OBJS := $(call obj,u13,$(U13_SRCS)) $(call obj,u13/vendor,$(U13_VSRCS) $(U13_ASM))
U28_OBJS := $(call obj,u28,$(U28_SRCS)) $(call obj,u28/vendor,$(U28_VSRCS) $(U28_ASM))
DOCK_OBJS := $(call obj,dock,$(DOCK_SRCS)) $(call obj,dock/vendor,$(DOCK_VSRCS) $(DOCK_ASM))

.PHONY: all u13 u28 dock clean flash-u13 flash-u28 flash-dock check-sdk test
all: u13 u28 dock
u13: check-sdk $(BUILD)/u13.bin
u28: check-sdk $(BUILD)/u28.bin
dock: check-sdk $(BUILD)/dock.bin

check-sdk:
	@test -d $(U13_SDK) -a -d $(U28_SDK) -a -d $(DOCK_SDK) || { echo "WCH SDK missing: run scripts/fetch-wch-sdk.sh"; exit 1; }

define rules
$(BUILD)/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$(CC) $($(2)_ARCH) $($(2)_DEFS) $(CFLAGS_COMMON) $($(2)_INC) -MMD -MP -c $$< -o $$@
$(BUILD)/$(1)/_up/%.o: ../%.c
	@mkdir -p $$(dir $$@)
	$(CC) $($(2)_ARCH) $($(2)_DEFS) $(CFLAGS_COMMON) $($(2)_INC) -MMD -MP -c $$< -o $$@
$(BUILD)/$(1)/vendor/%.o: %.c
	@mkdir -p $$(dir $$@)
	$(CC) $($(2)_ARCH) $($(2)_DEFS) $(VENDOR_CFLAGS) $($(2)_INC) $($(2)_VENDOR_EXTRA) -MMD -MP -c $$< -o $$@
$(BUILD)/$(1)/vendor/%.o: %.S
	@mkdir -p $$(dir $$@)
	$(CC) $($(2)_ARCH) $($(2)_DEFS) -x assembler-with-cpp -c $$< -o $$@
$(BUILD)/$(1).elf: $($(2)_OBJS) ld/$(3).ld ld/sections.ld
	$(CC) $($(2)_ARCH) $(LDFLAGS_COMMON) -Tld/$(3).ld -Wl,-Map=$(BUILD)/$(1).map $($(2)_OBJS) -o $$@
	$(SIZE) $$@
$(BUILD)/$(1).bin: $(BUILD)/$(1).elf
	$(OBJCOPY) -O binary $$< $$@
	$(OBJCOPY) -O ihex $$< $(BUILD)/$(1).hex
endef

$(eval $(call rules,u13,U13,ch32v307))
$(eval $(call rules,u28,U28,ch32l103))
$(eval $(call rules,dock,DOCK,ch32v003))

flash-u13: $(BUILD)/u13.bin
	$(WLINK) flash --address 0x08000000 $<
flash-u28: $(BUILD)/u28.bin
	$(WLINK) flash --address 0x08000000 $<
flash-dock: $(BUILD)/dock.bin
	$(WLINK) flash --address 0x08000000 $<

# Host tests of the portable logic (no SDK, no cross compiler needed).
HOSTCC ?= cc
test:
	@mkdir -p $(BUILD)
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined -I$(COMMON) 	  -I$(REPO)/include $(filter-out $(COMMON)/cli.c,$(LOGIC_SRCS)) $(PROTO) $(REPO)/tests/mcu_logic_test.c 	  -o $(BUILD)/mcu_logic_test -lm
	$(BUILD)/mcu_logic_test

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
