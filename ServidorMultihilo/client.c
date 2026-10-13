#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#define BUFFER_SIZE 1024

// volatile sig_atomic_t: el compilador no puede cachearla en un registro
volatile sig_atomic_t sigint = 1;

enum {
    MIN_ID = 1,
    MAX_ID = 1000000,
    MIN_PORT = 1024,
    MAX_PORT = 65535
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

    // end == text: no había dígitos. *end != '\0': basura al final ("12abc")
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (value < min || value > max) {
        return -1;
    }

    return value;
}

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
    if (connect(sock, (struct sockaddr *)&serv_addr,
                sizeof(serv_addr)) < 0) {
        // EINTR = Control+C mientras se conectaba, no es un error real
        if (errno != EINTR) {
            perror("connection failed");
        }

        close(sock);
        return -1;
    }

    printf("connected to the server...\n");

    return sock;
}

int main(int argc, char * argv[]) {
    // El cliente no escucha ni acepta nada,
    // por lo que solo necesita un solo socket
    int sock;
    char buffer[BUFFER_SIZE] = {0};
    long port; // [1024, 65535]
    long client_id; // [1, MAX_ID]
    int opt;
    int n;
    struct sigaction sa;

    // Sin buffering, obligatorio en prácticas
    setbuf(stdout, NULL);
    
    while ((opt = getopt(argc, argv, "")) != -1) {
        fprintf(stderr, "Usage: ./client [client_id] [ip] [port_number]\n");
        exit(EXIT_FAILURE);
    }

    if (argc - optind != 3) {
        fprintf(stderr, "Usage: ./client [client_id] [ip] [port_number]\n");
        exit(EXIT_FAILURE);
    }

    client_id = parse_number(argv[optind], MIN_ID, MAX_ID);
    if (client_id < 0) {
        fprintf(stderr, "Invalid client_id [1, 1000000]\n");
        exit(EXIT_FAILURE);
    }

    port = parse_number(argv[optind + 2], MIN_PORT, MAX_PORT);
    if (port < 0) {
        fprintf(stderr, "Invalid port [1024, 65535]\n");
        exit(EXIT_FAILURE);
    }

    // Registramos el sigint
    // Con sigaction y SIN SA_RESTART, las llamadas bloqueantes
    // (connect) devuelven error con errno == EINTR
    // en vez de reiniciarse como hace signal() en glibc.
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0) {
        perror("sigaction failed");
        exit(EXIT_FAILURE);
    }

    sock = connect_to_server(argv[optind + 1], port);
    if (sock < 0) {
        if (!sigint) {
            printf("\nClosing cleanly...\n");
            exit(EXIT_SUCCESS);
        }

        exit(EXIT_FAILURE);
    }

    // Mensaje del enunciado: "Hello server! From client: <id>"
    snprintf(buffer, BUFFER_SIZE,
             "Hello server! From client: %ld", client_id);

    // Send message
    if (send(sock, buffer, strlen(buffer), 0) < 0) {
        perror("send failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // recv (bloqueante: espera la respuesta del servidor)
    n = recv(sock, buffer, BUFFER_SIZE - 1, 0);
    if (n < 0) {
        // EINTR = Control+C mientras esperaba, no es un error real
        if (errno != EINTR) {
            perror("recv failed");
            close(sock);
            exit(EXIT_FAILURE);
        }

        close(sock);
        exit(EXIT_SUCCESS);
    }

    if (n == 0) {
        // El servidor cerró la conexión sin responder
        printf("\nServer disconnected\n");
    } else {
        buffer[n] = '\0';
        printf("+++ %s\n", buffer);
    }

    // Close socket
    close(sock);

    exit(EXIT_SUCCESS);
}