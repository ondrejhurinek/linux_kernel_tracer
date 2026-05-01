#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>

int main() {
	printf("Press ENTER to start syscall test...\n");
    	getchar();  // waits for Enter
	syscall(SYS_write, 1, "hi\n", 3);
}
