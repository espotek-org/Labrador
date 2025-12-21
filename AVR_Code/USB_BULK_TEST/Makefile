# see https://github.com/espotek-org/Labrador/wiki/Building-from-source#building-the-firmware
EXECUTABLES :=
LIB_DEP :=
LIBS :=
USER_OBJS :=
OUTPUT_FILE_PATH :=
OUTPUT_FILE_PATH +=labrafirm_new.elf
OUTPUT_FILE_PATH_AS_ARGS +=labrafirm_new.elf
AVR_APP_PATH :=$$$AVR_APP_PATH$$$

AVRDIR=
CC=$(AVRDIR)/bin/avr-gcc
INCLUDES=-I"./common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained" -I"./common/services/usb/class/vendor/device/example" -I"./src/ASF/common/services/usb/udc" -I"./src/ASF/xmega/drivers/nvm" -I"./src/ASF/common/services/sleepmgr" -I"./src/ASF/common/services/clock" -I"./src/ASF/xmega/drivers/sleep" -I"./src/ASF/xmega/drivers/usb" -I"./src/ASF/xmega/drivers/cpu" -I"./src/ASF/common/services/usb/class/vendor" -I"./src/ASF/common/services/usb/class/vendor/device" -I"./src/ASF/common/services/usb" -I"./common/applications/user_application/user_board/config" -I"./src/ASF/xmega/utils" -I"./src/config" -I"./src/ASF/common/boards" -I"./src/ASF/xmega/utils/preprocessor" -I"./src/ASF/common/utils" -I"./src" -I"./src/ASF/common/boards/user_board" -I"./src/ASF/common/services/ioport"

CFLAGS=-std=gnu99 -ffunction-sections -mmcu=atxmega32a4u -fsigned-char -funsigned-bitfields -fdata-sections -fshort-enums -fno-strict-aliasing -fno-jump-tables -fpack-struct -Wall -O2 -MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -MT"$(@:%.o=%.o)" 

OBJS += \
./src/tiny_calibration.o \
./src/tiny_dig.o \
./src/tiny_eeprom.o \
./src/ASF/common/boards/user_board/init.o \
./src/ASF/common/services/ioport/xmega/ioport_compat.o \
./src/main.o \
./src/tiny_adc.o \
./src/tiny_dac.o \
./src/tiny_dma.o \
./src/tiny_timer.o \
./src/tiny_uart.o \
./src/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o \
./src/ASF/common/services/clock/xmega/sysclk.o \
./src/ASF/common/services/sleepmgr/xmega/sleepmgr.o \
./src/ASF/common/services/usb/class/vendor/device/udi_vendor.o \
./src/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o \
./src/ASF/common/services/usb/udc/udc.o \
./src/ASF/xmega/drivers/nvm/nvm.o \
./src/ASF/xmega/drivers/nvm/nvm_asm.o \
./src/ASF/xmega/drivers/cpu/ccp.o \
./src/ASF/xmega/drivers/usb/usb_device.o

C_DEPS_AS_ARGS += \
./src/tiny_calibration.d \
./src/tiny_dig.d \
./src/tiny_eeprom.d \
./src/ASF/common/boards/user_board/init.d \
./src/ASF/common/services/ioport/xmega/ioport_compat.d \
./src/main.d \
./src/tiny_adc.d \
./src/tiny_dac.d \
./src/tiny_dma.d \
./src/tiny_timer.d \
./src/tiny_uart.d \
./src/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.d \
./src/ASF/common/services/clock/xmega/sysclk.d \
./src/ASF/common/services/sleepmgr/xmega/sleepmgr.d \
./src/ASF/common/services/usb/class/vendor/device/udi_vendor.d \
./src/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.d \
./src/ASF/common/services/usb/udc/udc.d \
./src/ASF/xmega/drivers/nvm/nvm.d \
./src/ASF/xmega/drivers/nvm/nvm_asm.d \
./src/ASF/xmega/drivers/cpu/ccp.d \
./src/ASF/xmega/drivers/usb/usb_device.d

OBJS_AS_ARGS += \
./src/tiny_calibration.o \
./src/tiny_dig.o \
./src/tiny_eeprom.o \
./src/ASF/common/boards/user_board/init.o \
./src/ASF/common/services/ioport/xmega/ioport_compat.o \
./src/main.o \
./src/tiny_adc.o \
./src/tiny_dac.o \
./src/tiny_dma.o \
./src/tiny_timer.o \
./src/tiny_uart.o \
./src/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o \
./src/ASF/common/services/clock/xmega/sysclk.o \
./src/ASF/common/services/sleepmgr/xmega/sleepmgr.o \
./src/ASF/common/services/usb/class/vendor/device/udi_vendor.o \
./src/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o \
./src/ASF/common/services/usb/udc/udc.o \
./src/ASF/xmega/drivers/nvm/nvm.o \
./src/ASF/xmega/drivers/nvm/nvm_asm.o \
./src/ASF/xmega/drivers/cpu/ccp.o \
./src/ASF/xmega/drivers/usb/usb_device.o

