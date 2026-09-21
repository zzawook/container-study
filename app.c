#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <time.h>
#include <stdlib.h>

int main(void)
{
    printf("Hello from Child Container!\n");

    const size_t bytes = 128UL * 1024 * 1024;
    unsigned char *buffer = NULL;

    // TODO 1: bytes만큼 malloc()으로 요청해서 buffer에 저장.
    //         NULL이면 오류를 출력하고 종료.
    int *malloc_result = malloc(bytes);
    if (malloc_result == NULL)
    {
        perror("malloc");
        fprintf(stderr, "Failed to allocate memory. Terminating.");
        return -1;
    }
    buffer = (unsigned char *)malloc_result;

    // TODO 2: "malloc succeeded; touching memory..." 출력.
    //         실제 메모리 쓰기 전에 fflush(stdout) 수행.
    fprintf(stderr, "malloc succeeded; touching memory...\n");
    int fflush_result = fflush(stdout);
    if (fflush_result != 0)
    {
        perror("fflush");
        fprintf(stderr, "Failed to flush stdout. Terminating.");
        return fflush_result;
    }

    volatile unsigned char *touch = buffer;

    // TODO 3: 인덱스 0부터 bytes - 1까지 반복하면서
    //         touch의 각 원소에 1을 기록.
    //
    //         반복문 안에서 free()하지 않기.
    //         할당한 범위 밖에 쓰지 않기.
    for (size_t i = 0; i < bytes; ++i)
    {
        touch[i] = 1;
    }

    // TODO 4: 여기까지 왔다면
    //         "Finished touching all memory" 출력.
    fprintf(stderr, "Finished touching all memory\n");

    // TODO 5: free()로 buffer 반환.
    free(buffer);

    return 0;
}
