#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]){
	const char *prog = argv[0];

	if(argc != 3){
			fprintf(stderr, "Usage: %s <file> <string>\n", prog);
			return 1;
	}

	const char *filepath = argv[1];
	const char *txt = argv[2];

	openlog("writer", LOG_PID|LOG_CONS, LOG_USER);

	int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);

	if(fd < 0){
		syslog(LOG_ERR, "Failed to open '%s' : %s", filepath, strerror(errno));
		fprintf(stderr, "Error: Cannot create file '%s': %s\n", filepath, strerror(errno));
		closelog();
		return 1;
	}

	ssize_t len = strlen(txt);
	ssize_t written = write(fd, txt, len);
	if((written < 0) || (written != len)){
		syslog(LOG_ERR, "Failed to write on '%s': %s", filepath, strerror(errno));
		fprintf(stderr, "Error: Write failed on '%s': %s\n", filepath, strerror(errno));
		close(fd);
		closelog();
		return 1;
	}

	syslog(LOG_DEBUG, "Writing \"%s\" to \"%s\"", txt, filepath);

	close(fd);
	closelog();

	return 0;

}
