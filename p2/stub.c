#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>

#define BUFFER_SIZE 1024

#include "stub.h"

unsigned int RELOJ = 0;
char node_name[20];

// Para no sufrir de condiciones de carrera
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

enum {
	MIN_ID = 1,
	MAX_ID = 1000000,
	MIN_PORT = 1024,
	MAX_PORT = 65535,
	BACKLOG = 128,
};

pthread_t server_thread;
pthread_t client_thread;

int socket_p1 = -1;
int socket_p2 = -1;
int socket_p3 = -1;

// -- RELOJES DE LAMPORT --
int get_clock_lamport() {
    int actual;

    pthread_mutex_lock(&mutex);

    actual = RELOJ;

    pthread_mutex_unlock(&mutex);

    return actual;
}

void update_clock_lamport(int counter) {
    pthread_mutex_lock(&mutex);

	RELOJ = counter;

    pthread_mutex_unlock(&mutex);
}

char *operation_to_string(enum operations action) {
    switch (action) {
        case READY_TO_SHUTDOWN:
            return "READY_TO_SHUTDOWN";
        case SHUTDOWN_NOW:
            return "SHUTDOWN_NOW";
        case SHUTDOWN_ACK:
            return "SHUTDOWN_ACK";
        default:
            return "UNKNOWN";
    }
}

void send_message(struct message message, const char *destino) {
    int sock = -1;
    ssize_t r;
    size_t enviados = 0;
    char *buffer = (char *)&message;

    pthread_mutex_lock(&mutex);

    // Seleccionar el socket del destinatario
    if (strcmp(destino, "P1") == 0) {
        sock = socket_p1;
    }
    else if (strcmp(destino, "P2") == 0) {
        sock = socket_p2;
    }
    else if (strcmp(destino, "P3") == 0) {
        sock = socket_p3;
    }

    if (sock < 0) {
        pthread_mutex_unlock(&mutex);
        fprintf(stderr, "No existe conexión con %s\n", destino);
        return;
    }

    // Actualizar el reloj de Lamport
    RELOJ++;
    message.clock_lamport = RELOJ;

    // Enviar todos los bytes del mensaje
    while (enviados < sizeof(message)) {
        r = send(sock, buffer + enviados,
                 sizeof(message) - enviados, 0);

        if (r < 0) {
            perror("send failed");
            pthread_mutex_unlock(&mutex);
            return;
        }

        if (r == 0) {
            fprintf(stderr, "No se pudo completar el envío\n");
            pthread_mutex_unlock(&mutex);
            return;
        }

        enviados += r;
    }

    // Especificacion de prints
    fprintf(stdout, "%s, %u, SEND, %s\n",
            message.origin,
            message.clock_lamport,
            operation_to_string(message.action));

    pthread_mutex_unlock(&mutex);
}

void receive_message(struct message message) {
    pthread_mutex_lock(&mutex);

    // Actualizar reloj de Lamport
    if (message.clock_lamport > RELOJ) {
        RELOJ = message.clock_lamport;
    }

    RELOJ++;

    // Especificacion de prints
    fprintf(stdout, "%s, %u, RECV (%s), %s\n",
            node_name,
            RELOJ,
            message.origin,
            operation_to_string(message.action));

    pthread_mutex_unlock(&mutex);
}

// -----------------------
/*
 * Convierte texto a número en el rango [min, max]. 
 * Devuelve -1 si no es válido.
 */
long
parse_number(const char *text, long min, long max)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(text, &end, 10);

	// end == text: no había dígitos. *end != '\0': basura al final ("12abc")
	if (errno != 0 || end == text || *end != '\0') {
		return -1;
	}

	if (value < min || value > max) {
		return -1;
	}

	return value;
}

int check_arguments(int argc, char *argv[]) {
    struct in_addr address;
    long port;

    if (argc != 3) {
        fprintf(stderr, "Usage: ./P2 <ip> <puerto>\n");
        return 0;
    }

    if (inet_pton(AF_INET, argv[1], &address) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[1]);
        return 0;
    }

    port = parse_number(argv[2], MIN_PORT, MAX_PORT);

    if (port < 0) {
        fprintf(stderr, "Invalid port [1024, 65535]\n");
        return 0;
    }

    return 1;
}

