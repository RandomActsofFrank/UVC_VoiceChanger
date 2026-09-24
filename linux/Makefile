# Milestone 1 ALSA pass-through engine (Raspberry Pi OS 64-bit)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS  ?= -lasound -lm

SRCS = main.cpp alsa_io.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = uvc_pass

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp alsa_io.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)
