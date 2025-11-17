CC=g++

CFLAGS=-Wall -Werror -Wextra -Iinclude -fsanitize=address

SRC=$(shell find . -type f -name '*.cpp')
OBJ=$(patsubst %.cpp,%.o,$(SRC))

all: $(OBJ)
	$(CC) $(CFLAGS) -o socketGateway $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) socketGateway

.PHONY: all clean
