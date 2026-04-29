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
SERVER_SRCS = server.cpp      \
              command.cpp     \
              vector_store.cpp\
              search.cpp

SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)

#Targets
.PHONY: all clean

all: vdb vdb-cli

# Link the server binary
vdb: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built: $@"

# Compile each .c to a .o
%.o: %.cpp
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# Explicit header dependencies 
# Tell make when to recompile an object if a header changes.
server.o:        server.cpp        vdb_interface.h command.h vector_store.h
command.o:       command.cpp       vdb_interface.h command.h vector_store.h
vector_store.o:  vector_store.cpp  vector_store.h
search.o:        search.cpp        vdb_interface.h vector_store.h

CLI_SRCS = vdb_cli.cpp
CLI_OBJS = $(CLI_SRCS:.cpp=.o)

vdb-cli: $(CLI_OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) -o $@ $^

# Clean 
clean:
	rm -f $(SERVER_OBJS) $(CLI_OBJS) vdb vdb-cli
	@echo "Cleaned."