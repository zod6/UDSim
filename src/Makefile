BIN ?= udsim
CC = g++
CFLAGS = -std=gnu++11 -Iinclude -Wall -O2
X86_PARAMS ?= -DSDL -I/usr/include/SDL2
LDFLAGS ?= -lSDL2 -lSDL2_image -lSDL2_ttf
#OBJS = main.o gui.o logparser.o module.o gamedata.o can.o canframe.o icon-button.o configparser.o input.o
SRCS = $(wildcard *.cc) $(wildcard *.c)
OBJS := $(addsuffix .o,$(basename $(SRCS)))
#OBJS = $(patsubst %.cc, %.o, $(SRCS))

DEP := $(OBJS:.o=.d)

all: $(BIN)

-include $(DEP)

$(BIN): $(OBJS)
	$(TOOLCHAIN)$(CC) $(X86_PARAMS) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $(BIN)

%.o: %.cc Makefile
	$(TOOLCHAIN)$(CC) $(X86_PARAMS) -c -MMD $(CFLAGS) -o $@ $<

%.o: %.c
	$(TOOLCHAIN)$(CC) $(X86_PARAMS) -c -MMD $(CFLAGS) -o $@ $<

clean:
	rm -f $(BIN) *.o *.d

x86:
	make clean
	make all TOOLCHAIN=

nogui: X86_PARAMS =
nogui: LDFLAGS = 
nogui: all

mipsel:
	make clean
	make all TOOLCHAIN="/opt/mipsel-linux/bin/mipsel-linux-" X86_PARAMS="-mips32r2" BIN="$(BIN)-mipsel" LDFLAGS=""

upload:
	scp $(BIN)-mipsel can-lan:/data

