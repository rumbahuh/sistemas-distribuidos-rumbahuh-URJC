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
	MAX_LINE_LEN = 512,
	BUF_SIZE = MAX_LINE_LEN + 2,	// +1 para el salto de línea y +1 para el \0 final
	MAX_FAILS = 255,
	DEFAULT_EXIT_CODE = 255,
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

int main(int argc, char * argv[]) {
    // El cliente no escucha ni acepta nada,
    // por lo que solo necesita un solo socket
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    long port; // [1024, 65535]
    long client_id; // [1, MAX_ID]
    int opt;

    // Debe leer de la entrada estandar
    FILE *input = stdin;

	struct sigaction sa;

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
        fprintf(stderr, "Invalid client_id x > 0\n");
        exit(EXIT_FAILURE);
    }

    port = parse_number(argv[optind + 2], MIN_PORT, MAX_PORT);
    if (port < 0) {
        fprintf(stderr, "Invalid port [1024, 65535]\n");
        exit(EXIT_FAILURE);
    }

    // Registramos el sigint
	// Con sigaction y SIN SA_RESTART, las llamadas bloqueantes
	// (fgets/read, connect) devuelven error con errno == EINTR
	// en vez de reiniciarse como hace signal() en glibc.
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handle_sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, argv[optind + 1], &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Invalid IP address: %s\n", argv[optind + 1]);
        exit(EXIT_FAILURE);
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    printf("Socket successfully created...\n");

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        // EINTR = Control+C mientras se conectaba, no es un error real
        if (errno != EINTR) {
            perror("connection failed");
            close(sock);
            exit(EXIT_FAILURE);
        }
        printf("\nClosing cleanly...\n");
        close(sock);
        exit(EXIT_SUCCESS);
    }
    printf("connected to the server...\n");

    while(sigint) {
        char line[BUF_SIZE];

        // Especificación de indicación de lectura
        printf(">");
        // Sin \n el prompt se queda en el buffer de stdout
        fflush(stdout);

        // Una sola lectura por vuelta, para poder volver a mirar sigint
        if (fgets(line, BUF_SIZE, input) == NULL) {
            if (!sigint) {
                // Control+C: fgets devolvió NULL por EINTR, salimos por el while
                break;
            }

            if (feof(input)) {
                // Control+D: fin de entrada, salimos en vez de repetir el prompt
                printf("\n");
                break;
            }

            fprintf(stderr, "error reading input\n");
            close(sock);
            return DEFAULT_EXIT_CODE;
        }

        // Send message
        int r = send(sock, line, strlen(line), 0);
        if (r < 0) {
            // EINTR = Control+C mientras enviaba, salimos por el while
            if (errno != EINTR) {
                perror("send failed");
            }

            break;
        }

		// recv (bloqueante: espera la respuesta del servidor)
        int n = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (n < 0) {
            // EINTR = Control+C mientras esperaba, salimos por el while
            if (errno != EINTR) {
                perror("recv failed");
            }

            break;
        }

        if (n == 0) {
            // El servidor cerró la conexión
            printf("\nServer disconnected\n");

            break;
        }

        buffer[n] = '\0';
        printf("+++ %s", buffer);
	}

    // Close socket (fuera del bucle, para cerrarlo una sola vez)
    close(sock); // ROBERTO DICE QUE ESTO NO VA AQUI

    if (!sigint) {
        printf("\nClosing cleanly...\n");
    }

    exit(EXIT_SUCCESS);
}