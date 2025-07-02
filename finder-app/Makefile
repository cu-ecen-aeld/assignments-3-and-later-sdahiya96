CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Wextra -O2

SRCS := writer.c
OBJS := $(SRCS:.c=.o)
TARGET := writer

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@


.PHONY: clean
clean:
	rm -f $(TARGET) $(OBJS)

