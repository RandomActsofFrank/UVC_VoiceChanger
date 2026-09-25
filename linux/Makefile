# ALSA stereo capture -> voice effects -> playback, with web config
# (Raspberry Pi OS 64-bit; Pi Zero 2 W or Pi 4)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS  ?= -lasound -lm -pthread

SRCS = main.cpp alsa_io.cpp engine.cpp web.cpp dsp.cpp character.cpp biquad.cpp presets.cpp settings.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = uvc_pass

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp alsa_io.h engine.h web.h dsp.h character.h biquad.h presets.h settings.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)
