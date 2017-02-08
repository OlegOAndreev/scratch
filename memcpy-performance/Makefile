CFLAGS=-O3 -Wall -Wextra -std=c++11 -fomit-frame-pointer -mavx

memcpy-performance: memcpy-performance.cpp memcpy-impl.S memcpy-cpp-impl.cpp
	g++ $(CFLAGS) -o $@ $^
