#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define FIB_DEV "/dev/fibonacci"

int main()
{
    long long sz;

    char buf[256];
    char write_buf[] = "testing writing";
    int offset = 100; /* TODO: try test something bigger than the limit */
    struct timespec tt1, tt2;

    int fd = open(FIB_DEV, O_RDWR);
    if (fd < 0) {
        perror("Failed to open character device");
        exit(1);
    }

    for (int i = 0; i <= offset; i++) {
        sz = write(fd, write_buf, strlen(write_buf));
        printf("Writing to " FIB_DEV ", returned the sequence %lld\n", sz);
    }

    for (int i = 0; i <= offset; i++) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_REALTIME, &tt1);
        sz = read(fd, buf, sizeof(buf));
        buf[sz] = 0;
        clock_gettime(CLOCK_REALTIME, &tt2);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%s. Execution time: %ld ns\n",
               i, buf, tt2.tv_nsec - tt1.tv_nsec);
    }

#if 0
    for (int i = offset; i >= 0; i--) {
        lseek(fd, i, SEEK_SET);
        clock_gettime(CLOCK_REALTIME, &tt1);
        sz = read(fd, buf, 1);
        clock_gettime(CLOCK_REALTIME, &tt2);
        printf("Reading from " FIB_DEV
               " at offset %d, returned the sequence "
               "%lld. Execution time: %ld ns\n",
               i, sz, tt2.tv_nsec - tt1.tv_nsec);
    }
#endif
    close(fd);
    return 0;
}
