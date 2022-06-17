#include <stdio.h>
#include <string.h>
#include "connection_queue.h"

/*
    int client_fds[CAPACITY];
    int length;
    int read_idx;
    int write_idx;
    int shutdown;
*/
int connection_queue_init(connection_queue_t *queue) {
    // Initialize all variables and mutexs for synchronization associated with the queue
    queue->length = 0;
    queue->read_idx = 0;
    queue->write_idx = 0;
    int result;
    queue->shutdown = 0;
    // Error check for all the init function calls
    if ((result = pthread_mutex_init(&queue->lock, NULL)) != 0) {
        fprintf(stderr, "pthread_mutex_init: %s\n", strerror(result));
        return -1;
    }
    // Initialize the queue full condition
    if ((result = pthread_cond_init(&queue->queue_full, NULL)) != 0) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        return -1;
    }
    // Initializes queue empty condition
    if ((result = pthread_cond_init(&queue->queue_empty, NULL)) != 0) {
        fprintf(stderr, "pthread_cond_init: %s\n", strerror(result));
        return -1;
    }
    return 0;
}

int connection_enqueue(connection_queue_t *queue, int connection_fd) {
    int result;
    // Lock the mutex before the critical section
    if ((result = pthread_mutex_lock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }
    // Put the threads to sleep if the queue is full
    while (queue->length == CAPACITY && !queue->shutdown){  // Potentially 
        if ((result = pthread_cond_wait(&queue->queue_full, &queue->lock)) != 0) {
            fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            return -1;
        }
    }
    // Check if the server is shutdown
    if (queue->shutdown){
        if ((result = pthread_cond_signal(&queue->queue_empty)) != 0) {
            fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
            return -1;
        }
        if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
            fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
            return -1;
        }
        return 0;
    }
    // Critical Section: perform enqueue operations
    queue->client_fds[queue->write_idx] = connection_fd;
    queue->length++;
    queue->write_idx = (queue->write_idx + 1) % 5;
    // Done, release the mutex lock and signal other threads
    if ((result = pthread_cond_signal(&queue->queue_empty)) != 0) {
        fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }
    return 0;
}

int connection_dequeue(connection_queue_t *queue) {
    int result;
    // Lock the mutex before the critical section
    if ((result = pthread_mutex_lock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(result));
        return -1;
    }
    // Put the threads to sleep is the queue is full
    while(queue->length == 0 && !queue->shutdown){ // Potentially do 
        if ((result = pthread_cond_wait(&queue->queue_empty, &queue->lock)) != 0) {
            fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
            return -1;
        }
    }
    // Check if the server is shutdown
    if (queue->shutdown){
        if ((result = pthread_cond_signal(&queue->queue_full)) != 0) {
            fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
            return -1;
        }
        if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
            fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
            return -1;
        }
        return 0;
    }
    
    // Critical Section: Perform dequeue operations
    int fd = queue->client_fds[queue->read_idx];
    queue->read_idx = (queue->read_idx + 1) % 5;
    queue->length--;

    // Release the mutex lock and signal other threads
    if ((result = pthread_cond_signal(&queue->queue_full)) != 0) {
        fprintf(stderr, "pthread_cond_signal: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_mutex_unlock(&queue->lock)) != 0) {
        fprintf(stderr, "pthread_mutex_unlock: %s\n", strerror(result));
        return -1;
    }
    
    return fd;
}

int connection_queue_shutdown(connection_queue_t *queue) {
    int result = 0;
    queue->shutdown = 1;
    // Signal all threads that are waiting on both enqueue and dequeue
    if ((result = pthread_cond_broadcast(&queue->queue_full)) != 0) {
        fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_cond_broadcast(&queue->queue_empty)) != 0) {
        fprintf(stderr, "pthread_cond_wait: %s\n", strerror(result));
        return -1;
    }
    
    return 0;
}

int connection_queue_free(connection_queue_t *queue) {
    int result;
    // Destroy all the mutex locks and conditional variables
    if ((result = pthread_cond_destroy(&queue->queue_full)) != 0) {
        fprintf(stderr, "queue_full pthread_cond_destroy: %s\n", strerror(result));
        return -1;
    }
    if ((result = pthread_cond_destroy(&queue->queue_empty)) != 0) {
        fprintf(stderr, "queue_empty pthread_cond_destroy: %s\n", strerror(result));
        return -1;
    }
   if ((result = pthread_mutex_destroy(&queue->lock)) != 0) {
        fprintf(stderr, "lock pthread_mutex_destroy: %s\n", strerror(result));
        return -1;
    }
    return 0;
}
