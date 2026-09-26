# ALSA stereo capture -> voice effects -> playback, with web config
# (Raspberry Pi OS 64-bit; Pi Zero 2 W or Pi 4)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS  ?= -lasound -lm -pthread

SRCS = main.cpp alsa_io.cpp engine.cpp web.cpp dsp.cpp character.cpp tuning.cpp biquad.cpp presets.cpp settings.cpp ptt.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = uvc_pass

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp alsa_io.h engine.h web.h dsp.h character.h tuning.h biquad.h presets.h settings.h ptt.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Push-to-talk logic tests (no hardware needed)
ptt_test: ptt_test.o ptt.o
	$(CXX) -o $@ $^ -lm -pthread

test: ptt_test
	./ptt_test

clean:
	rm -f $(OBJS) $(BIN) ptt_test.o ptt_test
