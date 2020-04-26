#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "util.h"

#define DRYDOCK_PORT "2048"
#define SERVER_PATH "./dry-dock-server"
#define VERIFICATION_MESSAGE "DRYDOCK"
#define VERIFICATION_RESPONSE "DRYDOCK"
#define DESTROY_MESSAGE "KILL"

static int SOCKFD = -1;

// forward declare functions
void initialize_server();
void destroy_server();
void create_server();

/**
 * Arguments:
 * init
 * destroy
 * create <PATH_TO_CONTAINERFILE>
 * */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: ./dry-dock <init, destroy, create> <options>\n");
        return 1;
    }

    if (strncmp(argv[1], "init", strlen("init")) == 0) {
        initialize_server();
    }
    else if (strncmp(argv[1], "destroy", strlen("destroy")) == 0) {
        destroy_server();
    }
    else if (strncmp(argv[1], "create", strlen("create")) == 0) {
        create_container();
    }
    else {
        fprintf(stderr, "Unrecognized command\n");
        return 1;
    }
    return 0;
}


int connect_and_verify_server() {
    /**
     *  tries to connect to dry-dock server, verifies it is the correct server with an initial message
     * return:
     * 0 on success, sets SOCKFD to the TCP socket
     * -1 on connect error
     * -2 on server exists but did not pass verification
    **/
    struct addrinfo hints, *servinfo;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rv;
    if ((rv = getaddrinfo(NULL, DRYDOCK_PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }
    if ((SOCKFD = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol)) == -1) {
        perror("socket");
        return -1;
    }
    if (connect(SOCKFD, servinfo->ai_addr, servinfo->ai_addrlen) == -1) {
        close(SOCKFD);
        perror("client: connect");
        return -1;
    }
    freeaddrinfo(servinfo); servinfo = NULL;
    if (write_all_to_socket(SOCKFD, VERIFICATION_MESSAGE, strlen(VERIFICATION_MESSAGE))) {
        fprintf(stderr, "Could not write verification message to server\n");
        return -2;
    }
    int vr_len = strlen(VERIFICATION_RESPONSE);
    char buf[strlen(vr_len)];
    int read;
    if ((read = read_all_from_socket(SOCKFD, buf, vr_len) != vr_len) || strncmp(buf, VERIFICATION_RESPONSE, vr_len) != 0) {
        fprintf(stderr, "Did not receive proper response from server\n");
        return -2;
    }
    return 0;
}


void initialize_server() {
    // replace process image with the servers
    pid_t child = fork();
    if (child == -1) {
        perror("fork");
        exit(1);
    }
    else if (child == 0) {
        execlp(SERVER_PATH, SERVER_PATH);
        // should never get here
        exit(1);
    }
    return; // parent can just finish
}

void destroy_server() {
    if (connect_and_verify_server() != 0) {
        fprintf(stderr, "Cannot desroy server\n");
        exit(1);
    }
    write_all_to_socket(SOCKFD, DESTROY_MESSAGE, strlen(DESTROY_MESSAGE));
}
