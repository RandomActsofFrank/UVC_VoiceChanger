# Milestone 1 ALSA stereo pass-through + web device picker
# (Raspberry Pi OS 64-bit; Pi Zero 2 W or Pi 4)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS  ?= -lasound -lm -pthread

SRCS = main.cpp alsa_io.cpp engine.cpp web.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = uvc_pass

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp alsa_io.h engine.h web.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)
