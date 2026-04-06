#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 9000
#define PAYLOAD_SIZE 1024

// The agreed-upon "Envelope" structure
typedef struct {
    int type;                  // 1 = Argument, 2 = File Data, 3 = EOF
    int data_length;           // How many bytes are in this specific message?
    char payload[PAYLOAD_SIZE];// The actual data
} Message;

int main() {
    int sock;
    struct sockaddr_in serv_addr;
    char ip[50], limit[50];

    // 1. Get user inputs locally
    printf("Enter Server IP: ");
    if (scanf("%49s", ip) != 1) return 1;

    printf("Enter the limit for prime numbers: ");
    if (scanf("%49s", limit) != 1) return 1;

    // 2. Compile program
    printf("Compiling payload locally...\n");
    if (system("gcc program.c -o program.out") != 0) {
        printf("Local compilation failed\n");
        return 1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid IP address\n");
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Connection failed\n");
        return -1;
    }

    // 3. Send the Argument (Type 1)
    Message arg_msg;
    arg_msg.type = 1;
    arg_msg.data_length = strlen(limit);
    memset(arg_msg.payload, 0, PAYLOAD_SIZE);
    strcpy(arg_msg.payload, limit);
    send(sock, &arg_msg, sizeof(Message), 0);

    // 4. Send the File Data in chunks (Type 2)
    FILE *fp = fopen("program.out", "rb");
    int bytes;
    char buffer[PAYLOAD_SIZE];
    
    while ((bytes = fread(buffer, 1, PAYLOAD_SIZE, fp)) > 0) {
        Message file_msg;
        file_msg.type = 2;
        file_msg.data_length = bytes;
        memset(file_msg.payload, 0, PAYLOAD_SIZE);
        memcpy(file_msg.payload, buffer, bytes);
        
        send(sock, &file_msg, sizeof(Message), 0);
    }
    fclose(fp);

    // 5. Send the End-Of-File signal (Type 3)
    Message eof_msg;
    eof_msg.type = 3;
    eof_msg.data_length = 0;
    send(sock, &eof_msg, sizeof(Message), 0);

    // 6. Receive final output from Server
    printf("\n---- Output from Remote Machine ----\n");
    while ((bytes = recv(sock, buffer, PAYLOAD_SIZE, 0)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
    }

    close(sock);
    return 0;
}