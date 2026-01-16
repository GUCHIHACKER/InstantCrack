# Makefile para el ingestor

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fPIC
LDFLAGS = -ldl -llmdb -lpthread -lssl -lcrypto
INCLUDES = -Iinclude
SRC = src/main.c src/ingestor.c src/loader.c src/worker.c src/lmdb.c src/wordlist.c
OBJ = $(SRC:.c=.o)
BIN = ingestor

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
