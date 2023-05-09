#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "msg.h"

void Usage(char *progname);
int LookupName(char *name, unsigned short port, struct sockaddr_storage *ret_addr, size_t *ret_addrlen);
int Connect(const struct sockaddr_storage *addr, const size_t addrlen, int *ret_fd);
void process_user_input(int *choice, struct msg *message);
void handle_server_response(struct msg *message, struct msg *response);

int main(int argc, char **argv) {
    if (argc != 3) {
        Usage(argv[0]);
    }

    unsigned short port = 0;
    if (sscanf(argv[2], "%hu", &port) != 1) {
        Usage(argv[0]);
    }

    // Get an appropriate sockaddr structure.
    struct sockaddr_storage addr;
    size_t addrlen;
    if (!LookupName(argv[1], port, &addr, &addrlen)) {
        Usage(argv[0]);
    }

    // Connect to the remote host.
    int socket_fd;
    if (!Connect(&addr, addrlen, &socket_fd)) {
        Usage(argv[0]);
    }

    int choice;
    struct msg message;
    struct msg response;

    while (1) {
        process_user_input(&choice, &message);
        if (choice != PUT && choice != GET) {
            break;
        }

        // Send the message to the remote host.
        ssize_t req = write(socket_fd, &message, sizeof(message));
        if (req == -1) {
            perror("client write");
            close(socket_fd);
            return EXIT_FAILURE;
        }
        else if (req < sizeof(message)) {
            fprintf(stderr, "partial/failed write\n");
            close(socket_fd);
            return EXIT_FAILURE;
        }

        // Read the response from the remote host.
        ssize_t res = read(socket_fd, &response, sizeof(response));
        if (res == 0) {
            printf("socket closed prematurely \n");
            close(socket_fd);
            return EXIT_FAILURE;
        }
        if (res == -1) {
            if (errno == EINTR) {
                continue;
            }
            printf("socket read failure \n");
            close(socket_fd);
            return EXIT_FAILURE;
        }

        handle_server_response(&message, &response);
    }

    // Clean up.
    close(socket_fd);
    return EXIT_SUCCESS;
}

void get_name(struct msg *message) {
    int valid_input = 0;

    while (!valid_input) {
        printf("Enter the name: ");
        if (fgets(message->rd.name, MAX_NAME_LENGTH, stdin) == NULL) {
            printf("Error: input string is NULL\n");
            continue;
        }
        message->rd.name[strcspn(message->rd.name, "\n")] = 0;

        if (strlen(message->rd.name) == 0) {
            printf("Name cannot be empty\n");
        } else {
            valid_input = 1;
        }
    }
}

void get_id(struct msg *message) {
    char input[256];
    char *endptr;
    int valid_input = 0;

    while (!valid_input) {
        printf("Enter the id: ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("Error: input string is NULL\n");
            continue;
        }

        if (input[0] == '\n') {
            printf("ID must not be empty\n");
            continue;
        }

        message->rd.id = strtol(input, &endptr, 10);

        if (endptr == input || *endptr != '\n') {
            printf("ID must be a number\n");
        } else {
            valid_input = 1;
        }
    }
}

void process_user_input(int* choice, struct msg *message) {
    printf("Enter your choice (1 to put, 2 to get, 0 to quit): ");
    scanf("%d", choice);
    getchar();

    switch (*choice) {
        case 1:
            message->type = PUT;
            get_name(message);
            get_id(message);
            break;
        case 2:
            message->type = GET;
            get_id(message);
            break;
        default:
            return;
    }
}

void handle_server_response(struct msg *message, struct msg *response) {
    if (response->type == SUCCESS) {
        switch (message->type) {
            case PUT:
                printf("Put success.\n");
                break;
            case GET:
                printf("name: %s\nid: %u\n", response->rd.name, response->rd.id);
                break;
            default:
                printf("Invalid request.\n");
        }
    }
    else if (response->type == FAIL) {
        printf("Operation failed\n");
    }
    else {
        printf("Invalid response\n");
    }
}

void Usage(char *progname) {
    printf("usage: %s  hostname port \n", progname);
    exit(EXIT_FAILURE);
}

int LookupName(char *name, unsigned short port, struct sockaddr_storage *ret_addr, size_t *ret_addrlen) {
    struct addrinfo hints, *results;
    int retval;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // Do the lookup by invoking getaddrinfo().
    if ((retval = getaddrinfo(name, NULL, &hints, &results)) != 0) {
        printf("getaddrinfo failed: %s", gai_strerror(retval));
        return 0;
    }

    // Set the port in the first result.
    if (results->ai_family == AF_INET) {
        struct sockaddr_in *v4addr =
                (struct sockaddr_in *) (results->ai_addr);
        v4addr->sin_port = htons(port);
    } else if (results->ai_family == AF_INET6) {
        struct sockaddr_in6 *v6addr =
                (struct sockaddr_in6 *) (results->ai_addr);
        v6addr->sin6_port = htons(port);
    } else {
        printf("getaddrinfo failed to provide an IPv4 or IPv6 address \n");
        freeaddrinfo(results);
        return 0;
    }

    // Return the first result.
    assert(results != NULL);
    memcpy(ret_addr, results->ai_addr, results->ai_addrlen);
    *ret_addrlen = results->ai_addrlen;

    // Clean up.
    freeaddrinfo(results);
    return 1;
}

int Connect(const struct sockaddr_storage *addr, const size_t addrlen, int *ret_fd) {
    // Create the socket.
    int socket_fd = socket(addr->ss_family, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        printf("socket() failed: %s", strerror(errno));
        return 0;
    }

    // Connect the socket to the remote host.
    int res = connect(socket_fd,
                      (const struct sockaddr *) (addr),
                      addrlen);
    if (res == -1) {
        printf("connect() failed: %s", strerror(errno));
        return 0;
    }

    *ret_fd = socket_fd;
    return 1;
}
