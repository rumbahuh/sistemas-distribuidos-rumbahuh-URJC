// Camera, LiDAR, and service estimation processing node
// perception_node

#include "stub.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SUPERVISOR_NAME "P2"
#define WAIT_INTERVAL_US 10000

enum {
    VALUE_READY = 0, // Sends READY_TO_SHUTDOWN to P2 and advances to 1
    VALUE_ACK = 5    // Sends SHUTDOWN_ACK to P2 and advances to 6
};

void send_ready() {
    struct message message = {0};

    strcpy(message.origin, node_name);
    message.action = READY_TO_SHUTDOWN;

    send_message(message, SUPERVISOR_NAME);
}

void send_ack() {
    struct message message = {0};

    strcpy(message.origin, node_name);
    message.action = SHUTDOWN_ACK;

    send_message(message, SUPERVISOR_NAME);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if (!check_arguments(argc, argv)) {
        return 1;
    }

    strcpy(node_name, "P1");

    // Connect to the supervisor
    if (create_client(argv[1], argv[2]) != 0) {
        return 1;
    }

    // Phase 1: send READY
    while (get_clock_lamport() != VALUE_READY) {
        // Avoid 100% CPU usage while waiting
        usleep(WAIT_INTERVAL_US);
    }

    send_ready();

    // Phase 2: send ACK
    while (get_clock_lamport() != VALUE_ACK) {
        // Avoid 100% CPU usage while waiting
        usleep(WAIT_INTERVAL_US);
    }

    send_ack();

    // Wait for the receiver thread and close the connection
    if (close_client() != 0) {
        return 1;
    }

    return 0;
}