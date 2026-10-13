#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#define BUFFER_SIZE 1024

volatile sig_atomic_t sigint = 1;

// Para no sufrir de condiciones de carrera
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
// hilos atendiendo clientes ahora mismo
int active_threads = 0;

enum {
    MIN_PORT = 1024,
    MAX_PORT = 65535,
    BACKLOG = 128,
    MAX_THREADS = 100,
    POLL_US = 10000 // 10ms entre comprobaciones cuando está lleno
};

/*
 * Manejador de la señal SIGINT (Control+C)
 *
 * Se trata de una subrutina de interrupcion
 * que bloquea el while asi que gastamos un 
 * mínimo tiempo usando el flag signint.
 */
void handle_sigint(int sig) {
    // Evito un warning
    (void)sig;

    // Seteo la flag
    sigint = 0;
}

/*
 * Convierte texto a número en el rango [min, max]. 
 * Devuelve -1 si no es válido.
 */
long parse_number(const char *text, long min, long max)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    // Si no hay digitos o hay basura al final
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (value < min || value > max) {
        return -1;
    }

    return value;
}

/*
 * Crea y configura el socket del servidor.
 * Devuelve el descriptor del socket o -1 si ocurre un error.
 */
int create_server_socket(long port)
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
    } // Añado comprobación de error a la primitiva

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0) {
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
 * Espera hasta que haya hueco para un hilo más y lo reserva.
 * Devuelve 0 si se hizo Control+C mientras ocurre la espera
 */
int wait_for_slot(void)
{
    while (sigint) {
        pthread_mutex_lock(&mutex);

        if (active_threads < MAX_THREADS) {
            active_threads++;
            pthread_mutex_unlock(&mutex);

            return 1;
        }

        pthread_mutex_unlock(&mutex);

        usleep(POLL_US);
    }

    return 0;
}

// Libera un hueco
void release_slot(void)
{
    pthread_mutex_lock(&mutex);
    active_threads--;
    pthread_mutex_unlock(&mutex);
}

/*
 * Función que ejecuta cada hilo: atiende a un único cliente.
 * Recibe una copia en memoria dinámica del client_fd.
 */
void * thread_client(void *arg)
{
    int client_fd = *(int *)arg; // Hago un cast
    char buffer[BUFFER_SIZE] = {0}; // Local al hilo, no se comparte
    const char *reply = "Hello client!";
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)client_fd;
    int r;

    free(arg); // Libero la copia de memoria dinámica

    // recv (bloqueante: este hilo solo atiende a un cliente)
    r = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (r < 0) {
        perror("recv failed");
        goto end;
    }

    if (r == 0) {
        // El cliente cerró la conexión sin enviar nada
        goto end;
    }

    buffer[r] = '\0';
    printf("+++ %s\n", buffer);

    // Espera aleatoria entre 0.5 y 2 segundos (en microsegundos)
    usleep(500000 + rand_r(&seed) % 1500001);

    // Send reply
    if (send(client_fd, reply, strlen(reply), 0) < 0) {
        perror("send failed");
    }

end:
    close(client_fd);
    release_slot();

    return NULL;
}

int main(int argc, char * argv[]) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    long port; // [1024, 65535]
    int opt;

    struct sigaction sa;

    // Sin buffering, obligatorio en prácticas
    setbuf(stdout, NULL);

    // No se aceptan opciones: si getopt encuentra alguna, es un error
    while ((opt = getopt(argc, argv, "")) != -1) {
        fprintf(stderr, "Usage: ./server [port_number]\n");
        exit(EXIT_FAILURE);
    }

    // Los argumentos se entienden diferentemente
    // usando optind en lugar de argv++ y argc--.
    // Empieza en optind, así que restamos
    if (argc - optind != 1) {
        fprintf(stderr, "Usage: ./server [port_number]\n");
        exit(EXIT_FAILURE);
    }

    port = parse_number(argv[optind], MIN_PORT, MAX_PORT);
    if (port < 0) {
        fprintf(stderr, "Invalid port [1024, 65535]\n");
        exit(EXIT_FAILURE);
    }

    // Registramos el sigint
    // Con sigaction y SIN SA_RESTART, las llamadas bloqueantes
    // (fgets/read, accept) devuelven error con errno == EINTR
    // en vez de reiniciarse como hace signal() en glibc.
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }

    // Create and configure server socket
    server_fd = create_server_socket(port);
    if (server_fd < 0) {
        exit(EXIT_FAILURE);
    }

    while (sigint) {
        pthread_t thread;
        int *client_fd_copia;
        int err;

        // Para no sobreescribir, reiniciamos addrlen
        addrlen = sizeof(address);

        // Si hay 100 hilos activos no aceptamos más: esperan en la cola del kernel
        if (!wait_for_slot()) {
            break;
        }

        // Accept conections
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            release_slot(); // No usaremos el hueco reservado para su thread

            // EINTR = Control+C mientras esperaba conexión, no es un error real
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
            release_slot();

            continue;
        }
        *client_fd_copia = client_fd;

        err = pthread_create(&thread, NULL, thread_client, client_fd_copia);
        if (err != 0) {
            fprintf(stderr, "pthread_create failed: %s\n", strerror(err));
            free(client_fd_copia);
            close(client_fd);
            release_slot(); // porque wait_for_slot() incrementó active_threads

            continue;
        } // Especificación de errores de primitivas

        // No permito join, para que cada hilo libre recursos al terminar
        err = pthread_detach(thread);
        if (err != 0) {
            fprintf(stderr, "pthread_detach failed: %s\n", strerror(err));
        }

        printf("Client connected...\n");
    }

    if (!sigint) {
        printf("\nClosing cleanly...\n");
    }

    if (server_fd >= 0) {
        close(server_fd);
    }

    exit(EXIT_SUCCESS);
}