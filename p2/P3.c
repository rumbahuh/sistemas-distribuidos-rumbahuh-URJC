// Nodo de control de motores y actuadores físicos
//		actuator_node

#include "stub.h"
#include <unistd.h>
#include <string.h>

enum {
	VALUE_READY = 0, // Manda R_T_S a P2 y pasa a 1
	VALUE_ACK = 9 // Manda SD_ACK a P2 y pasa a 10
};

void funcion_ready() {
	struct message message = {0};

	strcpy(message.origin, "P3");
    message.action = READY_TO_SHUTDOWN;

    send_message(message, "P2");
}

void funcion_ack() {
	struct message message = {0};

	strcpy(message.origin, "P3");
    message.action = SHUTDOWN_ACK;

    send_message(message, "P2");
}

int main(int argc, char *argv[]) {
	if (!check_arguments(argc, argv)) {
		return 1;
	}

	strcpy(node_name, "P3");

	// Conectar con el supervisor
	if (createClient(argv[1], argv[2]) != 0) {
		return 1;
	}
	
	// Fase 1: mandar READY
	while (get_clock_lamport() != VALUE_READY) {
		// Usar un pequeño sleep o usleep para evitar consumo de CPU del 100%
		// (comprobar con htop)
		usleep(10000);
	}

	funcion_ready();

	// Fase 2: mandar ACK
	while (get_clock_lamport() != VALUE_ACK) {
		// Usar un pequeño sleep o usleep para evitar consumo de CPU del 100%
		// (comprobar con htop)
		usleep(10000);
	}

	funcion_ack();

	// Esperar al hilo de recepción y cerrar la conexión
	if (close_client() != 0) {
		return 1;
	}

	return 0;
}