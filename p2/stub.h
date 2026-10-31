enum operations {
	READY_TO_SHUTDOWN = 0,
	SHUTDOWN_NOW,
	SHUTDOWN_ACK
};

struct message {
	char origin[20]; // nombre del nodo ej. P1
	enum operations action; // READY_TO_SHUTDOWN SHUTDOWN_NOW SHUTDOWN_ACK
	unsigned int clock_lamport; // valor del contador logico de Lamport del emisor
};

// Obtiene el valor actual del reloj de Lamport.
// Utilízalo cada vez que necesites consultar el tiempo lógico del nodo.
// Esta función NO puede realizar ninguna operación de comunicación por red
// (sockets).
int get_clock_lamport();

void send_message(struct message message);
void receive_message(struct message message);