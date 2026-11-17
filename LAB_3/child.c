#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>

static int is_prime(int value)
{
    if (value < 2)
        return 0;

    if (value == 2)
        return 1;

    if (value % 2 == 0)
        return 0;

    for (int divisor = 3;divisor <= value / divisor;
         divisor += 2) {

        if (value % divisor == 0)
            return 0;
    }

    return 1;
}

static int int_to_string(int value, char *buffer)
{
    char temp[32];
    int pos = 0;
    int length = 0;

    unsigned int number = (unsigned int)value;

    do {
        temp[pos++] = (char)('0' + number % 10);
        number /= 10;
    } while (number != 0);

    while (pos > 0)
        buffer[length++] = temp[--pos];

    buffer[length++] = '\n';

    return length;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        say(STDERR_FILENO, "Run ./parent to start the lab\n");
        return EXIT_FAILURE;
    }
    pid_t parent = getppid();
    int status = EXIT_FAILURE, output = -1;
    sem_t *request = SEM_FAILED, *reply = SEM_FAILED;
    Mailbox *box = MAP_FAILED;
    int fd = shm_open(argv[2], O_RDWR, 0);
    if (fd == -1) {
        report("child shm_open");
        return status;
    }
    box = mmap(NULL, sizeof(*box), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (box == MAP_FAILED) {
        report("child mmap");
        goto cleanup;
    }
    request = sem_open(argv[3], 0);
    if (request == SEM_FAILED) {
        report("child sem_open request");
        goto cleanup;
    }
    reply = sem_open(argv[4], 0);
    if (reply == SEM_FAILED) {
        report("child sem_open reply");
        goto cleanup;
    }
    output = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output == -1)
        report("open output file");
    box->response = output == -1 ? FAILED : READY;
    if (sem_post(reply) == -1) {
        report("sem_post ready");
        goto cleanup;
    }
    if (output == -1)
        goto cleanup;

    for (;;) {
        if (wait_tick(request) == -1) {
            if (errno == ETIMEDOUT && getppid() == parent)
                continue;
            if (errno != ETIMEDOUT)
                report("child sem_timedwait");
            break;
        }

        int value = box->value;
        int response = CONTINUE;
        if (box->eof || value < 0 || is_prime(value)) {
            response = STOP;
        } else if (value > 1) {
            char buffer[32];
            int length = int_to_string(value, buffer);
            if (write_all(output, buffer, (size_t)length) == -1) {
                report("write output file");
                response = FAILED;
            }
        }
        if (response == STOP) {
            if (close(output) == -1) {
                report("close output file");
                response = FAILED;
            }
            output = -1;
        }
        box->response = response;
        if (sem_post(reply) == -1) {
            report("sem_post reply");
            break;
        }
        if (response != CONTINUE) {
            status = response == STOP ? EXIT_SUCCESS : EXIT_FAILURE;
            break;
        }
    }

cleanup:
    if (output != -1)
        checked_cleanup(close(output), "close output file", &status);
    if (request != SEM_FAILED)
        checked_cleanup(sem_close(request), "child sem_close request", &status);
    if (reply != SEM_FAILED)
        checked_cleanup(sem_close(reply), "child sem_close reply", &status);
    if (box != MAP_FAILED)
        checked_cleanup(munmap(box, sizeof(*box)), "child munmap", &status);
    checked_cleanup(close(fd), "child close shared memory", &status);
    return status;
}
