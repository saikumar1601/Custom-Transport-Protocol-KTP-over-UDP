/*
Name: B.Preetham
Roll:22Cs10015
GDrive: https://drive.google.com/drive/folders/1ATi6IspZRnERxlp6zUwT4PrFTid5pJvb?usp=drive_link
*/
//--------------------------------------------------------------------------------------------
#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>

KTP_Socket *ktp_sockets = NULL;
void kill_sock();

int k_socket()
{
    if (ktp_sockets == NULL)
    {
        if (init_shared_memory())
        {
            for (int i = 0; i < MAX_SOCKETS; i++)
            {
                pthread_mutex_lock(&ktp_sockets[i].lock);
                close(ktp_sockets[i].udp_sock);
                pthread_mutex_unlock(&ktp_sockets[i].lock);
            }
        }
    }
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        pthread_mutex_lock(&ktp_sockets[i].lock);
        if (ktp_sockets[i].is_active == 0)
        {
            ktp_sockets[i].swnd.buf_size = 0;
            ktp_sockets[i].swnd.win_size = 0;
            ktp_sockets[i].swnd.next_seq = 1;
            ktp_sockets[i].rwnd.buf_size = 0;
            ktp_sockets[i].rwnd.exp_seq = 1;
            ktp_sockets[i].rwnd.base = 0;
            ktp_sockets[i].swnd.base = 0;
            ktp_sockets[i].swnd.recv_size = WINDOW_SIZE;

            ktp_sockets[i].pid = getpid();
            ktp_sockets[i].is_binded = -1;

            ktp_sockets[i].is_active = 1;
            pthread_mutex_unlock(&ktp_sockets[i].lock);
            return i;
        }
        pthread_mutex_unlock(&ktp_sockets[i].lock);
    }
    return -1;
}

int k_bind(int ktp_sock, struct sockaddr_in *myaddr, struct sockaddr_in *cliaddr)
{
    pthread_mutex_lock(&ktp_sockets[ktp_sock].lock);
    if (ktp_sockets[ktp_sock].pid != getpid())
    {
        errno = ERRORNP;
        pthread_mutex_unlock(&ktp_sockets[ktp_sock].lock);
        return -1;
    }
    if (!ktp_sockets[ktp_sock].is_active)
    {
        pthread_mutex_unlock(&ktp_sockets[ktp_sock].lock);
        return -1;
    }
    ktp_sockets[ktp_sock].remote_addr = *cliaddr;
    ktp_sockets[ktp_sock].my_addr = *myaddr;
    ktp_sockets[ktp_sock].is_binded = 0;

    pthread_mutex_unlock(&ktp_sockets[ktp_sock].lock);
    return 1;
}

int send_data(int fd, char *buf, int len)
{
    if (fd < 0 || fd >= MAX_SOCKETS || buf == NULL || len <= 0)
    {
        // perror("Invalid arguments to send_data\n");
        return -1;
    }

    pthread_mutex_lock(&ktp_sockets[fd].lock);
    if (ktp_sockets[fd].pid != getpid() || ktp_sockets[fd].is_active == 0)
    {
        errno = ERRORNP;
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        return -1;
    }

    if ((ktp_sockets[fd].swnd.buf_size + ktp_sockets[fd].swnd.win_size) >= WINDOW_SIZE)
    {
        // perror("No space in buffer\n");
        errno = ENOSPACE;
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        return -1;
    }

    int index = (ktp_sockets[fd].swnd.base + ktp_sockets[fd].swnd.win_size + ktp_sockets[fd].swnd.buf_size) % WINDOW_SIZE;
    KTP_Packet *pkt = &ktp_sockets[fd].swnd.window[index];

    memset(pkt, 0, sizeof(KTP_Packet));
    pkt->seq_num = ktp_sockets[fd].swnd.next_seq;
    memcpy(pkt->data, buf, len);
    pkt->len = len;

    ktp_sockets[fd].swnd.buf_size++;
    ktp_sockets[fd].swnd.next_seq++;

    pthread_mutex_unlock(&ktp_sockets[fd].lock);
    printf("Added data in window\n");
    return len;
}

int recv_data(int fd, char *buf, int len)
{
    if (fd < 0 || fd >= MAX_SOCKETS || buf == NULL || len <= 0)
    {
        // perror("Invalid arguments to recv_data\n");
        return -1;
    }

    pthread_mutex_lock(&ktp_sockets[fd].lock);
    if (ktp_sockets[fd].pid != getpid() || ktp_sockets[fd].is_active == 0)
    {
        errno = ERRORNP;
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        return -1;
    }
    if (ktp_sockets[fd].rwnd.buf_size == 0)
    {
        errno = ENOMESSAGE;
        // perror("No message to recv");
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        return -1;
    }

    int index = ktp_sockets[fd].rwnd.base;
    int bytes_available = ktp_sockets[fd].rwnd.window[index].len;
    int bytes_to_read = (bytes_available < len) ? bytes_available : len;

    memcpy(buf, ktp_sockets[fd].rwnd.window[index].data, bytes_to_read);

    if (bytes_to_read < bytes_available)
    {
        memmove(ktp_sockets[fd].rwnd.window[index].data,
                ktp_sockets[fd].rwnd.window[index].data + bytes_to_read,
                bytes_available - bytes_to_read);

        ktp_sockets[fd].rwnd.window[index].len -= bytes_to_read;
    }
    else
    {
        ktp_sockets[fd].rwnd.buf_size--;
        ktp_sockets[fd].rwnd.base++;
        ktp_sockets[fd].rwnd.base %= WINDOW_SIZE;
        ktp_sockets[fd].rwnd.window[index].flags = 0;
    }

    pthread_mutex_unlock(&ktp_sockets[fd].lock);

    return bytes_to_read;
}

int k_close(int fd)
{
    if (fd < 0 || fd >= MAX_SOCKETS)
    {
        perror("Invalid socket descriptor");
        return -1;
    }

    pthread_mutex_lock(&ktp_sockets[fd].lock);

    if (!ktp_sockets[fd].is_active)
    {
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        perror("Socket already closed");
        return -1;
    }

    while ((ktp_sockets[fd].swnd.buf_size + ktp_sockets[fd].swnd.win_size) > 0)
    {
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        pthread_mutex_lock(&ktp_sockets[fd].lock);
    }

    while (ktp_sockets[fd].rwnd.buf_size > 0)
    {
        pthread_mutex_unlock(&ktp_sockets[fd].lock);
        pthread_mutex_lock(&ktp_sockets[fd].lock);
    }

    printf("Closing socket %d (fd: %d)\n", fd, ktp_sockets[fd].udp_sock); // ✅ Debug print
    ktp_sockets[fd].is_active = 0;
    ktp_sockets[fd].is_binded = -1;
    memset(&ktp_sockets[fd].rwnd, 0, sizeof(recv_wdw));
    memset(&ktp_sockets[fd].swnd, 0, sizeof(send_wdw));
    memset(&ktp_sockets[fd].remote_addr, 0, sizeof(struct sockaddr_in));
    memset(&ktp_sockets[fd].my_addr, 0, sizeof(struct sockaddr_in));

    pthread_mutex_unlock(&ktp_sockets[fd].lock);
    return 1;
}