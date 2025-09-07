/*
Name: B.Preetham
Roll: 22CS10015
GDrive: https://drive.google.com/drive/folders/1ATi6IspZRnERxlp6zUwT4PrFTid5pJvb?usp=drive_link
*/
//--------------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include "ksocket.h"

#define MAX_PAYLOAD_ 200 // Adjust buffer size as needed

int main()
{
    srand(time(NULL));
    int fd = k_socket();
    struct sockaddr_in cli_addr, serv_addr;

    if (fd < 0)
    {
        perror("Failed to create socket");
        return 1;
    }

    printf("Created Socket at %d\n", fd);

    cli_addr.sin_family = AF_INET;
    cli_addr.sin_addr.s_addr = INADDR_ANY;
    cli_addr.sin_port = htons(9997);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(8887);

    if (k_bind(fd, &cli_addr, &serv_addr) < 0)
    {
        perror("Binding failed");
        k_close(fd);
        return 1;
    }
    printf("Binding Succeeded\n");

    FILE *file = fopen("input.txt", "r"); // Open in binary mode to preserve all characters
    if (!file)
    {
        perror("Failed to open input.txt");
        k_close(fd);
        return 1;
    }

    sleep(1);

    printf("Press any key to continue:");
    getchar();
    printf("\n");

    char buffer[MAX_PAYLOAD_];
    size_t bytes_read;

    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        int status = send_data(fd, buffer, bytes_read);

        while (status < 0 && errno == ENOSPACE) // ENOSPC instead of ENOSPACE
        {
            printf("Buffer full, retrying in 10 seconds...\n");
            sleep(10);
            status = send_data(fd, buffer, bytes_read);
        }

        if (status < 0)
        {
            perror("Socket transmission error");
            break;
        }
    }

    printf("File added to buf and sent to window, check output.txt before closing(cntrl+c)\n");

    while (1)
    {
        sleep(1);
    }

    fclose(file);
    k_close(fd);
    printf("File transmission completed.\n");
    return 0;
}
