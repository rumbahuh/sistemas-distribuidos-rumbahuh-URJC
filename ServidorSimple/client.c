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
    while ((c = getchar()) != 'n' && c != EOF);
}

int main(void) {
    int server_fd, client_fd;
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    const char *msg = "Received"; // not sure

    // Debe leer de la entrada estandar
    FILE *input = stdin;

    // Evito los warnings por ahora
    (void)buffer;
    (void)msg;
    (void)server_fd;
    (void)client_fd;

	// Registramos el sigint
	signal(SIGINT, handle_sigint);

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IPv4 address from text to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        perror("invalid address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    return 0;
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

        // Send message
        //write(sock, msg, strlen(msg));
        //printf("Sent: %s\n", msg);
		// recv
        // Read reply
        //ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
        //if (bytes_read > 0) {
        //    buffer[bytes_read] = '\0';
        //    printf("Received: %s\n", buffer);
        //}

		// Close socket
        close(sock); // ROBERTO DICE QUE ESTO NO VA AQUI

		if (!sigint) {
			printf("\nClosing cleanly...\n");
			if (sock >= 0) {
				close(sock);
			} // no libera buffer?

			exit(EXIT_SUCCESS);
		}
	}
}