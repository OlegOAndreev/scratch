UNAME := $(shell uname -s)
ifeq ($(UNAME),Linux)
    EXTRA_LDFLAGS += -lrt
endif

all:
	g++ -Wall -Wextra -O0 -std=c++11 timer-resolution.cpp -o timer-resolution -pthread $(EXTRA_LDFLAGS)