// -- SERVIDOR --
/*
 * Crea y configura el socket del servidor.
 * Devuelve el descriptor del socket o -1 si ocurre un error.
 */
int
create_server_socket(long port)
{
	int server_fd;
	int enable = 1;
	struct sockaddr_in address;

	// Create socket
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("socket creation failed");
		return -1;
	}
	printf("Socket successfully created...\n");

	// Reuse address and port
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
		       &enable, sizeof(enable)) < 0) {
		perror("setsockopt(SO_REUSEADDR) failed");
		close(server_fd);
		return -1;
	}			// Añado comprobación de error a la primitiva

	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(port);

	// Bind socket
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
		perror("bind failed");
		close(server_fd);
		return -1;
	}
	printf("Socket successfully binded...\n");

	// Listen for incoming connections
	if (listen(server_fd, BACKLOG) < 0) {
		perror("listen failed");
		close(server_fd);
		return -1;
	}
	printf("Server listening...\n");

	return server_fd;
}

/*
 * Función que ejecuta cada hilo: atiende a un único cliente.
 * Recibe una copia en memoria dinámica del client_fd.
 */
void *
thread_client(void *arg)
{
    int client_fd = *(int *)arg;
    int r;
    size_t recibidos;
    struct message message;
    char *buffer = (char *)&message;

    free(arg);

    // Recibir mensajes mientras la conexión esté abierta
    while (1) {
        recibidos = 0;

        // Recibir todos los bytes de la estructura
        while (recibidos < sizeof(message)) {
            r = recv(client_fd, buffer + recibidos,
                     sizeof(message) - recibidos, 0);

            if (r < 0) {
                perror("recv failed");
                break;
            }

            if (r == 0) {
                // El cliente ha cerrado la conexión
                break;
            }

            recibidos += r;
        }

        // Si el mensaje está incompleto, terminar
        if (recibidos != sizeof(message)) {
            break;
        }

        // Identificar las conexiones de P1 y P3 en el supervisor
        if (strcmp(node_name, "P2") == 0 &&
            message.action == READY_TO_SHUTDOWN) {

            pthread_mutex_lock(&mutex);

            if (strcmp(message.origin, "P1") == 0) {
                socket_p1 = client_fd;
            }
            else if (strcmp(message.origin, "P3") == 0) {
                socket_p3 = client_fd;
            }

            pthread_mutex_unlock(&mutex);
        }

        // Procesar el mensaje completo
        receive_message(message);

        // El supervisor ha recibido la confirmación de apagado
        if (strcmp(node_name, "P2") == 0 &&
            message.action == SHUTDOWN_ACK) {
            break; // Sale del bucle y continua hasta close
        }

        // El cliente ha recibido la orden de apagado
        if (strcmp(node_name, "P2") != 0 &&
            message.action == SHUTDOWN_NOW) {
            return NULL; // Necesitamos ack, asi que no termino el client
        }
    }

    pthread_mutex_lock(&mutex);

    if (socket_p1 == client_fd) {
        socket_p1 = -1;
    }

    if (socket_p2 == client_fd) {
        socket_p2 = -1;
    }

    if (socket_p3 == client_fd) {
        socket_p3 = -1;
    }

    pthread_mutex_unlock(&mutex);

    close(client_fd);

    return NULL;
}

