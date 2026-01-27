# see https://github.com/espotek-org/Labrador/wiki/Building-from-source#building-the-firmware
# Usage :
# make 01
# 	build a version of the firmware compatible with Windows x64 OSs
# make 02
# 	build a version of the firmware compatible with all other OSs
# Here, OS refers to that of the machine with which the Labrador board will interface.
# After running 'make 01', to switch to building variant 02 instead, first run 'make clean'.
# The same goes for switching from building the 02 variant to building the 01 variant.
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
# globals.h.  If it is uncommented, both 'make 01' and 'make 02' will
# produce variant 02.  Further, 'make 01' will mislabel the firmware with
# the suffix 01.  It is recommended that this line in globals.h be left
# commented-out, which ensures that `make 01` and `make 02` work as
# expected.
LIB_DEP :=
LIBS :=
USER_OBJS :=
OUTPUT_FILE_PATH :=
OUTPUT_FILE_PATH =$(NAME).elf
OUTPUT_FILE_PATH_AS_ARGS +=$(NAME).elf
AVR_APP_PATH :=$$$AVR_APP_PATH$$$

CC=avr-gcc
SRC_DIR=./USB_BULK_TEST/src

INCLUDES=-I"$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained" -I"$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example" -I"$(SRC_DIR)/ASF/common/services/usb/udc" -I"$(SRC_DIR)/ASF/xmega/drivers/nvm" -I"$(SRC_DIR)/ASF/common/services/sleepmgr" -I"$(SRC_DIR)/ASF/common/services/clock" -I"$(SRC_DIR)/ASF/xmega/drivers/sleep" -I"$(SRC_DIR)/ASF/xmega/drivers/usb" -I"$(SRC_DIR)/ASF/xmega/drivers/cpu" -I"$(SRC_DIR)/ASF/common/services/usb/class/vendor" -I"$(SRC_DIR)/ASF/common/services/usb/class/vendor/device" -I"$(SRC_DIR)/ASF/common/services/usb" -I"$(SRC_DIR)mon/applications/user_application/user_board/config" -I"$(SRC_DIR)/ASF/xmega/utils" -I"$(SRC_DIR)/config" -I"$(SRC_DIR)/ASF/common/boards" -I"$(SRC_DIR)/ASF/xmega/utils/preprocessor" -I"$(SRC_DIR)/ASF/common/utils" -I"$(SRC_DIR)" -I"$(SRC_DIR)/ASF/common/boards/user_board" -I"$(SRC_DIR)/ASF/common/services/ioport"

CFLAGS=-std=gnu99 -ffunction-sections -mmcu=atxmega32a4u -fsigned-char -funsigned-bitfields -fdata-sections -fshort-enums -fno-strict-aliasing -fno-jump-tables -fpack-struct -Wall -O2 -MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" -DFIRMWARE_VERSION_ID=$(FIRMWARE_VERSION_ID)

NAME=labrafirm_$(subst 2,4,$(FIRMWARE_VERSION_ID))_$(SUFFIX)

OBJS += \
$(SRC_DIR)/tiny_calibration.o \
$(SRC_DIR)/tiny_dig.o \
$(SRC_DIR)/tiny_eeprom.o \
$(SRC_DIR)/ASF/common/boards/user_board/init.o \
$(SRC_DIR)/ASF/common/services/ioport/xmega/ioport_compat.o \
$(SRC_DIR)/main.o \
$(SRC_DIR)/tiny_adc.o \
$(SRC_DIR)/tiny_dac.o \
$(SRC_DIR)/tiny_dma.o \
$(SRC_DIR)/tiny_timer.o \
$(SRC_DIR)/tiny_uart.o \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o \
$(SRC_DIR)/ASF/common/services/clock/xmega/sysclk.o \
$(SRC_DIR)/ASF/common/services/sleepmgr/xmega/sleepmgr.o \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor.o \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o \
$(SRC_DIR)/ASF/common/services/usb/udc/udc.o \
$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm.o \
$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm_asm.o \
$(SRC_DIR)/ASF/xmega/drivers/cpu/ccp.o \
$(SRC_DIR)/ASF/xmega/drivers/usb/usb_device.o

C_DEPS_AS_ARGS += \
$(SRC_DIR)/tiny_calibration.d \
$(SRC_DIR)/tiny_dig.d \
$(SRC_DIR)/tiny_eeprom.d \
$(SRC_DIR)/ASF/common/boards/user_board/init.d \
$(SRC_DIR)/ASF/common/services/ioport/xmega/ioport_compat.d \
$(SRC_DIR)/main.d \
$(SRC_DIR)/tiny_adc.d \
$(SRC_DIR)/tiny_dac.d \
$(SRC_DIR)/tiny_dma.d \
$(SRC_DIR)/tiny_timer.d \
$(SRC_DIR)/tiny_uart.d \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.d \
$(SRC_DIR)/ASF/common/services/clock/xmega/sysclk.d \
$(SRC_DIR)/ASF/common/services/sleepmgr/xmega/sleepmgr.d \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor.d \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.d \
$(SRC_DIR)/ASF/common/services/usb/udc/udc.d \
$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm.d \
$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm_asm.d \
$(SRC_DIR)/ASF/xmega/drivers/cpu/ccp.d \
$(SRC_DIR)/ASF/xmega/drivers/usb/usb_device.d

