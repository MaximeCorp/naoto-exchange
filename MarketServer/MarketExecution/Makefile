CC=g++

CFLAGS= -std=c++20 -Wall -Werror -Wextra -Iinclude -fsanitize=address

SRC=$(shell find . -type f -name '*.cpp')
OBJ=$(patsubst %.cpp,%.o,$(SRC))

all: $(OBJ)
	$(CC) $(CFLAGS) -o MarketExecution $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) MarketExecution

.PHONY: all clean
