#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>

#include "stub.h"

#define PERCEPTION_NODE "P1"
#define SUPERVISOR_NODE "P2"
#define ACTUATOR_NODE "P3"

enum {
    MIN_PORT = 1024,
    MAX_PORT = 65535,
    BACKLOG = 128,
    MAX_CLIENTS = 2
};

unsigned int lamport_clock = 0;
char node_name[NODE_NAME_LENGTH];

// Protect shared state against race conditions.
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t server_thread, client_thread;

int socket_p1 = -1, socket_p2 = -1, socket_p3 = -1;

// -- LAMPORT CLOCKS --
int get_clock_lamport(void) {
    int current_clock;

    pthread_mutex_lock(&mutex);
    current_clock = lamport_clock;
    pthread_mutex_unlock(&mutex);

    return current_clock;
}

void update_clock_lamport(int counter) {
    pthread_mutex_lock(&mutex);
    lamport_clock = counter;
    pthread_mutex_unlock(&mutex);
}

char *operation_to_string(enum operations action) {
    switch (action) {
        case READY_TO_SHUTDOWN:
            return "READY_TO_SHUTDOWN";
        case SHUTDOWN_NOW:
            return "SHUTDOWN_NOW";
        case SHUTDOWN_ACK:
            return "SHUTDOWN_ACK";
        default:
            return "UNKNOWN";
    }
}

void send_message(struct message message, const char *destination) {
    int sock = -1;
    ssize_t result;
    size_t bytes_sent = 0;
    char *buffer = (char *)&message;

    pthread_mutex_lock(&mutex);

    // Select the destination socket.
    if (strcmp(destination, PERCEPTION_NODE) == 0) {
        sock = socket_p1;
    } else if (strcmp(destination, SUPERVISOR_NODE) == 0) {
        sock = socket_p2;
    } else if (strcmp(destination, ACTUATOR_NODE) == 0) {
        sock = socket_p3;
    }

    if (sock < 0) {
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "No existe conexión con %s\n", destination);
        return;
    }

    // Update the Lamport clock before sending.
    lamport_clock++;
    message.clock_lamport = lamport_clock;

    // Send all bytes of the message.
    while (bytes_sent < sizeof(message)) {
        result = send(sock, buffer + bytes_sent,
                      sizeof(message) - bytes_sent, 0);

        if (result < 0) {
            perror("send failed");
            pthread_mutex_unlock(&mutex);
            return;
        }

        if (result == 0) {
            fprintf(stderr, "No se pudo completar el envío\n");
            pthread_mutex_unlock(&mutex);
            return;
        }

        bytes_sent += result;
    }

    // Required output format.
    fprintf(stdout, "%s, %u, SEND, %s\n",
            message.origin,
            message.clock_lamport,
            operation_to_string(message.action));

    pthread_mutex_unlock(&mutex);
}

void receive_message(struct message message) {
    pthread_mutex_lock(&mutex);

    // Update the Lamport clock after receiving.
    if (message.clock_lamport > lamport_clock) {
        lamport_clock = message.clock_lamport;
    }

    lamport_clock++;

    // Required output format.
    fprintf(stdout, "%s, %u, RECV (%s), %s\n",
            node_name,
            lamport_clock,
            message.origin,
            operation_to_string(message.action));

    pthread_mutex_unlock(&mutex);
}

/*
 * Converts text to a number in the range [min, max].
 * Returns -1 if the value is invalid.
 */
long parse_number(const char *text, long min, long max) {
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    // Reject missing digits or unexpected characters.
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (value < min || value > max) {
        return -1;
    }

    return value;
}

int check_arguments(int argc, char *argv[]) {
    struct in_addr address;
    long port;

    if (argc != 3) {
        fprintf(stderr, "Usage: ./P2 <ip> <puerto>\n");
        return 0;
    }

    if (inet_pton(AF_INET, argv[1], &address) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        return 0;
    }

    port = parse_number(argv[2], MIN_PORT, MAX_PORT);

    if (port < 0) {
        fprintf(stderr, "Invalid port [1024, 65535]\n");
        return 0;
    }

    return 1;
}

