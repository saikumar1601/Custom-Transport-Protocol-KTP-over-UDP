/*
Name: B.Preetham
Roll:22Cs10015
GDrive: https://drive.google.com/drive/folders/1ATi6IspZRnERxlp6zUwT4PrFTid5pJvb?usp=drive_link
*/
//--------------------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include "ksocket.h"
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

int debug = 0;
#ifdef DEBUG_VER
debug = 1;
#endif

extern KTP_Socket *ktp_sockets;
void init_threads()
{
    printf("Initializing socket sender and receiver threads...\n");

    if (fork() == 0)
    {
        execl("./initial", "initial", NULL);
    }
}

void garbage_collector()
{
    for (int i = 0; i < MAX_SOCKETS; i++)
    {
        pthread_mutex_lock(&ktp_sockets[i].lock);
        if (ktp_sockets[i].is_binded == 0 && ktp_sockets[i].is_active == 1)
        {
            if (bind(ktp_sockets[i].udp_sock, (struct sockaddr *)&ktp_sockets[i].my_addr, sizeof(ktp_sockets[i].my_addr)) == 0)
            {
                ktp_sockets[i].is_binded = 1;
                printf("binding success\n");
            }
        }
        if (ktp_sockets[i].pid != 0)
        {
            if (kill(ktp_sockets[i].pid, 0) == -1)
            {
                if (errno == ESRCH)
                {
                    pthread_mutex_unlock(&ktp_sockets[i].lock);
                    k_close(i);
                    pthread_mutex_lock(&ktp_sockets[i].lock);
                    printf("Closing underlying udp socket for ksocket %d\n", i);
                    close(ktp_sockets[i].udp_sock);
                    ktp_sockets[i].pid = 0;
                    ktp_sockets[i].udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
                    if (ktp_sockets[i].udp_sock < 0)
                    {
                        perror("Failed to create socket");
                        pthread_mutex_unlock(&ktp_sockets[i].lock);
                        continue;
                    }
                    // Add this after socket creation for each socket
                    int flags = fcntl(ktp_sockets[i].udp_sock, F_GETFL, 0);
                    fcntl(ktp_sockets[i].udp_sock, F_SETFL, flags | O_NONBLOCK);
                }
            }
        }
        pthread_mutex_unlock(&ktp_sockets[i].lock);
    }
}

int init_shared_memory()
{
    int flag = 0;
    int shmid = shmget(SHM_KEY, 0, 0);
    if (shmid < 0)
    {
        // If it does not exist, create it
        shmid = shmget(SHM_KEY, MAX_SOCKETS * sizeof(KTP_Socket) + sizeof(pthread_mutex_t), IPC_CREAT | 0666);
        if (shmid < 0)
        {
            perror("Shared memory allocation failed for sockets");
            exit(1);
        }
        flag = 1;
    }

    printf("Process %d: Got shmid %d\n", getpid(), shmid);

    KTP_Socket *ktp_sockets_ = (KTP_Socket *)shmat(shmid, NULL, 0);
    if (ktp_sockets_ == (void *)-1)
    {
        perror("Shared memory attach failed");
        exit(1);
    }
    printf("Process %d: Attached at address %p\n", getpid(), (void *)ktp_sockets_); // Add this

    ktp_sockets = ktp_sockets_;
    if (flag)
    {
        memset(ktp_sockets_, 0, MAX_SOCKETS * sizeof(KTP_Socket));
        pthread_mutexattr_t mutex_attr;
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            pthread_mutex_init(&ktp_sockets_[i].lock, &mutex_attr);
            ktp_sockets_[i].udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
            if (ktp_sockets_[i].udp_sock < 0)
            {
                perror("Failed to create socket");
                exit(1);
            }
            // Add this after socket creation for each socket
            int flags = fcntl(ktp_sockets_[i].udp_sock, F_GETFL, 0);
            fcntl(ktp_sockets_[i].udp_sock, F_SETFL, flags | O_NONBLOCK);
        }
        pthread_mutexattr_destroy(&mutex_attr);
        init_threads();
    }
    printf("KTP socket initialization complete\n");
    return flag;
}

int getcurtime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec; // Returns current time in SECONDS
}

