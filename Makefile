SHELL := /bin/bash

CC=g++

# ----------------------------------------

SDIR=src
IDIR=include
ODIR=obj

# ----------------------------------------

_OBJ = main printer translator
_EXEC = ftc

# ----------------------------------------

ifneq ($(mode),release)
	CFLAGS=-I$(IDIR) -I$(ROSE_HOME)/include -std=gnu++0x -DDEBUG -O0 -g
else
	CFLAGS=-I$(IDIR) -I$(ROSE_HOME)/include -std=gnu++0x
endif
    
LFLAGS= -L$(ROSE_HOME)/lib -lrose

# ----------------------------------------

OBJ = $(patsubst %,$(ODIR)/%.o,$(_OBJ))

# ----------------------------------------

$(ODIR)/%.o: $(SDIR)/%.cpp
	$(CC) -c -o $@ $< $(CFLAGS)

all: $(OBJ)
	$(CC) -o $(_EXEC) $(OBJ) $(LFLAGS)
	gcc -c ftc_file_io.c
	ar -r libftc.a ftc_file_io.o
	
# ----------------------------------------

main.cpp : printer.hpp translator.hpp
printer.cpp : printer.hpp
translator.cpp : translator.hpp
	
# ----------------------------------------

.PHONY: clean

clean:
	rm -f $(ODIR)/*.o *~ core $(IDIR)/*~ ftc
	rm -f ftc_file_io.o libftc.a

