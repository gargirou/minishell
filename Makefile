CC      = gcc
CFLAGS  = -std=gnu99 -Wall -Werror -Wextra

SDIR = src
ODIR = obj

SRCS = $(SDIR)/minishell.c \
       $(SDIR)/parse.c     \
       $(SDIR)/execute.c   \
       $(SDIR)/builtins.c

OBJS = $(patsubst $(SDIR)/%.c,$(ODIR)/%.o,$(SRCS))

DEPS = $(SDIR)/minishell.h

$(ODIR)/%.o: $(SDIR)/%.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

minishell: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

run: minishell
	./minishell

.PHONY: clean
clean:
	rm -f $(ODIR)/*.o minishell *~ core
