#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <signal.h>

#define PORT 8090
#define BUFFER_SIZE 1024

int sigint = 1;

enum {
	MAX_LINE_LEN = 512,
	BUF_SIZE = MAX_LINE_LEN + 2,	// +1 para el salto de línea y +1 para el \0 final
	MAX_FAILS = 255,
	DEFAULT_EXIT_CODE = 255
};

// Manejador de la señal SIGINT (Control+C)
void handle_sigint(int sig) {
	// subrutina de interrupcion
	// bloquea el while
	// asi que gastamos minimo tiempo usando el flag
    sigint = 0;
}

int main(void) {
    int server_fd, client_fd;
    int sock;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    const char *msg = "Received"; // not sure

    // Debe leer de la entrada estandar
    FILE *input = stdin;

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
		// fgets
        char line[BUF_SIZE];

        while (fgets(line, BUF_SIZE, input) != NULL) {
            printf("Leído: %s", line);
        }

        if (!feof(input)) {
		    fprintf(stderr, "error reading input\n");
		    return DEFAULT_EXIT_CODE;
	    }

        // Send message
        write(sock, msg, strlen(msg));
        printf("Sent: %s\n", msg);
		// recv
        // Read reply
        ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            printf("Received: %s\n", buffer);
        }

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