./src/tiny_calibration.o: ./src/tiny_calibration.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_dig.o: ./src/tiny_dig.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_eeprom.o: ./src/tiny_eeprom.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/boards/user_board/init.o: ./src/ASF/common/boards/user_board/init.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/ioport/xmega/ioport_compat.o: ./src/ASF/common/services/ioport/xmega/ioport_compat.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/main.o: ./src/main.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_adc.o: ./src/tiny_adc.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_dac.o: ./src/tiny_dac.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_dma.o: ./src/tiny_dma.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_timer.o: ./src/tiny_timer.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/tiny_uart.o: ./src/tiny_uart.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.o: ./src/ASF/common/services/usb/class/vendor/device/example/atxmega256a3bu_xmega_a3bu_xplained/ui.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/clock/xmega/sysclk.o: ./src/ASF/common/services/clock/xmega/sysclk.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/sleepmgr/xmega/sleepmgr.o: ./src/ASF/common/services/sleepmgr/xmega/sleepmgr.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/usb/class/vendor/device/udi_vendor.o: ./src/ASF/common/services/usb/class/vendor/device/udi_vendor.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.o: ./src/ASF/common/services/usb/class/vendor/device/udi_vendor_desc.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/common/services/usb/udc/udc.o: ./src/ASF/common/services/usb/udc/udc.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/xmega/drivers/cpu/ccp.o: ./src/ASF/xmega/drivers/cpu/ccp.s
	@echo Building file: $<
	@$(CC) -x assembler-with-cpp -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/xmega/drivers/nvm/nvm.o: ./src/ASF/xmega/drivers/nvm/nvm.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/xmega/drivers/nvm/nvm_asm.o: ./src/ASF/xmega/drivers/nvm/nvm_asm.s
	@echo Building file: $<
	@$(CC) -x assembler-with-cpp -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS) -c -o "$@" "$<" 
	@echo Finished building: $<

./src/ASF/xmega/drivers/usb/usb_device.o: ./src/ASF/xmega/drivers/usb/usb_device.c
	@echo Building file: $<
	@$(CC) -DNDEBUG -DBOARD=USER_BOARD $(INCLUDES) $(CFLAGS)-c -o "$@" "$<" 
	@echo Finished building: $<

all: $(OUTPUT_FILE_PATH)

$(OUTPUT_FILE_PATH): $(OBJS) $(USER_OBJS) $(OUTPUT_FILE_DEP) $(LIB_DEP) $(LINKER_SCRIPT_DEP)
	@echo Building target: $@
	$(CC) -o$(OUTPUT_FILE_PATH_AS_ARGS) $(OBJS_AS_ARGS) $(USER_OBJS) $(LIBS) -Wl,-Map="labrafirm_new.map"  -Wl,-lm -mmcu=atxmega32a4u -Wl,--gc-sections -Wl,--relax 
	$(AVRDIR)/bin/avr-objcopy -O ihex -R .eeprom -R .fuse -R .lock -R .signature  "labrafirm_new.elf" "labrafirm_new.hex"
	$(AVRDIR)/bin/avr-objdump -h -S "labrafirm_new.elf" > "labrafirm_new.lss"
	$(AVRDIR)/bin/avr-objcopy -j .eeprom --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0 --no-change-warnings -O binary "labrafirm_new.elf" "labrafirm_new.eep" || exit 0
	$(AVRDIR)/bin/avr-objcopy -O srec -R .eeprom -R .fuse -R .lock -R .signature  "labrafirm_new.elf" "labrafirm_new.srec"
	$(AVRDIR)/bin/avr-size "labrafirm_new.elf"
	@echo Finished successfully: $@

clean:
	-rm -rf $(OBJS_AS_ARGS) $(EXECUTABLES)
	-rm -rf $(C_DEPS_AS_ARGS)
	-rm -rf labrafirm_new.hex
	-rm -rf labrafirm_new.lss
	-rm -rf labrafirm_new.eep
	-rm -rf labrafirm_new.srec
	-rm -rf labrafirm_new.elf labrafirm_new.a labrafirm_new.map
