CC = gcc
CFLAGS = -O3 -Wall -Wextra -pedantic -std=c11 -pthread
LDFLAGS = -pthread -lm
DEBUG_FLAGS = -g -O0 -DDEBUG

SRCS = threadpool.c matrix.c benchmark.c
OBJS = $(SRCS:.c=.o)
HEADERS = threadpool.h matrix.h

TARGET = benchmark

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: all clean
