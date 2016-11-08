IDIR=.
CC=gcc
CFLAGS=-std=c99

SDIR=src
ODIR=obj
LIBS=-Wall -Werror

_DEPS = minishell.h parse_functions.h
DEPS = $(patsubst %,$(IDIR)/%,$(SDIR)/$(_DEPS))

_OBJ = minishell.o parse_functions.o
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

