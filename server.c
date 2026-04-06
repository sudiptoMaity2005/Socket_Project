#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <signal.h>

#define PORT 9000
#define PAYLOAD_SIZE 1024

// The agreed-upon "Envelope" structure
typedef struct {
    int type;                  // 1 = Argument, 2 = File Data, 3 = EOF
    int data_length;           // How many bytes are in this specific message?
    char payload[PAYLOAD_SIZE];// The actual data
} Message;

sem_t *lock;

void handle_client(int client_socket) {
    Message incoming;
    char argument[50] = {0};
    char filename[50];
    
    sprintf(filename, "received_%d.out", getpid());
    FILE *fp = fopen(filename, "wb");

    // 1. The Message Loop
    // MSG_WAITALL ensures TCP doesn't hand us half a struct
    while (recv(client_socket, &incoming, sizeof(Message), MSG_WAITALL) > 0) {
        
        if (incoming.type == 1) {
            // It's the user's argument limit
            strncpy(argument, incoming.payload, incoming.data_length);
            argument[incoming.data_length] = '\0';
        } 
        else if (incoming.type == 2) {
            // It's a chunk of the binary file
            fwrite(incoming.payload, 1, incoming.data_length, fp);
        } 
        else if (incoming.type == 3) {
            // Client is done sending data
            break; 
        }
    }
    fclose(fp);

    // 2. Critical Section: Execution
    sem_wait(lock);

    char cmd[256];
    sprintf(cmd, "chmod +x %s", filename);
    system(cmd);

    char outfile[50];
    sprintf(outfile, "output_%d.txt", getpid());

    // Execute with the dynamically passed argument
    sprintf(cmd, "./%s %s > %s", filename, argument, outfile);
    system(cmd);

    sem_post(lock);

    // 3. Send output back to client
    char buffer[1024];
    int bytes;
    fp = fopen(outfile, "r");
    if (fp) {
        while ((bytes = fread(buffer, 1, 1024, fp)) > 0) {
            send(client_socket, buffer, bytes, 0);
        }
        fclose(fp);
    }

    close(client_socket);
    exit(0);
}

int main() {
    signal(SIGCHLD, SIG_IGN);
    int server_fd, client_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    int opt = 1;

    lock = sem_open("/exec_lock", O_CREAT, 0644, 1);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    printf("Server running on port %d...\n", PORT);

    while (1) {
        client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);

        if (fork() == 0) {
            close(server_fd);
            handle_client(client_socket);
        }
        close(client_socket);
    }

    sem_close(lock);
    sem_unlink("/exec_lock");
    return 0;
}