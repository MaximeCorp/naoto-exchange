CC=g++

CXXFLAGS=-std=c++20 -Wall -Werror -Wextra -Iinclude
LDFLAGS =-fsanitize=address -letcd-cpp-api -lgrpc++ -lgrpc -lprotobuf -lcpprest -lssl -lcrypto -lz -lpthread


SRC=$(shell find src -type f -name '*.cpp')
OBJ=$(patsubst %.cpp,%.o,$(SRC))
BIN=SocketGateway

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CXXFLAGS) $(OBJ) -o $(BIN) $(LDFLAGS)

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
