#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include "msg.h"

void Usage(char *progname);
int Listen(char *portnum, int *sock_family);
void *HandleClient(void *client_fd_parameter);
void handle_put_request(struct msg received_message, struct msg *response);
void handle_get_request(struct msg received_message, struct msg *response);
int find_record(uint32_t id, struct record *entry);


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

        // new thread per client added to queue
        pthread_t client_thread_id;
        int create_result = pthread_create(&client_thread_id, NULL, HandleClient, (void *)(intptr_t)client_fd);
        if (create_result != 0) {
            printf("Error creating thread: %s\n", strerror(create_result));
            continue;
        }

        int detach_result = pthread_detach(client_thread_id);
        if (detach_result != 0) {
            printf("Error detaching thread: %s\n", strerror(detach_result));
            continue;
        }
    }

    // Close socket
    close(listen_fd);
    return EXIT_SUCCESS;
}

void *HandleClient(void *client_fd_parameter) {
    int client_fd = (int)(intptr_t)client_fd_parameter;
    printf("Socket [%d] connected\n", client_fd);

    while (1) {
        struct msg received_message;

        ssize_t request_from_client = read(client_fd, &received_message, sizeof(received_message));
        if (request_from_client == 0) {
            printf("Disconnected: Socket [%d]\n", client_fd);
            break;
        }
        if (request_from_client == -1) {
            if ((errno == EAGAIN) || (errno == EINTR)) {
                continue;
            }

            printf("Error on client Socket [%d] :%s\n", client_fd, strerror(errno));
            break;
        }

        struct msg response;

        switch (received_message.type) {
            case PUT:
                handle_put_request(received_message, &response);
                break;
            case GET:
                handle_get_request(received_message, &response);
                break;
            default:
                printf("Invalid request received\n");
                response.type = FAIL;
                break;
        }

        // Send the response back to the client
        ssize_t response_to_client = write(client_fd, &response, sizeof(response));
        if (response_to_client < sizeof(response)) {
            printf("Error writing response to client Socket [%d]: %s\n", client_fd, strerror(errno));
            continue;
        }
    }

    close(client_fd);
    return NULL;
}

void handle_put_request(struct msg received_message, struct msg *response) {
    printf("PUT request received:\n");
    printf("Name: %s\n", received_message.rd.name);
    printf("ID: %u\n", received_message.rd.id);

    int32_t db_fd = open("db", O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    if (db_fd < 0) {
        perror("Error opening the database file");
        response->type = FAIL;
        return;
    }

    off_t seek_res = lseek(db_fd, 0, SEEK_END); // to the end of the file
    if (seek_res == (off_t)-1) {
        perror("Error seeking to the end of the database file");
        response->type = FAIL;
        close(db_fd);
        return;
    }

    ssize_t write_res = write(db_fd, &received_message.rd, sizeof(struct record));
    if (write_res < sizeof(struct record)) {
        perror("Error writing to the database file");
        response->type = FAIL;
        close(db_fd);
        return;
    }

    response->type = SUCCESS;
    response->rd = received_message.rd;
    close(db_fd);
}

void handle_get_request(struct msg received_message, struct msg *response) {
    printf("GET request received:\n");
    printf("ID: %u\n", received_message.rd.id);

    struct record entry;
    int found = find_record(received_message.rd.id, &entry);

    if (found) {
        response->type = SUCCESS;
        response->rd = entry;
    } else {
        printf("Student record not found.\n");
        response->type = FAIL;
    }
}

int find_record(uint32_t id, struct record *entry) {
    // Check if the database file exists
    if (access("db", F_OK) != 0) {
        printf("Database file does not exist.\n");
        return 0;
    }

    int32_t db_fd = open("db", O_RDONLY);
    if (db_fd < 0) {
        perror("Error opening the database file");
        return 0;
    }

    off_t file_size = lseek(db_fd, 0, SEEK_END);
    off_t current_position = (off_t)(file_size - sizeof(struct record));

    int found = 0;
    while (current_position >= 0) {
        lseek(db_fd, current_position, SEEK_SET);
        read(db_fd, entry, sizeof(struct record));

        if (entry->id == id) {
            found = 1;
            break;
        }

        current_position -= sizeof(struct record);
    }

    close(db_fd);
    return found;
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