#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>
#include <sys/ioctl.h>

#define PORT 8090
#define BUFFER_SIZE 1024

int sigint = 1;

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
    const char *reply = "Received";
    int opt = 1;

    // Debe leer de la entrada estandar
    FILE *input = stdin;

    // Evito los warnings por ahora
    (void)buffer;
    (void)reply;

	// Registramos el sigint
	signal(SIGINT, handle_sigint);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Reuse address and port
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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
			perror("accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
    }

	while(sigint) {
        char line[BUF_SIZE];
        
        int bytes_disponibles = 0;
        ioctl(STDIN_FILENO, FIONREAD, &bytes_disponibles);
        // Limpiamos la entrada
        if (bytes_disponibles > 0) flush_stdin();

        // Especificación de indicación de lectura
        printf(">");

        while (fgets(line, BUF_SIZE, input) != NULL) {
            printf("Leído: %s", line);
        }

        if (!feof(input)) {
		    fprintf(stderr, "error reading input\n");
		    
            return DEFAULT_EXIT_CODE;
	    }

        // Send reply
		//write(client_fd, reply, strlen(reply));
		//printf("Sent: %s\n", reply);
		// recv

		// Close sockets
		close(client_fd);

		if (!sigint) {
			printf("\nClosing cleanly...\n");
			if (server_fd >= 0) {
				close(server_fd);
			} // no libera buffer?

			exit(EXIT_SUCCESS);
		}
	}
}