CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -D_GNU_SOURCE -O2
DBGFLAGS = -Wall -Wextra -pthread -D_GNU_SOURCE -g -fsanitize=address,undefined

SRCS = main.c \
       common.h \
       net_utils.c \
       peer_list.c \
       load_monitor.c \
       discovery.c \
       worker.c \
       dispatcher.c

OBJS = net_utils.o \
       peer_list.o \
       load_monitor.o \
       discovery.o \
       worker.o \
       dispatcher.o \
       main.o

TARGET = p2p_node

.PHONY: all debug clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

debug: CFLAGS := $(DBGFLAGS)
debug: $(OBJS)
	$(CC) $(DBGFLAGS) -o $(TARGET)_debug $^

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_debug /tmp/p2p_*.c /tmp/p2p_*_bin
