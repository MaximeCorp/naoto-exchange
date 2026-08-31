CXX := g++

DPDK_CFLAGS  := $(shell pkg-config --cflags libdpdk)
DPDK_LDFLAGS := $(shell pkg-config --static --libs libdpdk)

ABSL_CFLAGS  := $(shell pkg-config --cflags absl_flat_hash_map absl_flat_hash_set)
ABSL_LDFLAGS := $(shell pkg-config --libs absl_flat_hash_map absl_flat_hash_set)

CXXFLAGS := -std=c++20 -Wall -Werror -Wextra -Wno-volatile -Iinclude -fsanitize=address $(DPDK_CFLAGS) $(ABSL_CFLAGS)
LDFLAGS = -letcd-cpp-api -lgrpc++ -lgrpc -lprotobuf -lcpprest -lssl -lcrypto -lz -lpthread -fsanitize=address $(DPDK_LDFLAGS) $(ABSL_LDFLAGS)

SRC=$(shell find src -type f -name '*.cpp')
OBJ=$(patsubst %.cpp,%.o,$(SRC))
BIN=OrderGatewayService

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) -o $(BIN) $(OBJ) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
