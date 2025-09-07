/*
Name: B.Preetham
Roll:22Cs10015
GDrive: https://drive.google.com/drive/folders/1ATi6IspZRnERxlp6zUwT4PrFTid5pJvb?usp=drive_link
*/
//--------------------------------------------------------------------------------------------
#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define SEQ_SPACE 256 // 0-255
#define MAX_SOCKETS 10
#define MAX_PAYLOAD 512
#define WINDOW_SIZE 10
#define T 5
#define P 0.2 // drop probability
#define SHM_KEY 10015

#define SOCK_KTP 3
#define ENOTBOUND 1001
#define ENOSPACE 1002
#define ENOMESSAGE 1003
#define ERRORNP 1004

typedef struct
{
    uint64_t timestamp;
    char data[MAX_PAYLOAD];
    uint8_t seq_num;
    uint16_t len;
    uint8_t flags;
} KTP_Packet;

typedef struct
{
    KTP_Packet window[WINDOW_SIZE];
    int base;
    int buf_size, win_size;
    int next_seq;
    int recv_size;
} send_wdw;

typedef struct
{
    KTP_Packet window[WINDOW_SIZE];
    int exp_seq;
    int buf_size;
    int base;
} recv_wdw;

typedef struct
{
    recv_wdw rwnd;
    send_wdw swnd;
    struct sockaddr_in remote_addr, my_addr;

    pthread_mutex_t lock;

    int udp_sock;
    int is_active, is_binded;
    int pid;
} KTP_Socket;

// Function declarations - User access
int k_socket();
int k_bind(int ktp_sock, struct sockaddr_in *myaddr, struct sockaddr_in *cliaddr);
int k_close(int ktp_sock);
int send_data(int fc, char *buf, int len);
int recv_data(int fd, char *buf, int len);
int dropMessage(float p);

extern KTP_Socket *ktp_sockets;
extern pthread_t sender_tid, receiver_tid;

// Function declaration - internal declaration
void send_packet(KTP_Socket *i, KTP_Packet *pk);
int init_shared_memory();
void *sender_thread();
void *reciever_thread();

#endif