// -- SERVER --
/*
 * Creates and configures the server socket.
 * Returns its file descriptor or -1 on error.
 */
int create_server_socket(long port) {
    int server_fd;
    int enable = 1;
    struct sockaddr_in address;

    // Create the socket.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket creation failed");
        return -1;
    }
    printf("Socket successfully created...\n");

    // Allow the port to be reused.
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &enable, sizeof(enable)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    // Bind the socket.
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return -1;
    }
    printf("Socket successfully binded...\n");

    // Listen for incoming connections.
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        return -1;
    }
    printf("Server listening...\n");

    return server_fd;
}

/*
 * Each thread handles one client.
 * Receives a dynamically allocated copy of the socket descriptor.
 */
void *thread_client(void *arg) {
    int client_fd = *(int *)arg;
    int result;
    size_t bytes_received;
    struct message message;
    char *buffer = (char *)&message;

    free(arg);

    // Receive messages while the connection is open.
    while (1) {
        bytes_received = 0;

        // Receive every byte of the message structure.
        while (bytes_received < sizeof(message)) {
            result = recv(client_fd, buffer + bytes_received,
                          sizeof(message) - bytes_received, 0);

            if (result < 0) {
                perror("recv failed");
                break;
            }

            if (result == 0) {
                // The client has closed the connection.
                break;
            }

            bytes_received += result;
        }

        // Stop if the message was incomplete.
        if (bytes_received != sizeof(message)) {
            break;
        }

        // Identify the P1 and P3 connections on the supervisor.
        if (strcmp(node_name, SUPERVISOR_NODE) == 0 &&
            message.action == READY_TO_SHUTDOWN) {

            pthread_mutex_lock(&mutex);

            if (strcmp(message.origin, PERCEPTION_NODE) == 0) {
                socket_p1 = client_fd;
            } else if (strcmp(message.origin, ACTUATOR_NODE) == 0) {
                socket_p3 = client_fd;
            }

            pthread_mutex_unlock(&mutex);
        }

        // Process the complete message.
        receive_message(message);

        // The supervisor has received the shutdown acknowledgment.
        if (strcmp(node_name, SUPERVISOR_NODE) == 0 &&
            message.action == SHUTDOWN_ACK) {
            break;
        }

        // Keep the client socket open until the ACK is sent.
        if (strcmp(node_name, SUPERVISOR_NODE) != 0 &&
            message.action == SHUTDOWN_NOW) {
            return NULL;
        }
    }

    pthread_mutex_lock(&mutex);

    if (socket_p1 == client_fd) {
        socket_p1 = -1;
    }

    if (socket_p2 == client_fd) {
        socket_p2 = -1;
    }

    if (socket_p3 == client_fd) {
        socket_p3 = -1;
    }

    pthread_mutex_unlock(&mutex);

    close(client_fd);

    return NULL;
}

