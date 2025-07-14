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
#include <pthread.h>
#include <time.h>
#include <sys/queue.h>


#define PORT 9000
#define BUFFER_SIZE 1024
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define TIMESTAMP_INTERVAL 10


volatile sig_atomic_t shutdown_flag = 0;
int sock_fd = -1;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_node{
	pthread_t thread;
	int client_fd;
	SLIST_ENTRY(thread_node) entries;
};
SLIST_HEAD(thread_list, thread_node) thread_head = SLIST_HEAD_INITIALIZER(thread_head);


void return_file_contents(int data_fd, int client_fd) {
    lseek(data_fd, 0, SEEK_SET);
    char file_buffer[BUFFER_SIZE];
    ssize_t file_bytes;

    while (file_bytes = read(data_fd, file_buffer, sizeof(file_buffer))) {
        if (file_bytes < 0) {
            perror("read");
            break;
        }
        if (send(client_fd, file_buffer, file_bytes, 0) < 0) {
            perror("send");
            break;
        }
    }
}

//thread function to handle client connections
void *handle_client(void *arg) {
    struct thread_node *node = (struct thread_node *)arg;
    int client_fd = node->client_fd;
    char client_ip[INET_ADDRSTRLEN];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    if (getpeername(client_fd, (struct sockaddr*)&client_addr, &addr_len)) {
        perror("getpeername");
        goto cleanup;
    }

    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    int data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0666);
    if (data_fd < 0) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        goto cleanup;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    char *packet = NULL;
    size_t packet_size = 0;

    while (!shutdown_flag) {
        bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read <= 0) {
            if (bytes_read < 0 && errno == EINTR) continue;
            break;  // Client disconnected or error
        }

        buffer[bytes_read] = '\0';
        char *start = buffer;
        char *newline;

        while ((newline = strchr(start, '\n'))) {
            *newline = '\0';
            size_t line_len = strlen(start);
            
            pthread_mutex_lock(&file_mutex);
            if (write(data_fd, start, line_len) != line_len || 
                write(data_fd, "\n", 1) != 1) {  // Fixed: using string "\n"
                syslog(LOG_ERR, "Failed to write to data file");
            }
            return_file_contents(data_fd, client_fd);
            pthread_mutex_unlock(&file_mutex);
            
            start = newline + 1;
        }

        // Handle remaining data
        size_t remaining = strlen(start);
        if (remaining > 0) {
            char *temp = realloc(packet, packet_size + remaining + 1);
            if (!temp) {
                perror("realloc");
                goto cleanup;
            }
            packet = temp;
            memcpy(packet + packet_size, start, remaining);
            packet_size += remaining;
            packet[packet_size] = '\0';
        }
    }

    // Write any remaining packet data
    if (packet_size > 0) {
        pthread_mutex_lock(&file_mutex);
        if (write(data_fd, packet, packet_size) != packet_size) {
            syslog(LOG_ERR, "Failed to write remaining packet");
        }
        return_file_contents(data_fd, client_fd);
        pthread_mutex_unlock(&file_mutex);
    }

cleanup:
    free(packet);
    if (data_fd >= 0) close(data_fd);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    
    pthread_mutex_lock(&list_mutex);
    SLIST_REMOVE(&thread_head, node, thread_node, entries);
    pthread_mutex_unlock(&list_mutex);
    
    free(node);
    return NULL;
}


// Thread function for timestamp generation
void *timestamp_thread(void *arg) {
    while (!shutdown_flag) {
        sleep(TIMESTAMP_INTERVAL);
        if (shutdown_flag) break;

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

        pthread_mutex_lock(&file_mutex);
        int data_fd = open(DATA_FILE, O_CREAT | O_WRONLY | O_APPEND, 0666);
        if (data_fd >= 0) {
            write(data_fd, timestamp, strlen(timestamp));
            close(data_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        shutdown_flag = 1;

	struct thread_node *node;
        pthread_mutex_lock(&list_mutex);
        SLIST_FOREACH(node, &thread_head, entries) {
            shutdown(node->client_fd, SHUT_RDWR);
            close(node->client_fd);
        }
        pthread_mutex_unlock(&list_mutex);	
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
    
    SLIST_INIT(&thread_head);
    
    pthread_t ts_thread;
    if(pthread_create(&ts_thread, NULL, timestamp_thread, NULL)){
	    perror("pthread_create");
	    return EXIT_FAILURE;
    } 
    
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

	// Create thread node
        struct thread_node *node = malloc(sizeof(struct thread_node));
        if (!node) {
            perror("malloc");
            close(client_fd);
            continue;
        }
        node->client_fd = client_fd;

	// Add to thread list
        pthread_mutex_lock(&list_mutex);
        SLIST_INSERT_HEAD(&thread_head, node, entries);
        pthread_mutex_unlock(&list_mutex);

        // Create thread
        if (pthread_create(&node->thread, NULL, handle_client, node)) {
            perror("pthread_create");
            pthread_mutex_lock(&list_mutex);
            SLIST_REMOVE(&thread_head, node, thread_node, entries);
            pthread_mutex_unlock(&list_mutex);
            free(node);
            close(client_fd);
        }
    }
    
    // Cleanup
    close(sock_fd);
    
    // Join timestamp thread
    pthread_join(ts_thread, NULL);
    
    // Join all client threads
    struct thread_node *node;
    while (!SLIST_EMPTY(&thread_head)) {
        pthread_mutex_lock(&list_mutex);
        node = SLIST_FIRST(&thread_head);
        pthread_mutex_unlock(&list_mutex);

        if (node) {
            pthread_join(node->thread, NULL);
        }
    }

    remove(DATA_FILE);
    syslog(LOG_INFO, "Server shutdown complete");
    closelog();
    
    return EXIT_SUCCESS;
}
