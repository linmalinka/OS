#ifndef IPC_H
#define IPC_H

#include <errno.h>
#include <semaphore.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { READY, CONTINUE, STOP, FAILED };

typedef struct {
    int value;
    int eof;
    int response;
} Mailbox;

static int write_all(int fd, const void *buffer, size_t size)
{
    const char *ptr = buffer;
    while (size > 0) {
        ssize_t count = write(fd, ptr, size);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return -1;
        ptr += count;
        size -= (size_t)count;
    }
    return 0;
}

static void say(int fd, const char *text)
{
    write_all(fd, text, strlen(text));
}

static void report(const char *operation)
{
    int error = errno;
    say(STDERR_FILENO, operation);
    say(STDERR_FILENO, ": ");
    say(STDERR_FILENO, strerror(error));
    say(STDERR_FILENO, "\n");
}

static int wait_tick(sem_t *sem)
{
    struct timespec deadline;
    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1)
        return -1;
    ++deadline.tv_sec;
    int result;
    do {
        result = sem_timedwait(sem, &deadline);
    } while (result == -1 && errno == EINTR);
    return result;
}

static void checked_cleanup(int result, const char *operation, int *status)
{
    if (result == -1) {
        report(operation);
        *status = 1;
    }
}

#endif
