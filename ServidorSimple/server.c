#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8090
#define BUFFER_SIZE 1024

// volatile sig_atomic_t: el compilador no puede cachearla en un registro
volatile sig_atomic_t sigint = 1;

enum {
	MAX_LINE_LEN = 512,
	BUF_SIZE = MAX_LINE_LEN + 2,	// +1 para el salto de línea y +1 para el \0 final
	MAX_FAILS = 255,
	DEFAULT_EXIT_CODE = 255
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
 * En C, fflush(stdin) está indefinido por lo que
 * tuve que crear esta función para descartar caracteres
 * y simular la limpieza del buffer de lectura de stdin.
 */
void flush_stdin(void)
{
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

int main(void) {
    int server_fd, client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    int opt = 1;

    // Debe leer de la entrada estandar
    FILE *input = stdin;

	// Registramos el sigint
	// Con sigaction y SIN SA_RESTART, las llamadas bloqueantes
	// (fgets/read, accept) devuelven error con errno == EINTR
	// en vez de reiniciarse como hace signal() en glibc.
	struct sigaction sa;
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

    // Reuse address and port
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(PORT);

    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 1) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on 127.0.0.1:%d...\n", PORT);

    // Accept conection
    client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
    if (client_fd < 0) {
            // EINTR = Control+C mientras esperaba conexión, no es un error real
            if (errno != EINTR) {
			    perror("accept failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            }
            printf("\nClosing cleanly...\n");
            close(server_fd);
            exit(EXIT_SUCCESS);
    }

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
            close(client_fd);
            close(server_fd);
            return DEFAULT_EXIT_CODE;
        }

        // Send reply
        int s = send(client_fd, line, strlen(line), 0);
        if (s < 0) {
            // EINTR = Control+C mientras enviaba, salimos por el while
            if (errno != EINTR) {
                perror("send failed");
            }

            break;
        }

		// recv
        int r = recv(client_fd, buffer, BUFFER_SIZE - 1, MSG_DONTWAIT);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No hay nada que leer: volvemos a mostrar el prompt
                continue;
            }

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
	}

    // Close sockets (fuera del bucle, para cerrarlos una sola vez)
    close(client_fd);

    if (!sigint) {
        printf("\nClosing cleanly...\n");
    }

    if (server_fd >= 0) {
        close(server_fd);
    } // no libera buffer?

    exit(EXIT_SUCCESS);
}