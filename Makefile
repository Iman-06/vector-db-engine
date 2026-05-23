# Makefile — Vector DB Engine
# USAGE:
#   make          build the server binary: ./vdb
#   make clean    remove all build artefacts
CC      = g++

# -std=gnu11   C11 plus GNU/POSIX extensions (needed for strtok_r, fdopen, …)
# -Wall -Wextra  turn on most useful warnings
# -pthread      required for pthreads on Linux (both compile and link)
# -g            debug symbols (remove for a release build)
CFLAGS  = -Wall -Wextra -pthread -g
CPPFLAGS = -std=c++17
LDFLAGS = -pthread

#  Server sources
SERVER_SRCS = server.cpp command.cpp vector_store.cpp ivf.cpp kmeans.cpp search.cpp snapshot.cpp

SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)

#Targets
.PHONY: all clean

all: vdb vdb-cli benchmark/benchmark

# Link the server binary
vdb: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built: $@"

# Compile each .cpp to a .o
%.o: %.cpp
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# Explicit header dependencies
# Tell make when to recompile an object if a header changes.
# metric.h is the base of the include chain: changing it triggers
# a rebuild of every module that uses distance functions.
server.o:        server.cpp        vdb_interface.h metric.h kmeans.h command.h vector_store.h ivf.h
command.o:       command.cpp       vdb_interface.h metric.h kmeans.h command.h vector_store.h ivf.h
vector_store.o:  vector_store.cpp  vector_store.h
search.o:        search.cpp        vdb_interface.h metric.h kmeans.h vector_store.h ivf.h
kmeans.o:        kmeans.cpp        kmeans.h        metric.h vector_store.h
ivf.o:           ivf.cpp           ivf.h           metric.h vector_store.h kmeans.h
CLI_SRCS = vdb_cli.cpp
CLI_OBJS = $(CLI_SRCS:.cpp=.o)

vdb-cli: $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

# Benchmark — standalone TCP client, no server headers needed
benchmark/benchmark: benchmark/benchmark.cpp
	@mkdir -p benchmark
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $<
	@echo "Built: $@"

# Clean
clean:
	rm -f $(SERVER_OBJS) $(CLI_OBJS) vdb vdb-cli benchmark/benchmark
	@echo "Cleaned."