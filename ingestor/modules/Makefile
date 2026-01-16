# Makefile para compilar todos los módulos en modules/

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC -I../include
LDFLAGS = -shared -lssl -lcrypto
SRC := $(wildcard *.c)
SO := $(SRC:.c=.so)

all: $(SO)

%.so: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f *.so

.PHONY: all clean
