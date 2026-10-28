// Nodo supervisor central de seguridad y gestor del ciclo de vida del robot
//		safety_supervisor

#include "stub.h"
#include <unistd.h>

enum {
	VALUE = 0 // Me lo inventé para esquipear el error de compilacion
};

void funcion_a_ejecutar() {}

int main() {
	while (get_clock_lamport() != VALUE) {
		// Usar un pequeño sleep o usleep para evitar consumo de CPU del 100%
		// (comprobar con htop)
		usleep(10000);
	}
	funcion_a_ejecutar();

	return 0;
}