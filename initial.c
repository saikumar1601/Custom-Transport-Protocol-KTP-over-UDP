#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "ksocket.h"

// Redirect output to a file
void redirect_output(const char *filename)
{
    FILE *fp = freopen(filename, "w", stdout);
    if (fp == NULL)
    {
        perror("Failed to redirect stdout");
    }
    else
    {
        setvbuf(stdout, NULL, _IONBF, 0);
    }
}

// Thread functions
void *sender_thread_wrapper(void *arg)
{
    // redirect_output("sender_log.txt"); // Redirect sender thread output to sender_log.txt
    sender_thread();
    return NULL;
}

void *receiver_thread_wrapper(void *arg)
{
    // redirect_output("receiver_log.txt"); // Redirect receiver thread output to receiver_log.txt
    reciever_thread();
    return NULL;
}

int main()
{
    init_shared_memory();

    redirect_output("logs.txt");

    // Create threads
    pthread_t sender_tid, receiver_tid;

    if (pthread_create(&sender_tid, NULL, sender_thread_wrapper, NULL) != 0)
    {
        perror("Failed to create sender thread");
        return 1;
    }

    if (pthread_create(&receiver_tid, NULL, receiver_thread_wrapper, NULL) != 0)
    {
        perror("Failed to create receiver thread");
        return 1;
    }

    pthread_join(sender_tid, NULL);
    pthread_join(receiver_tid, NULL);

    return 0;
}
