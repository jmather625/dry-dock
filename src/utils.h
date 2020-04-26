#pragma once

/**
 * Writes count bytes from buffer
 * returns -1 on error, and total number of bytes written on success
 * */
int write_all_to_socket(int socket, const void *buffer, int count);

/**
 * Reads up to count bytes from the socket
 * socket MUST be non-blocking
 * returns -1 on error, and total number of bytes read on success
 * */
int read_all_from_socket(int socket, void *buffer, int count);
