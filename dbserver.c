#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <pthread.h>
#include <fcntl.h>
#include "msg.h"

typedef struct {
    int fd;
    struct sockaddr addr;
    size_t addrlen;
    int sock_family;
} client_connection;

void Usage(char *progname);
int Listen(char *portnum, int *sock_family);
void *HandleClient(void *client_parameters);
int main(int argc, char **argv) {
    // Expect the port number as a command line argument.
    if (argc != 2) {
        Usage(argv[0]);
    }

    int sock_family;
    int listen_fd = Listen(argv[1], &sock_family);
    if (listen_fd <= 0) {
        // We failed to bind/listen to a socket.  Quit with failure.
        printf("Couldn't bind to any addresses.\n");
        return EXIT_FAILURE;
    }

    // Loop forever, accepting a connection from a client and doing
    // an echo trick to it.
    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(listen_fd,(struct sockaddr *) (&client_addr), &client_addr_len);
        if (client_fd < 0) {
            if ((errno == EINTR) || (errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                continue;
            }
            printf("Failure on accept:%s \n ", strerror(errno));
            break;
        }

        // Create a new thread to handle the client
        client_connection *current_client = (client_connection *) malloc(sizeof(client_connection));
        current_client->fd = client_fd;
        current_client->addr = *(struct sockaddr *) (&client_addr);
        current_client->addrlen = client_addr_len;
        current_client->sock_family = sock_family;

        pthread_t thread_id;
        pthread_create(&thread_id, NULL, HandleClient, (void *)current_client);
        pthread_detach(thread_id);
    }

    // Close socket
    close(listen_fd);
    return EXIT_SUCCESS;
}
void Usage(char *progname) {
    printf("usage: %s port \n", progname);
    exit(EXIT_FAILURE);
}
int Listen(char *portnum, int *sock_family) {

    // Populate the "hints" addrinfo structure for getaddrinfo().
    // ("man addrinfo")
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;       // IPv6 (also handles IPv4 clients)
    hints.ai_socktype = SOCK_STREAM;  // stream
    hints.ai_flags = AI_PASSIVE;      // use wildcard "in6addr_any" address
    hints.ai_flags |= AI_V4MAPPED;    // use v4-mapped v6 if no v6 found
    hints.ai_protocol = IPPROTO_TCP;  // tcp protocol
    hints.ai_canonname = NULL;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;

    // getaddrinfo() returns a list of
    // address structures via the output parameter "result".
    struct addrinfo *result;
    int res = getaddrinfo(NULL, portnum, &hints, &result);

    // Did addrinfo() fail?
    if (res != 0) {
        printf("getaddrinfo failed: %s", gai_strerror(res));
        return -1;
    }

    // Loop through the returned address structures until we are able
    // to create a socket and bind to one.  The address structures are
    // linked in a list through the "ai_next" field of result.
    int listen_fd = -1;
    struct addrinfo *rp;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        listen_fd = socket(rp->ai_family,
                           rp->ai_socktype,
                           rp->ai_protocol);
        if (listen_fd == -1) {
            // Creating this socket failed.  So, loop to the next returned
            // result and try again.
            printf("socket() failed:%s \n ", strerror(errno));
            listen_fd = -1;
            continue;
        }

        // Configure the socket; we're setting a socket "option."  In
        // particular, we set "SO_REUSEADDR", which tells the TCP stack
        // so make the port we bind to available again as soon as we
        // exit, rather than waiting for a few tens of seconds to recycle it.
        int optval = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &optval, sizeof(optval));

        // Try binding the socket to the address and port number returned
        // by getaddrinfo().
        if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            // Bind worked!  Print out the information about what
            // we bound to.
            //PrintOut(listen_fd, rp->ai_addr, rp->ai_addrlen);

            // Return to the caller the address family.
            *sock_family = rp->ai_family;
            break;
        }

        // The bind failed.  Close the socket, then loop back around and
        // try the next address/port returned by getaddrinfo().
        close(listen_fd);
        listen_fd = -1;
    }

    // Free the structure returned by getaddrinfo().
    freeaddrinfo(result);

    // If we failed to bind, return failure.
    if (listen_fd == -1)
        return listen_fd;

    // Success. Tell the OS that we want this to be a listening socket.
    if (listen(listen_fd, SOMAXCONN) != 0) {
        printf("Failed to mark socket as listening:%s \n ", strerror(errno));
        close(listen_fd);
        return -1;
    }

    // Return to the client the listening file descriptor.
    return listen_fd;
}

void *HandleClient(void *client_parameters) {
    client_connection *client_data = (client_connection *)client_parameters;
    int client_fd = client_data->fd;

    printf("-- Socket [%d] connected -- \n", client_fd);

    while (1) {
        struct msg received_msg;
        ssize_t res = read(client_fd, &received_msg, sizeof(received_msg));

        if (res == 0) {
            printf("-- Disconnected: Socket [%d] --\n", client_fd);
            break;
        }

        if (res == -1) {
            if ((errno == EAGAIN) || (errno == EINTR))
                continue;

            printf("-- Error on client Socket [%d] :%s --\n", client_fd, strerror(errno));
            break;
        }

        struct msg response;

        if (received_msg.type == PUT) {
            printf(":PUT request received:\n");
            printf("Name: %s\n", received_msg.rd.name);
            printf("ID: %u\n", received_msg.rd.id);
            response.type = SUCCESS;

            int32_t db_fd = open("db", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
            if (db_fd < 0) {
                perror("Error opening the database file");
                return NULL;
            }

            struct record student_record;
            strcpy(student_record.name, received_msg.rd.name);
            student_record.id = received_msg.rd.id;
            lseek(db_fd, sizeof(struct record) * student_record.id, SEEK_SET);
            write(db_fd, &student_record, sizeof(struct record));

            close(db_fd);

        } else if (received_msg.type == GET) {
            printf("GET request received:\n");
            printf("ID: %u\n", received_msg.rd.id);
            response.type = SUCCESS;

            int32_t db_fd = open("db", O_RDONLY);
            if (db_fd < 0) {
                perror("Error opening the database file");
                return NULL;
            }

            struct record student_record;
            int32_t id = received_msg.rd.id;
            lseek(db_fd, sizeof(struct record) * id, SEEK_SET);
            read(db_fd, &student_record, sizeof(struct record));

            strcpy(response.rd.name, student_record.name);
            response.rd.id = student_record.id;

            close(db_fd);

        } else {
            printf("-- Invalid request received --\n");
            response.type = FAIL;
        }

        // Send the response back to the client
        write(client_fd, &response, sizeof(response));
    }

    close(client_fd);
    free(client_data);

    return NULL;
}
