# Makefile — Vector DB Engine
CC       = g++
CFLAGS   = -Wall -Wextra -pthread -g
CPPFLAGS = -std=c++17
LDFLAGS  = -pthread

SERVER_SRCS = server.cpp command.cpp vector_store.cpp ivf.cpp kmeans.cpp search.cpp normalize.cpp
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)

CLI_SRCS = vdb_cli.cpp
CLI_OBJS = $(CLI_SRCS:.cpp=.o)

BENCH_SRCS = benchmark/benchmark.cpp
BENCH_OBJS = $(BENCH_SRCS:.cpp=.o)

.PHONY: all clean benchmark

all: vdb vdb-cli benchmark/benchmark

benchmark: benchmark/benchmark

vdb: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built: $@"

vdb-cli: $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Built: $@"

benchmark/benchmark: $(BENCH_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^
	@echo "Built: $@"

%.o: %.cpp
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

server.o:       server.cpp       vdb_interface.h command.h vector_store.h
command.o:      command.cpp      vdb_interface.h command.h vector_store.h ivf.h normalize.h
vector_store.o: vector_store.cpp vector_store.h
search.o:       search.cpp       vdb_interface.h vector_store.h
kmeans.o:       kmeans.cpp       kmeans.h vector_store.h
ivf.o:          ivf.cpp          ivf.h vector_store.h kmeans.h
normalize.o:    normalize.cpp    normalize.h
benchmark/benchmark.o: benchmark/benchmark.cpp

clean:
	rm -f $(SERVER_OBJS) $(CLI_OBJS) $(BENCH_OBJS) vdb vdb-cli benchmark/benchmark
	@echo "Cleaned."