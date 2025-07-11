CC ?= gcc
CFLAGS ?= -Wall -Wextra -g
LDFLAGS ?=

TARGET = aesdsocket

SRCS = aesdsocket.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(DATA_FILE)
