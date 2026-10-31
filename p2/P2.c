// Nodo supervisor central de seguridad y gestor del ciclo de vida del robot
//		safety_supervisor

#include "stub.h"
#include <unistd.h>
#include <string.h>

enum {
	VALUE_SHUTDOWN_P1 = 3, // Manda SD_N a P1 y pasa a 4
	VALUE_SHUTDOWN_P3 = 7 // Manda SD_N a P3 y pasa a 8
};

void funcion_shutdown_p1() {
    struct message message;

    strcpy(message.origin, "P2");
    message.action = SHUTDOWN_NOW;

    send_message(message);
}

void funcion_shutdown_p3() {
    struct message message;

    strcpy(message.origin, "P2");
    message.action = SHUTDOWN_NOW;

    send_message(message);
}

int main() {
	// Fase 1: mandar SHUTDOWN NOW a P1
	while (get_clock_lamport() != VALUE_SHUTDOWN_P1) {
		// Usar un pequeño sleep o usleep para evitar consumo de CPU del 100%
		// (comprobar con htop)
		usleep(10000);
	}

	funcion_shutdown_p1();

	// Fase 2: mandar SHUTDOWN NOW a P3
	while (get_clock_lamport() != VALUE_SHUTDOWN_P3) {
		// Usar un pequeño sleep o usleep para evitar consumo de CPU del 100%
		// (comprobar con htop)
		usleep(10000);
	}

	funcion_shutdown_p3();

	return 0;
}