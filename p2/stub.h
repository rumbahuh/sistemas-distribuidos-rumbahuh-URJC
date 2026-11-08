#ifndef STUB_H
#define STUB_H

#define NODE_NAME_LENGTH 20

extern char node_name[NODE_NAME_LENGTH];

enum operations {
    READY_TO_SHUTDOWN = 0,
    SHUTDOWN_NOW,
    SHUTDOWN_ACK
};

struct message {
    char origin[NODE_NAME_LENGTH]; // Sender node name (e.g. P1)
    enum operations action;        // Requested operation
    unsigned int clock_lamport;    // Sender logical clock
};

// Returns the local Lamport clock without performing network operations.
int get_clock_lamport(void);

// Validates the arguments received by the main program.
int check_arguments(int argc, char *argv[]);

// Message sending and receiving functions (implemented in stub.c).
void send_message(struct message message, const char *destination);
void receive_message(struct message message);

// Communication initialization and termination.
int init_server(const char *port_text);
int create_client(const char *ip, const char *port_text);
int close_client(void);
int close_server(void);

#endif