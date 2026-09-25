# Milestone 1 ALSA stereo pass-through + optional GTK device picker
# (Raspberry Pi OS 64-bit; Pi 4 or Pi Zero 2 W)
#
#   make            GTK GUI if gtk+-3.0 dev files are installed, else CLI only
#   make NO_GUI=1   CLI only (recommended on Pi Zero 2 W / OS Lite)
CXX      ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
LDFLAGS  ?= -lasound -lm -pthread

SRCS = main.cpp alsa_io.cpp engine.cpp
BIN  = uvc_pass

ifndef NO_GUI
HAVE_GTK := $(shell pkg-config --exists gtk+-3.0 && echo 1)
endif

ifeq ($(HAVE_GTK),1)
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0) -DHAVE_GTK
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)
SRCS       += gui.cpp
endif

OBJS = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) -o $@ $^ $(LDFLAGS) $(GTK_LIBS)

%.o: %.cpp alsa_io.h engine.h gui.h
	$(CXX) $(CXXFLAGS) $(GTK_CFLAGS) -c -o $@ $<

clean:
	rm -f main.o alsa_io.o engine.o gui.o $(BIN)
