#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "connection_queue.h"
#include "http.h"

#define BUFSIZE 512
#define LISTEN_QUEUE_LEN 5
#define N_THREADS 5

const char *serve_dir;
int keep_going = 1;

// Information passed to consumer threads
typedef struct {
    int idx;
    connection_queue_t *queue;
} thread_args_t;

// Signal handling function
void handle_sigint(int signo) {
    keep_going = 0;
}

// Thread function 
void *thread_func(void *arg){
    // MAIN SERVER LOOP
    thread_args_t* args = (thread_args_t *) arg;
    while (keep_going && !(args->queue->shutdown)){
        int client_fd;
        // Dequeue client fds from the queue
        if ((client_fd = connection_dequeue(args->queue)) == -1){
            printf("Dequeue error");
            continue;
        }
        // Check if the server is shutdown
        if (args->queue->shutdown == 1){
            break;
        }
        // Once connected to client, read from them using reaf_http_response
        char res[BUFSIZE];
        char new_res[BUFSIZE];
        if (read_http_request(client_fd, res)){
            perror("read");
            close(client_fd);
            break;
        }
        // Combine the requested resource to the directory
        if (snprintf(new_res, BUFSIZE, "%s", serve_dir) < 0){
            printf("error creating formatted string of file\n");
            return NULL;
        }
        if(snprintf(new_res + strlen(serve_dir), BUFSIZE - strlen(serve_dir), "%s", res) < 0){
            printf("error creating formatted string of file\n");
            return NULL;
        }
        // Write the response to the client
        if (write_http_response(client_fd, new_res)){
            perror("write");
            close(client_fd);
            break;
        }
        if (close(client_fd) == -1){
            perror("close");
            break;
        }
    }
    
    return NULL;
}

int main(int argc, char **argv) {
    // First command is directory to serve, second command is port
    if (argc != 3) {
        printf("Usage: %s <directory> <port>\n", argv[0]);
        return 1;
    }

    // Set up signal handler
    sigset_t init_set;
    if (sigfillset(&init_set) == -1 || sigaddset(&init_set, SIGINT) == -1){
        perror("sigfill");
        return 1;
    }
    if (sigprocmask(SIG_BLOCK, &init_set, NULL) == -1){
        perror("procmask");
        return 1;
    }
    
    // Initialize connection_queue
    connection_queue_t queue;
    if (connection_queue_init(&queue) == -1){
        printf("error doing the intializing of the queue.\n");
        return 1;
    }

    // Initialize threads
    pthread_t threads[N_THREADS];
    thread_args_t args[N_THREADS];

    // Uncomment the lines below to use these definitions:
    serve_dir = argv[1];
    const char *port = argv[2];

    // Setting up the TCP socket (elements for getaddrinfo)
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));   // Emptying the struct
    hints.ai_family = AF_UNSPEC;        // Unspecified INET, IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;    // TCP socket
    hints.ai_flags = AI_PASSIVE;        // Being a server
    struct addrinfo *server;

    // Calling getaddrinfo to get all necessary info regarding the server
    int ret_val = getaddrinfo(NULL, port, &hints, &server);
    if (ret_val != 0) {
        fprintf(stderr, "getaddrinfo failed: %s\n", gai_strerror(ret_val));
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
            return 1;
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
            return 1;
        }
        return 1;
    }
    // Calling socket to create socket file descriptor
    int sock_fd = socket(server->ai_family, server->ai_socktype, server->ai_protocol);
    if (sock_fd == -1) {
        perror("socket");
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
            return 1;
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
            return 1;
        }
        return 1;
    }
    // Calling bind to reserve a specific port
    if (bind(sock_fd, server->ai_addr, server->ai_addrlen) == -1) {
        fprintf(stderr, "bind error\n");
        if (connection_queue_shutdown(&queue) == -1){
            fprintf(stderr, "shutdown error\n");
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }
    // Freeing server address struct as it is no longer needed to set up the server
    freeaddrinfo(server);

    // Calling listen to designate sock_fd as server socket 
    if (listen(sock_fd, LISTEN_QUEUE_LEN) == -1) {
        fprintf(stderr, "listen error\n");
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }
    
    // Give each thread job to run
    int result;
    for (int i = 0; i < N_THREADS; i++){
        (args+i)->queue = &queue;
        (args+i)->idx = i;
        if ((result = pthread_create(threads + i, NULL, thread_func, args + i)) != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(result));
            if (connection_queue_shutdown(&queue) == -1){
                printf("shutdown error\n");
            }
            if (connection_queue_free(&queue) == -1){
                fprintf(stderr, "free error\n");
            }
            if (close(sock_fd) == -1){
                perror("close");
            }
            return 1;
        }
    }
    
    // Deal with signals in the main thread
    sigset_t new_set;
    if(sigfillset(&new_set) == -1){
        fprintf(stderr, "sigfillset error\n");
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }
    // Unblock signals now that threads have been created
    if (sigprocmask(SIG_UNBLOCK, &new_set, &init_set) == -1){
        fprintf(stderr, "sigprocmask error\n");
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }    
    // Assign provided signal handler function to sa_handler field
    struct sigaction sact;
    sact.sa_handler = handle_sigint;
    sact.sa_flags = 0;

    // Fill the sa_mask field
    if(sigfillset(&sact.sa_mask) == -1){
        fprintf(stderr, "sigfillset error\n");
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }

    // Call to sigaction to apply the signal handler
    if (sigaction(SIGINT, &sact, NULL) == -1){
        fprintf(stderr, "sigaction error\n");
        if (connection_queue_shutdown(&queue) == -1){
            printf("shutdown error\n");
        }
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }

    // Enqueue loop to add jobs
    while (keep_going){
        // Connect to client
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno != EINTR) {
                perror("accept");
                if (close(sock_fd) == -1){
                    perror("close");
                }
                if (connection_queue_shutdown(&queue) == -1){
                    printf("shutdown error\n");
                }
                if (connection_queue_free(&queue) == -1){
                    fprintf(stderr, "free error\n");
                }
                return 1;
            } else {
                break;
            }
        }
        // Enqueue the client to the queue when there's a new client
        if (connection_enqueue(&queue, client_fd) == -1){
            printf("Error adding to queue\n");
            if (close(client_fd) == -1){
                perror("close");
            }
            if (close(sock_fd) == -1){
                fprintf(stderr, "close error\n");
            }
            if (connection_queue_shutdown(&queue) == -1){
                printf("shutdown error\n");
            }
            if (connection_queue_free(&queue) == -1){
                fprintf(stderr, "free error\n");
            }
            return 1;
        }
    }
    // Shutdown the server
    if (connection_queue_shutdown(&queue) == -1){
        printf("shutdown error\n");
        if (connection_queue_free(&queue) == -1){
            fprintf(stderr, "free error\n");
        }
        return 1;
    }

    // Join all the threads
    int exit_code;
    for (int i = 0; i < N_THREADS; i++) {
        if ((result = pthread_join(threads[i], NULL)) != 0) {
            fprintf(stderr, "pthread_join: %s\n", strerror(result));
            exit_code = 1;
        }
    }

    // Free everything
    if (connection_queue_free(&queue) == -1){
        fprintf(stderr, "free error\n");
        if (close(sock_fd) == -1){
            perror("close");
        }
        return 1;
    }
    
    // Close the socker file descriptor
    if (close(sock_fd) == -1){
        perror("close");
        return 1;
    }

    return exit_code;
}
