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
    MIN_PORT = 1024,
    MAX_PORT = 65535,
    BACKLOG = 128
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
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    int enable = 1;
    long port; // [1024, 65535]
    int opt;

    // Debe leer de la entrada estandar
    FILE *input = stdin;

	struct sigaction sa;

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
	sigaction(SIGINT, &sa, NULL);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully created...\n");

    // Reuse address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    } // Añado comprobación de error a la primitiva

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Socket successfully binded...\n");

    // Listen for incoming connections
    if (listen(server_fd, BACKLOG) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("Server listening...\n");

	while(sigint) {
        // Para no sobreescribir, reiniciamos addrlen
        addrlen = sizeof(address);

        // Accept conections
        client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (client_fd < 0) {
            // EINTR = Control+C mientras esperaba conexión, no es un error real
            if (errno != EINTR) {
                perror("accept failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            }

            continue;
        }

        printf("Client connected...\n");

        while(sigint) {
            char line[BUF_SIZE];

            // recv (bloqueante)
            int r = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
            if (r < 0) {
                // EINTR = Control+C mientras recibía, salimos por el while
                if (errno != EINTR) {
                    perror("recv failed");
                }

                break;
            }

            if (r == 0) {
                // El cliente cerró la conexión
                printf("\nClient disconnected\n");

                break;
            }

            buffer[r] = '\0';
            printf("+++ %s", buffer);

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
                break; // Especificacion: solo termina con CTRL+C, asi que
            }          // no return y añado un break.

            // Send reply
            int s = send(client_fd, line, strlen(line), 0);
            if (s < 0) {
                // EINTR = Control+C mientras enviaba, salimos por el while
                if (errno != EINTR) {
                    perror("send failed");
                }

                break;
            }
        }

        // Cierro cliente y vuelvo a accept
        close(client_fd);
	}

    if (!sigint) {
        printf("\nClosing cleanly...\n");
    }

    if (server_fd >= 0) {
        close(server_fd);
    }

    exit(EXIT_SUCCESS);
}