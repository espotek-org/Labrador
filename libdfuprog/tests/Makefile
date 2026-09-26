# Builds and runs the libdfuprog unit test against the host's libusb-1.0.
#   make -C libdfuprog/tests
DFU_SRC := ../dfu-programmer/src
SRCS := test_args.c ../libdfuprog.c \
        $(DFU_SRC)/arguments.c $(DFU_SRC)/atmel.c $(DFU_SRC)/commands.c \
        $(DFU_SRC)/dfu.c $(DFU_SRC)/intel_hex.c $(DFU_SRC)/stm32.c $(DFU_SRC)/util.c
CFLAGS ?= -O1 -g -Wall
CFLAGS += -DHAVE_CONFIG_H -I.. -I$(DFU_SRC) $(shell pkg-config --cflags libusb-1.0)
LDLIBS := $(shell pkg-config --libs libusb-1.0) -pthread

.PHONY: all test clean
all: test

test_args: $(SRCS) ../libdfuprog.h
	$(CC) $(CFLAGS) $(SRCS) $(LDLIBS) -o $@

test: test_args
	./test_args

clean:
	rm -f test_args
