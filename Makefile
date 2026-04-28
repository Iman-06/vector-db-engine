# Makefile — Vector DB Engine

# USAGE:
#   make          build the server binary: ./vdb
#   make clean    remove all build artefacts




# WHEN MEMBER 3 DELIVERS search.c:
#   Replace search_stub.c with search.c in SERVER_SRCS below.
#
# WHEN MEMBER 3 DELIVERS vdb_cli.c:
#   Uncomment the vdb-cli rules at the bottom.



CC      = gcc

# -std=gnu11   C11 plus GNU/POSIX extensions (needed for strtok_r, fdopen, …)
# -Wall -Wextra  turn on most useful warnings
# -pthread      required for pthreads on Linux (both compile and link)
# -g            debug symbols (remove for a release build)
CFLAGS  = -std=gnu11 -Wall -Wextra -pthread -g

LDFLAGS = -pthread

#  Server sources
SERVER_SRCS = server.c        \
              command.c       \
              vector_store.c  \
              search_stub.c

SERVER_OBJS = $(SERVER_SRCS:.c=.o)

#Targets
.PHONY: all clean

all: vdb

# Link the server binary
vdb: $(SERVER_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^
	@echo "Built: $@"

# Compile each .c to a .o
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Explicit header dependencies 
# Tell make when to recompile an object if a header changes.
server.o:        server.c        vdb_interface.h command.h vector_store.h
command.o:       command.c       vdb_interface.h command.h vector_store.h
vector_store.o:  vector_store.c  vector_store.h
search_stub.o:   search_stub.c   vdb_interface.h vector_store.h

# ── Member 3: client binary (uncomment when vdb_cli.c is ready) ───────
# CLI_SRCS = vdb_cli.c
# vdb-cli: $(CLI_SRCS)
# 	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Clean 
clean:
	rm -f $(SERVER_OBJS) vdb vdb-cli
	@echo "Cleaned."
