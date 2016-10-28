IDIR=.
CC=gcc
CFLAGS=-std=c99

ODIR=obj
LIBS=-Wall -Werror

_DEPS = minishell.h
DEPS = $(patsubst %,$(IDIR)/%,$(_DEPS))

_OBJ = minishell.o
OBJ = $(patsubst %,$(ODIR)/%,$(_OBJ))

$(ODIR)/%.o : %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

minishell: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

run: minishell
	./minishell

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(IDIR)/*~

