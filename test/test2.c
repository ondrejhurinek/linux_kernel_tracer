#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main() {
	printf("Press ENTER to start syscall test...\n");
    	getchar();  // waits for Enter
    	int fd = open("test.txt", O_CREAT | O_WRONLY, 0644);
    
    	for (int i = 0; i < 1000; i++) {
        	write(fd, "hello\n", 6);
    	}

    	close(fd);

    	return 0;
}
