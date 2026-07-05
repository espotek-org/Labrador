CXX = clang++
CXXFLAGS = -std=c++17 -O2 $(shell pkg-config --cflags libusb-1.0)
LDFLAGS = $(shell pkg-config --libs libusb-1.0)

ifeq ($(shell uname),Darwin)
LDFLAGS += -framework IOKit -framework CoreFoundation -framework Security
endif

all: aio_transport_test aio_peek

aio_transport_test: aio_transport_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Reads the firmware's 0xab debug state (transport, callback counters)
# via a device-level control transfer - no interface claim, so it can
# observe a running app's streaming without disturbing it.
aio_peek: aio_peek.c
	$(CC) -O2 $(shell pkg-config --cflags libusb-1.0) -o $@ $< $(LDFLAGS)

clean:
	rm -f aio_transport_test aio_peek

.PHONY: all clean
