#ifndef COMMAND_H
#define COMMAND_H

#include "vdb_interface.h"

/*
  fd  : connected client socket (readable and writable)
  cfg : server configuration — dim and store pointer are the key fields
 */
void handle_client(int fd, const server_config_t *cfg);

#endif
