#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ksocket.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h>

#define PORT 8887 // Port to listen on
#define BSIZE 300

int main()
{
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    char buffer[BSIZE];
    socklen_t addr_len = sizeof(client_addr);

    // Create UDP socket
    if ((sockfd = k_socket()) < 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Server address configuration
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY;
    client_addr.sin_port = htons(9997);

    // Bind the socket
    if (k_bind(sockfd, &server_addr, &client_addr) < 0)
    {
        perror("Bind failed");
        k_close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Got %d ksocket and binded\n", sockfd);
    FILE *file = fopen("output.txt", "w"); // Open in binary mode
    if (!file)
    {
        perror("Failed to open output.txt");
        k_close(sockfd);
        return 1;
    }

    while (1)
    {
        memset(buffer, 0, BSIZE); // Clear buffer
        int n = recv_data(sockfd, buffer, BSIZE - 1);

        while (n < 0 && errno == ENOMESSAGE) // ENOMSG instead of ENOMESSAGE
        {
            sleep(2);
            n = recv_data(sockfd, buffer, BSIZE - 1);
        }

        if (n < 0)
        {
            perror("Socket closed");
            break;
        }

        fwrite(buffer, 1, n, file);
        fflush(file);
    }

    printf("Check for output.txt before closing\n");

    fclose(file);
    k_close(sockfd);
    printf("File reception completed.\n");

    return 0;
}
