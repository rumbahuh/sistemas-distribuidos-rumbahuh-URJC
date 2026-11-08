#ifndef STUB_H
#define STUB_H

extern char node_name[20];

enum operations {
    READY_TO_SHUTDOWN = 0,
    SHUTDOWN_NOW,
    SHUTDOWN_ACK
};

struct message {
    char origin[20];              // Nombre del nodo emisor (ej. P1)
    enum operations action;       // Operacion solicitada
    unsigned int clock_lamport;   // Reloj logico del emisor
};

// Consulta el reloj local de Lamport sin realizar operaciones de red.
int get_clock_lamport(void);

// Comprueba los argumentos recibidos por el programa principal.
int check_arguments(int argc, char *argv[]);

// Envio y recepcion de mensajes (implementados en stub.c).
void send_message(struct message message, const char *destino);
void receive_message(struct message message);

// Inicializacion y finalizacion de las comunicaciones.
int init_server(const char *puerto_texto);
int createClient(const char *ip, const char *puerto_texto);
int close_client(void);
int close_server(void);

#endif