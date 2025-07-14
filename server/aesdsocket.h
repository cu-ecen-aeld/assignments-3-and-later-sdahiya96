#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>

#include "queue_bsd.h"

#define PORT "9000"

// (IPv4 only--see struct sockaddr_in6 for IPv6)
// struct sockaddr_in {
//     short int          sin_family;  // Address family, AF_INET
//     unsigned short int sin_port;    // Port number
//     struct in_addr     sin_addr;    // Internet address
//     unsigned char      sin_zero[8]; // Same size as struct sockaddr
// };

/// Create struct for thread information
struct CThreadInstance
{
    int fd ; // accepted socket connection identifyer, unique for each thread
    struct sockaddr_storage client_addr; // Describes the socket address.
    bool done; // True if the thread can be terminated
    pthread_t* thread;
    pthread_mutex_t *file_mutex; // Shared resource synchronization
};

struct slist_data_s
{
    struct CThreadInstance thread_data;
    SLIST_ENTRY(slist_data_s) pointers;
};

/// In case of abort request, terminate threads
static volatile int keepRunning = 1;
void intHandler(int dummy) {
    keepRunning = 0;
}

/// Helper function get mutex
void get_mutex(pthread_mutex_t* mutex)
{
    int rc = pthread_mutex_lock(mutex);
    if(rc != 0)
    {
        syslog(LOG_INFO, "[CHILD TREAD] could not get mutex");
    }
}

/// Helper function release mutex
void release_mutex(pthread_mutex_t* mutex)
{
    int rc = pthread_mutex_unlock(mutex);
    if(rc != 0)
    {
        syslog(LOG_INFO, "[CHILD TREAD] could not get mutex");
    }
}

/// Function which transfer process to a deamon
/// A deamon is running outside any console, in the system background
void create_deamon()
{        
    // creating child process which is not attached to a TTY
    int pid = fork();
    // An error occurred
    if (pid < 0)
        exit(EXIT_FAILURE);
    // Success: Let the parent terminate so grand parent can proceed
    if (pid > 0)
        exit(EXIT_SUCCESS);

    // Create a new session so daemon is not linked to any tty
    if (setsid() < 0)
        exit(EXIT_FAILURE);

    // Change the working directory to the root directory so no possible error if unmount
    chdir("/");

    // Redirect stdin, stdout and stderr to /dev/null, so our daemon won t communicate through a console
    close(0); close(1); close(2);
    open("/dev/null",O_RDWR); dup(0); dup(0);
}

/// Function initializing the socket, prepares the future connections
int createSocketConnection(struct addrinfo* my_addr)
{
    // Create socket and bind it to given port
    int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
    // first, load up address structs with getaddrinfo():
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // use IPv4 or IPv6, whichever
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // fill in my IP for me
    int status = getaddrinfo(NULL, PORT, &hints, &my_addr);
    if (status != 0 || my_addr == NULL)
    {
        return -1;
    }

    // Assign an address to the socket
    bind(socket_fd, my_addr->ai_addr, sizeof(struct sockaddr));
    syslog(LOG_INFO, "Socket created and binded, with file descriptor %d\n", socket_fd);
    return socket_fd;
}

