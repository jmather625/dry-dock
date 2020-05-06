#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include "utils.h"

int write_all_to_socket(int socket, const void *buffer, int count) {
    int total = 0;
    while (total != (int) count) {
        int sent = send(socket, buffer + total, count - total, 0);
        if (sent == 0)
            return -1;
        else if (sent == -1) {
            if (errno == EINTR)
                continue;
            perror("send");
            return -1;
        }
        total += (int) sent;
    }
    return total;
}

int read_all_from_socket(int socket, void *buffer, int count) {
    int total = 0;
    int recvd;
    while (count != total) {
        recvd = recv(socket, buffer + total, count - total, 0);
        if (recvd == 0) {
            return total;
        }
        else if (recvd == -1) {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                return total;
            else {
                perror("recv");
                return -1;
            }
        }
        total += recvd;
    }
    return total;
}
