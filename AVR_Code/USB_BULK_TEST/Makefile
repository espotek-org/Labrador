# see https://github.com/espotek-org/Labrador/wiki/Building-from-source#building-the-firmware
# Usage :
# `make 01`
# 	build a variant of the firmware compatible with Windows x64 OSs
# `make 02`
# 	build a variant of the firmware compatible with all other OSs
# Here, OS refers to that of the machine with which the Labrador board will interface.
#
# Whenever switching between building the two variants, always run either
# `make clean` (to delete everything) or `make mostlyclean` (to delete the
# object files and leave the .hex that you will load to the Labrador board)
# so that all objects will be rebuilt with the correct macro definitions.
#
# Some details:
# 	In the build process for variant 01, the macro SINGLE_ENDPOINT_INTERFACE
# 	is undefined, while in the build process for variant 02,
# 	SINGLE_ENDPOINT_INTERFACE is defined.  In several regions of the source
# 	code, the undefined/defined status of this macro determines which of two
# 	possible code blocks are compiled into the firmware.  The collective effect
# 	of having SINGLE_ENDPOINT_INTERFACE defined is to "change the headers so
# 	that it uses 1x 1023-byte isochronous endpoint, rather than 6x 128-byte
# 	endpoints to send the scope/logic analyzer data" (from
# 	https://github.com/espotek-org/Labrador/issues/260)
# *NOTE* : there is a commented-out line defining SINGLE_ENDPOINT_INTERFACE in
# globals.h.  If it is uncommented, both `make 01` and `make 02` will
# produce variant 02.  Further, `make 01` will mislabel the firmware with
# the suffix 01.  It is recommended that this line in globals.h be left
# commented-out, which ensures that `make 01` and `make 02` work as
# expected.  Similarly, FIRMWARE_VERSION_ID is commented out in globals.h
# and instead defined by this Makefile.  Only uncomment either definition if
# you are using Atmel Studio or similar and are not using this Makefile to build.

CROSS_COMPILE = avr-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

DEFINES = \
NDEBUG \
BOARD=USER_BOARD

INCLUDES = \
common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained \
common/services/usb/class/vendor/device/example \
src/ASF/common/services/usb/udc \
src/ASF/xmega/drivers/nvm \
src/ASF/common/services/sleepmgr \
src/ASF/common/services/clock \
src/ASF/xmega/drivers/sleep \
src/ASF/xmega/drivers/usb \
src/ASF/xmega/drivers/cpu \
src/ASF/common/services/usb/class/vendor \
src/ASF/common/services/usb/class/vendor/device \
src/ASF/common/services/usb \
common/applications/user_application/user_board/config \
src/ASF/xmega/utils \
src/config \
src/ASF/common/boards \
src/ASF/xmega/utils/preprocessor \
src/ASF/common/utils \
src \
src/ASF/common/boards/user_board \
src/ASF/common/services/ioport

CPPFLAGS = $(addprefix -D,$(DEFINES)) $(addprefix -I,$(INCLUDES))

CFLAGS = \
-std=gnu99 \
-ffunction-sections \
-mmcu=atxmega32a4u \
-fsigned-char \
-funsigned-bitfields \
-fdata-sections \
-fshort-enums \
-fno-strict-aliasing \
-fno-jump-tables \
-fpack-struct \
-Wall \
-O2 \
-MD -MP -MF $(@:%.o=%.d) -MT $(@:%.o=%.d) -MT $(@:%.o=%.o)

OBJS = \
src/tiny_calibration.o \
src/tiny_dig.o \
src/tiny_eeprom.o \
src/ASF/common/boards/user_board/init.o \
src/ASF/common/services/ioport/xmega/ioport_compat.o \
src/main.o \
src/tiny_adc.o \
src/tiny_dac.o \
src/tiny_dma.o \
src/tiny_timer.o \
src/tiny_uart.o \
src/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o \
src/ASF/common/services/clock/xmega/sysclk.o \
src/ASF/common/services/sleepmgr/xmega/sleepmgr.o \
src/ASF/common/services/usb/class/vendor/device/udi_vendor.o \
src/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o \
src/ASF/common/services/usb/udc/udc.o \
src/ASF/xmega/drivers/nvm/nvm.o \
src/ASF/xmega/drivers/nvm/nvm_asm.o \
src/ASF/xmega/drivers/cpu/ccp.o \
src/ASF/xmega/drivers/usb/usb_device.o

DEPS = $(OBJS:%.o=%.d)

VERSION = 0007
VARIANTS = 01 02
EXEEXTS = .elf .map .lss .hex .srec .eep
EXES = $(foreach variant,$(VARIANTS),$(foreach ext,$(EXEEXTS),labrafirm_$(VERSION)_$(variant)$(ext)))

.PHONY: $(VARIANTS)
01: labrafirm_$(VERSION)_01.hex
02: labrafirm_$(VERSION)_02.hex
labrafirm_$(VERSION)_01.elf: DEFINES += FIRMWARE_VERSION_ID=0x$(VERSION)
labrafirm_$(VERSION)_02.elf: DEFINES += FIRMWARE_VERSION_ID=0x$(VERSION) SINGLE_ENDPOINT_INTERFACE

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(CC) -x assembler-with-cpp $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

labrafirm_$(VERSION)_%.elf: $(OBJS)
	$(CC) -o $@ $(OBJS) -Wl,-Map=$(@:%.elf=%.map) -Wl,-lm -mmcu=atxmega32a4u -Wl,--gc-sections -Wl,--relax
	$(SIZE) $@
	$(OBJDUMP) -h -S $@ > $(@:%.elf=%.lss)

%.hex: %.elf
	$(OBJCOPY) -O ihex -R .eeprom -R .fuse -R .lock -R .signature $< $@ && sed -i 's/\r$$//' $@ && chmod -x $@

%.srec: %.elf
	$(OBJCOPY) -O srec -R .eeprom -R .fuse -R .lock -R .signature $< $@ && sed -i 's/\r$$//' $@ && chmod -x $@

%.eep: %.elf
	$(OBJCOPY) -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0 --no-change-warnings -O binary $< $@ && chmod -x $@ || exit 0

.PHONY: mostlyclean clean
mostlyclean:
	-rm -f $(OBJS) $(DEPS)
clean: mostlyclean
	-rm -f $(EXES)

$(OBJS) $(EXES): Makefile
-include $(DEPS)
