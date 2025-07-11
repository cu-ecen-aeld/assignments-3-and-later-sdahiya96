#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t shutdown_flag = 0;
int sock_fd = -1;

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        if(sock_fd >= 0){
		close(sock_fd);
		sock_fd = -1;
	}
	remove(DATA_FILE);
	shutdown_flag = 1;
    }
}

int setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    
    if (sigaction(SIGINT, &sa, NULL)) {
        perror("sigaction SIGINT");
        return -1;
    }
    if (sigaction(SIGTERM, &sa, NULL)) {
        perror("sigaction SIGTERM");
        return -1;
    }
    return 0;
}

int create_socket() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        syslog(LOG_ERR, "Failed to create socket");
        return -1;
    }

    // Set SO_REUSEADDR to avoid "Address already in use" errors
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr))) {
        syslog(LOG_ERR, "Failed to bind socket");
        close(fd);
        return -1;
    }

    if (listen(fd, 5)) {
        syslog(LOG_ERR, "Failed to listen on socket");
        close(fd);
        return -1;
    }

    return fd;
}

void handle_client(int client_fd) {
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len)) {
        perror("getpeername");
        return;
    }
    
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    int data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0666);
    if (data_fd < 0) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        close(client_fd);
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char *packet = NULL;
    size_t packet_size = 0;
    
    while ((bytes_read = recv(client_fd, buffer, sizeof(buffer), 0))) {
        if (bytes_read < 0) {
            if (errno == EINTR) break;
            perror("recv");
            break;
        }
        
        // Process received data
        char *start = buffer;
        char *newline;
        while ((newline = memchr(start, '\n', bytes_read - (start - buffer)))) {
            size_t segment_size = newline - start + 1;
            
            // Append to packet
            char *temp = realloc(packet, packet_size + segment_size);
            if (!temp) {
                perror("realloc");
                break;
            }
            packet = temp;
            memcpy(packet + packet_size, start, segment_size);
            packet_size += segment_size;
            
            // Write to file
            if (write(data_fd, packet, packet_size) != packet_size) {
                syslog(LOG_ERR, "Failed to write to data file");
            }
            syslog(LOG_DEBUG, "Processed %zd bytes from %s\n", bytes_read, client_ip); 
            // Send file contents back to client
            lseek(data_fd, 0, SEEK_SET);
            char file_buffer[BUFFER_SIZE];
            ssize_t file_bytes;
            while ((file_bytes = read(data_fd, file_buffer, sizeof(file_buffer)))) {
                if (file_bytes < 0) {
                    perror("read");
                    break;
                }
                send(client_fd, file_buffer, file_bytes, 0);
            }
            
            // Reset packet
            free(packet);
            packet = NULL;
            packet_size = 0;
            
            start = newline + 1;
        }
        
        // Handle remaining data
        size_t remaining = bytes_read - (start - buffer);
        if (remaining > 0) {
            char *temp = realloc(packet, packet_size + remaining);
            if (!temp) {
                perror("realloc");
                break;
            }
            packet = temp;
            memcpy(packet + packet_size, start, remaining);
            packet_size += remaining;
        }
    }
    
    free(packet);
    close(data_fd);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);
    
    if (setup_signal_handlers()) {
        return EXIT_FAILURE;
    }
    
    sock_fd = create_socket();
    if (sock_fd < 0) {
        return EXIT_FAILURE;
    }
    syslog(LOG_INFO, "Server started on port %d\n", PORT);
    
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return EXIT_FAILURE;
        }
        if (pid > 0) {
            // Parent exits
            return EXIT_SUCCESS;
        }
    }
    
    while (!shutdown_flag) {
        int client_fd = accept(sock_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        handle_client(client_fd);
    }
    
    // Cleanup
    close(sock_fd);
    remove(DATA_FILE);
    syslog(LOG_INFO, "Server shutdown complete");
    closelog();
    
    return EXIT_SUCCESS;
}
