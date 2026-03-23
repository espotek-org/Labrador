# see https://github.com/espotek-org/Labrador/wiki/Building-from-source#building-the-firmware
# Usage :
# make 0x01
# 	build a version of the firmware compatible with Windows x64 OSs
# make 0x02
# 	build a version of the firmware compatible with all other OSs
# Here, OS refers to that of the machine with which the Labrador board will interface.
# After running 'make 0x01', to switch to building variant 0x02 instead, first run 'make clean'.
# The same goes for switching from building the 0x02 variant to building the 0x01 variant.
# Some details:
# 	In the build process for variant 0x01, the macro SINGLE_ENDPOINT_INTERFACE
# 	is undefined, while in the build process for variant 0x02,
# 	SINGLE_ENDPOINT_INTERFACE is defined.  In several regions of the source
# 	code, the undefined/defined status of this macro determines which of two
# 	possible code blocks are compiled into the firmware.  The collective effect
# 	of having SINGLE_ENDPOINT_INTERFACE defined is to "change the headers so
# 	that it uses 1x 1023-byte isochronous endpoint, rather than 6x 128-byte
# 	endpoints to send the scope/logic analyzer data" (from
# 	https://github.com/espotek-org/Labrador/issues/260)
# *NOTE* : there is a commented-out line defining SINGLE_ENDPOINT_INTERFACE in
# globals.h.  If it is uncommented, both 'make 0x01' and 'make 0x02' will
# produce variant 0x02.  Further, 'make 0x01' will mislabel the firmware with
# the suffix 0x01.  It is recommended that this line in globals.h be left
# commented-out, which ensures that `make 0x01` and `make 0x02` work as
# expected.

CROSS_COMPILE = avr-
CC      = $(CROSS_COMPILE)gcc
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump
SIZE    = $(CROSS_COMPILE)size

DEFINES = \
-DNDEBUG \
-DBOARD=USER_BOARD

INCLUDES = \
-Icommon/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained \
-Icommon/services/usb/class/vendor/device/example \
-Isrc/ASF/common/services/usb/udc \
-Isrc/ASF/xmega/drivers/nvm \
-Isrc/ASF/common/services/sleepmgr \
-Isrc/ASF/common/services/clock \
-Isrc/ASF/xmega/drivers/sleep \
-Isrc/ASF/xmega/drivers/usb \
-Isrc/ASF/xmega/drivers/cpu \
-Isrc/ASF/common/services/usb/class/vendor \
-Isrc/ASF/common/services/usb/class/vendor/device \
-Isrc/ASF/common/services/usb \
-Icommon/applications/user_application/user_board/config \
-Isrc/ASF/xmega/utils \
-Isrc/config \
-Isrc/ASF/common/boards \
-Isrc/ASF/xmega/utils/preprocessor \
-Isrc/ASF/common/utils \
-Isrc \
-Isrc/ASF/common/boards/user_board \
-Isrc/ASF/common/services/ioport

CPPFLAGS = $(DEFINES) $(INCLUDES)

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

VERSION = 0007
.PHONY: 0x01 0x02
0x01: labrafirm_$(VERSION)_01.elf
0x02: labrafirm_$(VERSION)_02.elf

labrafirm_$(VERSION)_02.elf: DEFINES+=-DSINGLE_ENDPOINT_INTERFACE

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.s
	$(CC) -x assembler-with-cpp $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

labrafirm_$(VERSION)_%.elf: $(OBJS)
	$(CC) -o $@ $(OBJS) -Wl,-Map=$(@:%.elf=%.map) -Wl,-lm -mmcu=atxmega32a4u -Wl,--gc-sections -Wl,--relax
	$(SIZE) $@
	$(OBJDUMP) -h -S $@ > $(@:%.elf=%.lss)
	$(OBJCOPY) -O ihex -R .eeprom -R .fuse -R .lock -R .signature $@ $(@:%.elf=%.hex)
	$(OBJCOPY) -O srec -R .eeprom -R .fuse -R .lock -R .signature $@ $(@:%.elf=%.srec)
	$(OBJCOPY) -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0 --no-change-warnings -O binary $@ $(@:%.elf=%.eep) || exit 0

.PHONY: clean
clean:
	-rm -rf $(OBJS) $(OBJS:%.o=%.d) $(foreach variant,01 02,$(foreach ext,elf map lss hex srec eep,labrafirm_$(VERSION)_$(variant).$(ext)))

ifneq ($(MAKECMDGOALS),clean)
-include $(OBJS:%.o=%.d)
endif
