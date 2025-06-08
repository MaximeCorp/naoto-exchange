CC=g++
CFLAGS=-Wall -Werror -Wextra -fsanitize=address -g
SRC=$(shell find . -type f -name '*.cpp')
OBJ=$(patsubst %.cpp,%.o,$(SRC))

all: $(OBJ)
	$(CC) $(CFLAGS) -o marketExecution $^

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) marketExecution

.PHONY: all clean
