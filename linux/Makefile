# Milestone 1 ALSA stereo pass-through + GTK device picker (Raspberry Pi OS 64-bit)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)
LDFLAGS  ?= -lasound -lm -pthread $(GTK_LIBS)

SRCS = main.cpp alsa_io.cpp engine.cpp gui.cpp
OBJS = $(SRCS:.cpp=.o)
BIN  = uvc_pass

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.cpp alsa_io.h engine.h gui.h
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(BIN)
