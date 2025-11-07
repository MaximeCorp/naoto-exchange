CC=g++
:q


KAFKA_CFLAGS=$(shell pkg-config --cflags rdkafka++)
KAFKA_LDLIBS=$(shell pkg-config --libs rdkafka++)

CFLAGS=-Wall -Werror -Wextra -I/usr/include/librdkafka/ -Iinclude $(KAFKA_CFLAGS)
LDFLAGS=$(KAFKA_LDLIBS)-lrocksdb -lstdc++ -lpthread -lsnappy -lz -lbz2

SRC=$(shell find . -type f -name '*.cpp')
OBJ=$(patsubst %.cpp,%.o,$(SRC))

all: $(OBJ)
	$(CC) $(CFLAGS) -o marketExecution $^ $(LDFLAGS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) marketExecution

.PHONY: all clean
