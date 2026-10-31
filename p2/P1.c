// Nodo encargado de la captura y procesamiento de datos de cámara, LIDAR y estimación de servicio
//		perception_node

#include "stub.h"
#include <unistd.h>
#include <string.h>

enum {
	VALUE_READY = 0, // Manda R_T_SD a P2 y pasa a 1
	VALUE_ACK = 5 // Manda SD_ACK a P2 y pasa a 6
};

void funcion_ready() {
	struct message message;

	strcpy(message.origin, "P1");
    message.action = READY_TO_SHUTDOWN;

    send_message(message);
}

void funcion_ack() {
	struct message message;

	strcpy(message.origin, "P1");
    message.action = SHUTDOWN_ACK;

    send_message(message);
}

int main() {
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

	return 0;
}