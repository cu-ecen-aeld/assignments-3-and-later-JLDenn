#I (Justin Denning) wrote most of this file for ECEN5813 last semester, but modified it to fit 
#this assignment (assignment 2) for ECEN5713 spring 2025

EXEC     = aesdsocket
CC       ?= $(CROSS_COMPILE)gcc

CFLAGS   ?= -Wall -Werror 

LDFLAGS  ?= 

SRC      = $(wildcard *.c)
OBJ      = $(SRC:.c=.o)

all: $(EXEC)
default: $(EXEC)

${EXEC}: $(OBJ)
		$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c %.h main.h
		$(CC) -o $@ -c $< $(CFLAGS)

.PHONY: clean

clean:
		@rm -rf *.o ${EXEC}

