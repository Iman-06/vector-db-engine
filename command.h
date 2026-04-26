/*
 * command.h — Command parsing and dispatch (Member 1).
 */

#ifndef COMMAND_H
#define COMMAND_H

#include "vdb_interface.h"

/*
 * handle_client — main command loop for one connected client.
 *
 * Reads commands line-by-line from the socket `fd`, parses them,
 * calls the appropriate Member 2 / Member 3 functions, and writes
 * responses back to the client.
 *
 * Returns when the client sends QUIT or the connection drops.
 * The caller is responsible for closing `fd` after this returns.
 *
 * fd  : connected client socket (readable and writable)
 * cfg : server configuration — dim and store pointer are the key fields
 */
void handle_client(int fd, const server_config_t *cfg);

#endif /* COMMAND_H */
