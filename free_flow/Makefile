CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -O2

TARGET  = free_flow
SRCS    = main.c generator.c solver.c
OBJS    = $(SRCS:.c=.o)
DEPS    = puzzle.h

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
