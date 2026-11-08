// Central safety supervisor and robot lifecycle manager
// safety_supervisor

#include "stub.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define PERCEPTION_NODE "P1"
#define ACTUATOR_NODE "P3"
#define WAIT_INTERVAL_US 10000

enum {
    VALUE_SHUTDOWN_P1 = 3, // Sends SHUTDOWN_NOW to P1 and advances to 4
    VALUE_SHUTDOWN_P3 = 7, // Sends SHUTDOWN_NOW to P3 and advances to 8
    VALUE_CLOSED_NODES = 11
};

void send_shutdown_p1() {
    struct message message = {0};

    strcpy(message.origin, node_name);
    message.action = SHUTDOWN_NOW;

    send_message(message, PERCEPTION_NODE);
}

void send_shutdown_p3() {
    struct message message = {0};

    strcpy(message.origin, node_name);
    message.action = SHUTDOWN_NOW;

    send_message(message, ACTUATOR_NODE);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if (!check_arguments(argc, argv)) {
        return 1;
    }

    strcpy(node_name, "P2");

    // Initialize the server
    if (init_server(argv[2]) != 0) {
        return 1;
    }

    // Phase 1: send SHUTDOWN_NOW to P1
    while (get_clock_lamport() != VALUE_SHUTDOWN_P1) {
        // Avoid 100% CPU usage during the wait
        usleep(WAIT_INTERVAL_US);
    }

    send_shutdown_p1();

    // Phase 2: send SHUTDOWN_NOW to P3
    while (get_clock_lamport() != VALUE_SHUTDOWN_P3) {
        // Avoid 100% CPU usage during the wait
        usleep(WAIT_INTERVAL_US);
    }

    send_shutdown_p3();

    // Phase 3: wait until all nodes have shut down
    while (get_clock_lamport() != VALUE_CLOSED_NODES) {
        // Avoid 100% CPU usage during the wait
        usleep(WAIT_INTERVAL_US);
    }

    // Wait for the server threads to finish
    if (close_server() != 0) {
        return 1;
    }

    fprintf(stdout,
            "Subsistemas de software del robot apagados correctamente "
            "en t(lamport) = %d\n",
            get_clock_lamport());

    return 0;
}