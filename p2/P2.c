// Nodo supervisor central de seguridad y gestor del ciclo de vida del robot
//		safety_supervisor

#include "stub.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>

enum {
	VALUE_SHUTDOWN_P1 = 3, // Manda SD_N a P1 y pasa a 4
	VALUE_SHUTDOWN_P3 = 7, // Manda SD_N a P3 y pasa a 8
	VALUE_CLOSED_NODES = 11
};

void funcion_shutdown_p1() {
    struct message message;

    strcpy(message.origin, "P2");
    message.action = SHUTDOWN_NOW;

    send_message(message, "P1");
}

void funcion_shutdown_p3() {
    struct message message;

    strcpy(message.origin, "P2");
    message.action = SHUTDOWN_NOW;

    send_message(message, "P3");
}

int main(int argc, char *argv[]) {
	if (!check_arguments(argc, argv)) {
		return 1;
	}
	
	strcpy(node_name, "P2");

	// Inicializar servidor
	if (init_server(argv[2]) != 0) {
		return 1;
	}

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

	// Fase 3: todos los nodos cerraron de manera normal
	while (get_clock_lamport() != VALUE_CLOSED_NODES) {
		// Usar un pequeño sleep o usleep para evitar consumo de CPU del 100%
		// (comprobar con htop)
		usleep(10000);
	}

	// Esperar a que terminen los hilos del servidor
	if (close_server() != 0) {
		return 1;
	}

	fprintf(stdout, "Subsistemas de software del robot apagados correctamente en t(lamport) = %d\n",
       get_clock_lamport());

	return 0;
}