void *createServer(void *arg) {
    long port = *(long *)arg;
    free(arg);

    int server_fd, client_fd;

    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    
    pthread_t threads[2];
    int num_threads = 0;

    // Sin buffering, obligatorio en prácticas
    setbuf(stdout, NULL);

    // Create and configure server socket
    server_fd = create_server_socket(port);
    if (server_fd < 0) {
        exit(EXIT_FAILURE);
    }

    while (num_threads < 2) {
        int *client_fd_copia;
        int err;

        // Para no sobreescribir, reiniciamos addrlen
        addrlen = sizeof(address);

        // Accept connections
        client_fd =
            accept(server_fd, (struct sockaddr *)&address, &addrlen);

        if (client_fd < 0) {

            // EINTR = Control+C mientras esperaba conexión
            if (errno != EINTR) {
                perror("accept failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            }

            continue;
        }

        // Cada hilo recibe su propia copia del descriptor
        client_fd_copia = malloc(sizeof(int));

        if (client_fd_copia == NULL) {
            perror("malloc failed");
            close(client_fd);
            continue;
        }

        *client_fd_copia = client_fd;

        err = pthread_create(&threads[num_threads], NULL,
                     thread_client, client_fd_copia);

        if (err != 0) {
            fprintf(stderr, "pthread_create failed: %s\n",
                    strerror(err));
            free(client_fd_copia);
            close(client_fd);

            continue;
        }

        num_threads++;

        printf("Client connected...\n");
    }

    // Aquí eliminé detached
    // [DETACH]
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    if (server_fd >= 0) {
        close(server_fd);
    }

    return NULL;
}
// --------------

// -- CLIENTE --

/*
 * Crea el socket y conecta con el servidor.
 * Devuelve el descriptor del socket o -1 si ocurre un error.
 */
int connect_to_server(const char *ip, long port)
{
    int sock;
    struct sockaddr_in serv_addr;

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", ip);
        return -1;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket creation failed");
        return -1;
    }

    printf("Socket successfully created...\n");

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        if (errno != EINTR) {
            perror("connection failed");
        }

        close(sock);
        return -1;
    }

    printf("connected to the server...\n");

    return sock;
}

int createClient(const char *ip, const char *puerto_texto)
{
    int *client_fd_copia;
    int err;

    long port = parse_number(puerto_texto, MIN_PORT, MAX_PORT);

    if (port < 0) {
        return -1;
    }

    socket_p2 = connect_to_server(ip, port);

    if (socket_p2 < 0) {
        return -1;
    }

    // Reservamos memoria para el descriptor del socket
    client_fd_copia = malloc(sizeof(int));

    if (client_fd_copia == NULL) {
        perror("malloc failed");
        close(socket_p2);
        socket_p2 = -1;
        return -1;
    }

    *client_fd_copia = socket_p2;

    // Creamos el hilo secundario de recepción
    err = pthread_create(&client_thread, NULL,
                         thread_client, client_fd_copia);

    if (err != 0) {
        fprintf(stderr, "pthread_create failed: %s\n",
                strerror(err));
        free(client_fd_copia);
        close(socket_p2);
        socket_p2 = -1;
        return -1;
    }

    return 0;
}

int close_client(void) {
    int err;
    int sock;

    // Esperar a que termine el hilo de recepción
    err = pthread_join(client_thread, NULL);

    if (err != 0) {
        fprintf(stderr, "pthread_join failed: %s\n",
                strerror(err));
        return -1;
    }

    // Recuperar el socket y marcarlo como cerrado
    pthread_mutex_lock(&mutex);

    sock = socket_p2;
    socket_p2 = -1;

    pthread_mutex_unlock(&mutex);

    // Cerrar la conexión
    if (sock >= 0) {
        if (close(sock) < 0) {
            perror("close failed");
            return -1;
        }
    }

    return 0;
}

int init_server(const char *puerto_texto) {

    long port = parse_number(puerto_texto, MIN_PORT, MAX_PORT);

    if (port < 0) {
        return -1;
    }

    long *port_copia;
    int err;

    port_copia = malloc(sizeof(long));

    if (port_copia == NULL) {
        perror("malloc failed");
        return -1;
    }

    *port_copia = port;

    err = pthread_create(&server_thread, NULL,
                         createServer, port_copia);

    if (err != 0) {
        fprintf(stderr, "pthread_create failed: %s\n",
                strerror(err));
        free(port_copia);
        return -1;
    }

    return 0;
}

int close_server(void) {
    int err;

    // Esperar a que termine el hilo del servidor
    err = pthread_join(server_thread, NULL);

    if (err != 0) {
        fprintf(stderr, "pthread_join failed: %s\n",
                strerror(err));
        return -1;
    }

    return 0;
}
// -------------

