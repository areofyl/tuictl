CC = gcc
CFLAGS = -std=c99 -Wall -Wextra $(shell pkg-config --cflags ncursesw)
LDFLAGS = $(shell pkg-config --libs ncursesw)

SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
BIN = tuictl

$(BIN): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(BIN)

.PHONY: clean