OBJS_AS_ARGS += \
$(SRC_DIR)/tiny_calibration.o \
$(SRC_DIR)/tiny_dig.o \
$(SRC_DIR)/tiny_eeprom.o \
$(SRC_DIR)/ASF/common/boards/user_board/init.o \
$(SRC_DIR)/ASF/common/services/ioport/xmega/ioport_compat.o \
$(SRC_DIR)/main.o \
$(SRC_DIR)/tiny_adc.o \
$(SRC_DIR)/tiny_dac.o \
$(SRC_DIR)/tiny_dma.o \
$(SRC_DIR)/tiny_timer.o \
$(SRC_DIR)/tiny_uart.o \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o \
$(SRC_DIR)/ASF/common/services/clock/xmega/sysclk.o \
$(SRC_DIR)/ASF/common/services/sleepmgr/xmega/sleepmgr.o \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor.o \
$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o \
$(SRC_DIR)/ASF/common/services/usb/udc/udc.o \
$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm.o \
$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm_asm.o \
$(SRC_DIR)/ASF/xmega/drivers/cpu/ccp.o \
$(SRC_DIR)/ASF/xmega/drivers/usb/usb_device.o

01: SUFFIX=01
01: $(OUTPUT_FILE_PATH)
	@echo CFLAGS:$(CFLAGS)

02: CFLAGS+=-DSINGLE_ENDPOINT_INTERFACE
02: SUFFIX=02
02: $(OUTPUT_FILE_PATH)
	@echo CFLAGS:$(CFLAGS)

$(SRC_DIR)/tiny_calibration.o: $(SRC_DIR)/tiny_calibration.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_dig.o: $(SRC_DIR)/tiny_dig.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_eeprom.o: $(SRC_DIR)/tiny_eeprom.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/boards/user_board/init.o: $(SRC_DIR)/ASF/common/boards/user_board/init.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/ioport/xmega/ioport_compat.o: $(SRC_DIR)/ASF/common/services/ioport/xmega/ioport_compat.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/main.o: $(SRC_DIR)/main.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_adc.o: $(SRC_DIR)/tiny_adc.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_dac.o: $(SRC_DIR)/tiny_dac.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_dma.o: $(SRC_DIR)/tiny_dma.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_timer.o: $(SRC_DIR)/tiny_timer.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/tiny_uart.o: $(SRC_DIR)/tiny_uart.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o: $(SRC_DIR)/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/clock/xmega/sysclk.o: $(SRC_DIR)/ASF/common/services/clock/xmega/sysclk.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/sleepmgr/xmega/sleepmgr.o: $(SRC_DIR)/ASF/common/services/sleepmgr/xmega/sleepmgr.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor.o: $(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o: $(SRC_DIR)/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/common/services/usb/udc/udc.o: $(SRC_DIR)/ASF/common/services/usb/udc/udc.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/xmega/drivers/cpu/ccp.o: $(SRC_DIR)/ASF/xmega/drivers/cpu/ccp.s
	@echo Building file: $<
	@$(CC) -x assembler-with-cpp -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm.o: $(SRC_DIR)/ASF/xmega/drivers/nvm/nvm.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/xmega/drivers/nvm/nvm_asm.o: $(SRC_DIR)/ASF/xmega/drivers/nvm/nvm_asm.s
	@echo Building file: $<
	@$(CC) -x assembler-with-cpp -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(SRC_DIR)/ASF/xmega/drivers/usb/usb_device.o: $(SRC_DIR)/ASF/xmega/drivers/usb/usb_device.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

$(OUTPUT_FILE_PATH): $(OBJS) $(USER_OBJS) $(OUTPUT_FILE_DEP) $(LIB_DEP) $(LINKER_SCRIPT_DEP)
	@echo Building target: $@
	$(CC) -o$(OUTPUT_FILE_PATH_AS_ARGS) $(OBJS_AS_ARGS) $(USER_OBJS) $(LIBS) -Wl,-Map=$(NAME)".map"  -Wl,-lm -mmcu=atxmega32a4u -Wl,--gc-sections -Wl,--relax 
	avr-objcopy -O ihex -R .eeprom -R .fuse -R .lock -R .signature  $(NAME)".elf" $(NAME)".hex"
	avr-objdump -h -S $(NAME)".elf" > $(NAME)".lss"
	avr-objcopy -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0 --no-change-warnings -O binary $(NAME)".elf" $(NAME)".eep" || exit 0
	avr-objcopy -O srec -R .eeprom -R .fuse -R .lock -R .signature  $(NAME)".elf" $(NAME)".srec"
	avr-size $(NAME)".elf"
	@echo Finished successfully: $(NAME)

clean_o_d :
	-rm -rf $(OBJS_AS_ARGS)
	-rm -rf $(C_DEPS_AS_ARGS)
VARIANTS = 01 02
clean : clean_o_d
	$(foreach SUFFIX,$(VARIANTS), \
		rm -rf $(NAME).hex ; \
		rm -rf $(NAME).lss ; \
		rm -rf $(NAME).eep ; \
		rm -rf $(NAME).srec ; \
		rm -rf $(NAME).elf $(NAME).a $(NAME).map)
