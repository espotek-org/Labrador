CXX = clang++
CXXFLAGS = -std=c++17 -O2 $(shell pkg-config --cflags libusb-1.0)
LDFLAGS = $(shell pkg-config --libs libusb-1.0)

ifeq ($(shell uname),Darwin)
LDFLAGS += -framework IOKit -framework CoreFoundation -framework Security
endif

aio_transport_test: aio_transport_test.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f aio_transport_test

.PHONY: clean
