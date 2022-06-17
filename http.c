#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include "http.h"

#define BUFSIZE 512

const char *get_mime_type(const char *file_extension) {
    if (strcmp(".txt", file_extension) == 0) {
        return "text/plain";
    } else if (strcmp(".html", file_extension) == 0) {
        return "text/html";
    } else if (strcmp(".jpg", file_extension) == 0) {
        return "image/jpeg";
    } else if (strcmp(".png", file_extension) == 0) {
        return "image/png";
    } else if (strcmp(".pdf", file_extension) == 0) {
        return "application/pdf";
    }

    return NULL;
}

int read_http_request(int fd, char *resource_name) {
    // Declare a buffer to store read info
    char buf[BUFSIZE];
    // Validate content is a GET
    char *get_str = "GET";
    char mode_buf[4];
    // Reads first four characters
    if (read(fd, mode_buf, 4) < 0){
        fprintf(stderr, "Improper read of first 4 bytes\n");
        return 1;
    }
    mode_buf[3] = '\0';
    // Checks if the first 4 characters are GET 
    if (strcmp(mode_buf, get_str) != 0){
        fprintf (stderr, "Wrong mode %s\n", mode_buf);
        return 1;
    }
    int first_loop = 1;
    int bytes_read = 0;
    // Finds the resource name and reads the rest of the file
    while ((bytes_read = read(fd, buf, BUFSIZE)) > 0){
        // finds the resource name on first iteration
        if (first_loop){
            strcpy(resource_name, strtok(buf, " "));
            first_loop--;
        }
        // End loop at end of content
        if (bytes_read != BUFSIZE){
            break;
        }
    }
    return 0;
}

int write_http_response(int fd, const char *resource_path) {
    // Declares buffers to store content to read and write
    char line[BUFSIZE];
    char temp[BUFSIZE];
    struct stat file;
    // copies resource path into temp
    strcpy(temp, resource_path);
    // tokenize the resource path using strtok
    char* token = strtok(temp, ".");
    token = strtok(NULL, ".");
    token = token - 1;
    token[0] = '.';
    char *ending = "\r\n";
    // If not found, write 404 Not Found
    if(stat(resource_path, &file) == -1){
        // Construct the response using snprintf
        if (snprintf(line, BUFSIZE, "HTTP/1.0 404 Not Found\r\n") < 0){
            printf("error snprintfing");
            return 1;
        }
        // Writes the response to the provided file descriptor
        if(write(fd, line, strlen(line)) == -1){
            return 1;
        }
        // Constructs the string of the file size content
        if (snprintf(line, BUFSIZE, "Content-Length: %d\r\n", 0) < 0){
            printf("error snprintfing");
            return 1;
        }
        // prints file size content
        if(write(fd, line, strlen(line)) == -1){
            return 1;
        }
        // prints the ending of the HTTP format
        if(write(fd, ending, strlen(ending)) == -1){
            return 1;
        }
        return 0;
    }
    // If valid, write 200 OK
    else{
        int byte_write = 0;
        // First Line (200 ok)
        if (snprintf(line, BUFSIZE, "HTTP/1.0 200 OK\r\n") < 0){
            fprintf(stderr,"error snprintfing");
            return 1;
        }
        // Write first line to file descriptor
        if((byte_write = write(fd, line, strlen(line))) == -1){
            return 1;
        }
        // clear content of line, filling it with 0
        memset(line, 0, byte_write);
        // Second Line (Content Type:)
        const char* type = get_mime_type(token);
        if (snprintf(line, BUFSIZE, "Content-Type: %s\r\n", type) < 0){
            printf("error snprintfing");
            return 1;
        }
        if((byte_write = write(fd, line, strlen(line))) == -1){
            return 1;
        }
        memset(line, 0, byte_write);
        // Third Line (Content Length)
        if (snprintf(line, BUFSIZE, "Content-Length: %d\r\n", (int)file.st_size) < 0){
            printf("error snprintfing");
            return 1;
        }
        if((byte_write = write(fd, line, strlen(line))) == -1){
            return 1;
        }
        memset(line, 0, byte_write);
        // Separate between headers and body
        if((byte_write = write(fd, ending, strlen(ending))) == -1){
            return 1;
        }
        memset(line, 0, byte_write);
    }
    
    // Open file to copy content from 
    int file_fd = open(resource_path, O_RDONLY | O_CREAT);
    if (file_fd == -1){
        perror("could not open file");
        return 1;
    }
    // Variables required to copy file content
    char file_buf[BUFSIZE];
    int bytes_read;
    // Loop to copy file content
    while ((bytes_read = read(file_fd, file_buf, BUFSIZE)) > 0){
        if (write(fd, file_buf, bytes_read) != bytes_read){
            perror("Writing file");
            return 1;
        }
    }
    if (close(file_fd) == -1){
        perror("close");
    }
    return 0;
}
