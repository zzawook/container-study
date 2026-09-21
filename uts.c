#define _GNU_SOURCE

#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char hostname[256];
    const char *new_name = "practice-box";

    // TODO 1: 현재 hostname을 hostname 배열에 읽어온 뒤 출력하기.
    //         읽기에 실패하면 오류를 출력하고 종료하기.
    
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        printf("Cannot read current hostname. Terminating");
        return 1;
    }
    printf("Current hostname: %s\n", hostname);

    // TODO 2: 현재 프로세스의 UTS namespace를 새로 분리하기.
    //         실패하면 반드시 여기서 종료하기.
    
    int flags = CLONE_NEWUTS;
    int unshareResult = unshare(flags);

    if (unshareResult != 0) {
        printf("Cannot unshare. Terminating");
        perror("unshare");
        return unshareResult;
    };

    // TODO 3: 분리된 공간의 hostname을 new_name으로 변경하기.
    //         실패하면 오류를 출력하고 종료하기.
    
    int setResult = sethostname(new_name, strlen(new_name));

    if (setResult != 0) {
        printf("Failed to set hostname with code: %d", setResult);
        perror("sethostname");
        return setResult;
    }

    // TODO 4: hostname을 다시 읽어와 출력하기.
    //         읽기에 실패하면 오류를 출력하고 종료하기.
    
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        printf("Cannot read new hostname. Terminating.");
        return 1;
    }

    printf("New hostname: %s\n", hostname);

    // TODO 5: 실행할 hostname 프로그램에 전달할 인자 배열 만들기.
    char *argv[2];
    argv[0] = "hostname";
    argv[1] = NULL;


    // TODO 6: 지금까지 printf()한 내용이 남아 있지 않도록 출력 버퍼 비우기.
    //         실패하면 오류를 출력하고 종료하기.
    if (fflush(stdout) != 0) {
        perror("flush");
        printf("Could not flush buffer. Terminating");
        return -1;
    }


    // TODO 7: execv()로 현재 프로그램을 hostname 프로그램으로 교체하기.
    
    int execResult = execv("/bin/hostname", argv);
    if (execResult != 0) {
        printf("Error during execv");
        perror("execv");
        return execResult;
    }

    return 0;
}