void *sender_thread()
{
    while (1)
    {
        if (shmget(SHM_KEY, 0, 0) == -1)
        {
            printf("Shared memory removed. Stopping sender thread.\n");
            pthread_exit(NULL);
        }

        garbage_collector();

        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            // printf("Sender thread entered socket:%d\n", i);
            pthread_mutex_lock(&ktp_sockets[i].lock);
            if (ktp_sockets[i].is_active == 0 || ktp_sockets[i].is_binded != 1)
            {
                pthread_mutex_unlock(&ktp_sockets[i].lock);
                continue;
            }

            // printf("sender Thread: %d sock is active\n", i);
            if ((ktp_sockets[i].swnd.win_size + ktp_sockets[i].swnd.buf_size) == 0)
            {
                pthread_mutex_unlock(&ktp_sockets[i].lock);
                continue;
            }

            while (1)
            {
                if ((ktp_sockets[i].swnd.win_size < ktp_sockets[i].swnd.recv_size) && ktp_sockets[i].swnd.buf_size > 0)
                {
                    ktp_sockets[i].swnd.win_size++;
                    ktp_sockets[i].swnd.buf_size--;
                }
                else if ((ktp_sockets[i].swnd.win_size > ktp_sockets[i].swnd.recv_size) && ktp_sockets[i].swnd.buf_size > 0)
                {
                    ktp_sockets[i].swnd.win_size--;
                    ktp_sockets[i].swnd.buf_size++;
                }
                else
                    break;
            }

            int base = ktp_sockets[i].swnd.base;

            if (ktp_sockets[i].swnd.recv_size == 0 && ktp_sockets[i].swnd.buf_size > 0)
            {
                if ((getcurtime() - ktp_sockets[i].swnd.window[base].timestamp) > 10)
                {
                    send_packet(&ktp_sockets[i], &ktp_sockets[i].swnd.window[base]);
                    // Sending packet to check for sender window size
                }
            }

            for (int j = 0; j < ktp_sockets[i].swnd.win_size; j++)
            {
                KTP_Packet *temp = &ktp_sockets[i].swnd.window[(base + j) % WINDOW_SIZE];
                // printf("Sending a packet timestamp:%ld winsize:%d\n", temp->timestamp, ktp_sockets[i].swnd.win_size);
                send_packet(&ktp_sockets[i], temp);
            }

            pthread_mutex_unlock(&ktp_sockets[i].lock);
        }
    }
}

char pkdata[600];
void send_packet(KTP_Socket *fd, KTP_Packet *pk)
{
    if (pk->timestamp != 0)
    {
        if ((getcurtime() - pk->timestamp) < T) // T is in seconds
            return;
        pk->flags = 1;
    }
    pk->timestamp = getcurtime();
    sprintf(pkdata, "%d,%d,%d,%s", pk->flags, pk->seq_num, pk->len, pk->data);
    printf("%s\n", pkdata);

    sendto(fd->udp_sock, pkdata, sizeof(pkdata), 0,
           (struct sockaddr *)&fd->remote_addr, sizeof(fd->remote_addr));
    printf("Sent a packet of seq %d\n", pk->seq_num);
}

char recv_buf[600];
char data[MAX_PAYLOAD];

