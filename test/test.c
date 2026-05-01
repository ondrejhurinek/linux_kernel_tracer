/* Simple program to generate system calls*/
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void) {
	printf("PID: %d\n", getpid());
	printf("Press ENTER to start...\n");
	getchar();

	// File operations
	int fd = open("test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
	write(fd, "hello\n", 6);
	fsync(fd);
	close(fd);

	// Memory allocation
	char *buf = malloc(1024);
	if (buf) free(buf);

	// Process info
	getpid();
	getppid();

	// File metadata
	struct stat st;
	stat("test.txt", &st);

	// Sleep (timing syscall)
	usleep(1000);

	printf("Done. Press ENTER to exit...\n");
	getchar();

	return 0;
}