void *create_server(void *arg) {
    long port = *(long *)arg;
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t address_length = sizeof(address);
    pthread_t threads[MAX_CLIENTS];
    int num_threads = 0;

    free(arg);

    // Create and configure the server socket.
    server_fd = create_server_socket(port);
    if (server_fd < 0) {
        exit(EXIT_FAILURE);
    }

    while (num_threads < MAX_CLIENTS) {
        int *client_fd_copy;
        int err;

        // Reset the address length before each accept() call.
        address_length = sizeof(address);

        // Accept the next connection.
        client_fd = accept(server_fd, (struct sockaddr *)&address,
                           &address_length);

        if (client_fd < 0) {
            // Retry if a signal interrupts accept().
            if (errno != EINTR) {
                perror("accept failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            }

            continue;
        }

        // Give each thread its own copy of the descriptor.
        client_fd_copy = malloc(sizeof(int));

        if (client_fd_copy == NULL) {
            perror("malloc failed");
            close(client_fd);
            continue;
        }

        *client_fd_copy = client_fd;

        err = pthread_create(&threads[num_threads], NULL,
                             thread_client, client_fd_copy);

        if (err != 0) {
            fprintf(stderr, "pthread_create failed: %s\n",
                    strerror(err));
            free(client_fd_copy);
            close(client_fd);
            continue;
        }

        num_threads++;
        printf("Client connected...\n");
    }

    // Wait for both client threads to finish.
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    if (server_fd >= 0) {
        close(server_fd);
    }

    return NULL;
}

// -- CLIENT --
/*
 * Creates a socket and connects to the server.
 * Returns the socket descriptor or -1 on error.
 */
int connect_to_server(const char *ip, long port) {
    int sock;
    struct sockaddr_in server_address;

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);

    // Convert the IPv4 address from text to binary form.
    if (inet_pton(AF_INET, ip, &server_address.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        return -1;
    }

    // Create the socket.
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }

    printf("Socket successfully created...\n");

    // Connect to the server.
    if (connect(sock, (struct sockaddr *)&server_address,
                sizeof(server_address)) < 0) {
        if (errno != EINTR) {
            perror("connection failed");
        }

        close(sock);
        return -1;
    }

    printf("connected to the server...\n");

    return sock;
}

int create_client(const char *ip, const char *port_text) {
    int *client_fd_copy;
    int err;
    long port;

    port = parse_number(port_text, MIN_PORT, MAX_PORT);

    if (port < 0) {
        return -1;
    }

    socket_p2 = connect_to_server(ip, port);

    if (socket_p2 < 0) {
        return -1;
    }

    // Allocate memory for the socket descriptor.
    client_fd_copy = malloc(sizeof(int));

    if (client_fd_copy == NULL) {
        perror("malloc failed");
        close(socket_p2);
        socket_p2 = -1;
        return -1;
    }

    *client_fd_copy = socket_p2;

    // Create the receiver thread.
    err = pthread_create(&client_thread, NULL,
                         thread_client, client_fd_copy);

    if (err != 0) {
        fprintf(stderr, "pthread_create failed: %s\n",
                strerror(err));
        free(client_fd_copy);
        close(socket_p2);
        socket_p2 = -1;
        return -1;
    }

    return 0;
}

int close_client(void) {
    int err, sock;

    // Wait for the receiver thread to finish.
    err = pthread_join(client_thread, NULL);

    if (err != 0) {
        fprintf(stderr, "pthread_join failed: %s\n",
                strerror(err));
        return -1;
    }

    // Retrieve the socket and mark it as closed.
    pthread_mutex_lock(&mutex);
    sock = socket_p2;
    socket_p2 = -1;
    pthread_mutex_unlock(&mutex);

    // Close the connection.
    if (sock >= 0) {
        if (close(sock) < 0) {
            perror("close failed");
            return -1;
        }
    }

    return 0;
}

int init_server(const char *port_text) {
    long port;
    long *port_copy;
    int err;

    port = parse_number(port_text, MIN_PORT, MAX_PORT);

    if (port < 0) {
        return -1;
    }

    port_copy = malloc(sizeof(long));

    if (port_copy == NULL) {
        perror("malloc failed");
        return -1;
    }

    *port_copy = port;

    err = pthread_create(&server_thread, NULL,
                         create_server, port_copy);

    if (err != 0) {
        fprintf(stderr, "pthread_create failed: %s\n",
                strerror(err));
        free(port_copy);
        return -1;
    }

    return 0;
}

int close_server(void) {
    int err;

    // Wait for the server thread to finish.
    err = pthread_join(server_thread, NULL);

    if (err != 0) {
        fprintf(stderr, "pthread_join failed: %s\n",
                strerror(err));
        return -1;
    }

    return 0;
}