void *reciever_thread()
{
    fd_set readset;
    int nfds;
    struct sockaddr tempaddr;
    int templen = sizeof(tempaddr);

    while (1)
    {
        if (shmget(SHM_KEY, 0, 0) == -1)
        {
            printf("Shared memory removed. Stopping sender thread.\n");
            pthread_exit(NULL);
        }
        FD_ZERO(&readset);
        nfds = 0;

        // Add active sockets to readset
        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            // printf("Reciever thread entered socket:%d\n", i);
            pthread_mutex_lock(&ktp_sockets[i].lock);
            if (ktp_sockets[i].is_active == 0 || ktp_sockets[i].is_binded != 1)
            {
                pthread_mutex_unlock(&ktp_sockets[i].lock);
                continue;
            }

            FD_SET(ktp_sockets[i].udp_sock, &readset);
            if (nfds < ktp_sockets[i].udp_sock)
                nfds = ktp_sockets[i].udp_sock;

            pthread_mutex_unlock(&ktp_sockets[i].lock);
        }

        // Wait for data on any socket
        struct timeval timeout;
        timeout.tv_sec = 0;       // Convert milliseconds to seconds
        timeout.tv_usec = 100000; // Convert remainder to microseconds

        int ret = select(nfds + 1, &readset, NULL, NULL, &timeout);
        if (ret < 0)
        {
            perror("select failed");
            continue;
        }

        for (int i = 0; i < MAX_SOCKETS; i++)
        {
            pthread_mutex_lock(&ktp_sockets[i].lock);
            if (ktp_sockets[i].is_active == 0)
            {
                pthread_mutex_unlock(&ktp_sockets[i].lock);
                continue;
            }

            if (FD_ISSET(ktp_sockets[i].udp_sock, &readset))
            {
                ssize_t bytes_read = recvfrom(ktp_sockets[i].udp_sock, recv_buf, sizeof(recv_buf), 0, &tempaddr, (socklen_t *)&templen);
                if (bytes_read < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        pthread_mutex_unlock(&ktp_sockets[i].lock);
                        continue;
                    }
                    perror("recvfrom error");
                    pthread_mutex_unlock(&ktp_sockets[i].lock);
                    continue;
                }
                if (bytes_read > 0)
                {
                    if (dropMessage(P))
                    {
                        printf("Dropped a message (custom implementation)!\n");
                        pthread_mutex_unlock(&ktp_sockets[i].lock);
                        continue;
                    }
                    int flags, seq_no, len;
                    char temp_data[MAX_PAYLOAD] = {0}; // Temporary buffer to extract limited data

                    // Parse only metadata first
                    if (sscanf(recv_buf, "%d,%d,%d", &flags, &seq_no, &len) < 3)
                    {
                        printf("Error parsing received packet metadata!\n");
                        pthread_mutex_unlock(&ktp_sockets[i].lock);
                        continue;
                    }
                    printf("pid:%d recieved a packet seq:%d\n", ktp_sockets[i].pid, seq_no);

                    // Ensure len is within valid bounds
                    if (len > MAX_PAYLOAD || len < 0)
                    {
                        printf("Invalid packet length: %d\n", len);
                        pthread_mutex_unlock(&ktp_sockets[i].lock);
                        continue;
                    }

                    // Copy only the specified length of data
                    char *data_start = strchr(recv_buf, ','); // Move to the first comma
                    if (data_start)
                    {
                        data_start = strchr(data_start + 1, ','); // Move to the second comma
                        if (data_start)
                        {
                            data_start = strchr(data_start + 1, ','); // Move to the third comma
                            if (data_start)
                            {
                                data_start++; // Move to actual data start
                                strncpy(temp_data, data_start, len);
                                temp_data[len] = '\0'; // Ensure null termination
                            }
                        }
                    }

                    // Handle Data Packet (flags: 0 = Data, 1 = Retransmission)
                    if (flags == 0 || flags == 1)
                    {
                        int lower_bound = (ktp_sockets[i].rwnd.exp_seq - 20 + SEQ_SPACE) % SEQ_SPACE;
                        int upper_bound = (ktp_sockets[i].rwnd.exp_seq - 1 + SEQ_SPACE) % SEQ_SPACE;

                        if ((lower_bound <= upper_bound && (seq_no >= lower_bound && seq_no <= upper_bound)) ||
                            (lower_bound > upper_bound && (seq_no >= lower_bound || seq_no <= upper_bound)))
                        {

                            printf("Duplicate packet Ressending ack\n");
                            KTP_Packet ack_pkt;
                            memset(&ack_pkt, 0, sizeof(KTP_Packet));
                            ack_pkt.seq_num = upper_bound;
                            ack_pkt.flags = 2; // ACK
                            ack_pkt.len = (WINDOW_SIZE - ktp_sockets[i].rwnd.buf_size);
                            send_packet(&ktp_sockets[i], &ack_pkt);
                            pthread_mutex_unlock(&ktp_sockets[i].lock);
                            continue;
                        }
                        printf("Window details before ACk: base:%d buf_size:%d\n", ktp_sockets[i].rwnd.base, ktp_sockets[i].rwnd.buf_size);
                        int index = (ktp_sockets[i].rwnd.base + ktp_sockets[i].rwnd.buf_size + seq_no - ktp_sockets[i].rwnd.exp_seq) % WINDOW_SIZE;
                        KTP_Packet *pkt = &ktp_sockets[i].rwnd.window[index];
                        if (index >= ktp_sockets[i].rwnd.base && index < (ktp_sockets[i].rwnd.base + ktp_sockets[i].rwnd.buf_size) % WINDOW_SIZE)
                        {
                            perror("writing before reading\n");
                            pthread_mutex_unlock(&ktp_sockets[i].lock);
                            continue;
                        }
                        memcpy(pkt->data, temp_data, len);
                        pkt->flags = 2; // flag 1 means ack but not read by user 0 means ack and read 2 means buffered for cum ack
                        pkt->seq_num = seq_no;
                        pkt->len = len;
                        int last_seq = seq_no;
                        if (seq_no == ktp_sockets[i].rwnd.exp_seq)
                        {
                            printf("Exp seq Data Packet seq:%d\n", seq_no);
                            while (1)
                            {
                                pkt->flags = 1;
                                ktp_sockets[i].rwnd.buf_size++;
                                ktp_sockets[i].rwnd.exp_seq++;
                                index++;
                                index %= WINDOW_SIZE;
                                last_seq = pkt->seq_num;
                                pkt = &ktp_sockets[i].rwnd.window[index];
                                if (pkt->flags != 2)
                                {
                                    break;
                                }
                            }

                            KTP_Packet ack_pkt;
                            memset(&ack_pkt, 0, sizeof(KTP_Packet));
                            ack_pkt.seq_num = last_seq;
                            ack_pkt.flags = 2; // ACK
                            ack_pkt.len = (WINDOW_SIZE - ktp_sockets[i].rwnd.buf_size);
                            send_packet(&ktp_sockets[i], &ack_pkt);
                        }
                        printf("Window details after ACk: base:%d buf_size:%d\n", ktp_sockets[i].rwnd.base, ktp_sockets[i].rwnd.buf_size);
                    }

                    // Handle ACK Packet
                    else if (flags == 2) // ACK Packet Received
                    {
                        printf("Received ACK Packet for seq_no: %d\n", seq_no);
                        printf("Before ack win_size:%d base:%d buf_size:%d\n",
                               ktp_sockets[i].swnd.win_size, ktp_sockets[i].swnd.base, ktp_sockets[i].swnd.buf_size);

                        int base = ktp_sockets[i].swnd.base;
                        int base_seq = ktp_sockets[i].swnd.window[base].seq_num; // ✅ Corrected base sequence number

                        int lower_bound = (base_seq - 20 + SEQ_SPACE) % SEQ_SPACE;
                        int upper_bound = (base_seq - 1 + SEQ_SPACE) % SEQ_SPACE;

                        ktp_sockets[i].swnd.recv_size = len;

                        if ((lower_bound <= upper_bound && (seq_no >= lower_bound && seq_no <= upper_bound)) ||
                            (lower_bound > upper_bound && (seq_no >= lower_bound || seq_no <= upper_bound)))
                        {
                            printf("Got a duplicate ACK for seq_no: %d\n", seq_no);
                            pthread_mutex_unlock(&ktp_sockets[i].lock);
                            continue;
                        }

                        if (ktp_sockets[i].swnd.win_size == 0)
                        {
                            pthread_mutex_unlock(&ktp_sockets[i].lock);
                            continue;
                        }

                        if (ktp_sockets[i].swnd.window[base].seq_num == seq_no)
                        {
                            ktp_sockets[i].swnd.win_size--;
                            base = (base + 1) % WINDOW_SIZE;
                        }
                        else
                        {
                            while (ktp_sockets[i].swnd.window[base].seq_num != seq_no)
                            {
                                ktp_sockets[i].swnd.win_size--;
                                base = (base + 1) % WINDOW_SIZE;
                            }
                            ktp_sockets[i].swnd.win_size--;
                            base = (base + 1) % WINDOW_SIZE;
                        }
                        ktp_sockets[i].swnd.base = base;
                        printf("After ack win_size:%d base:%d buf_size:%d\n",
                               ktp_sockets[i].swnd.win_size, ktp_sockets[i].swnd.base, ktp_sockets[i].swnd.buf_size);
                    }
                }
            }
            pthread_mutex_unlock(&ktp_sockets[i].lock);
        }
    }
}

int dropMessage(float p)
{
    static int initialized = 0;
    if (!initialized)
    {
        srand(time(NULL));
        initialized = 1;
    }

    float rand_val = (float)rand() / RAND_MAX;

    return (rand_val < p) ? 1 : 0;
}