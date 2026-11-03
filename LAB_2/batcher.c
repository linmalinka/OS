#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { RANGE = 2049, OFFSET = RANGE / 2, THREAD_LIMIT = 256 };

typedef struct {
    int *values;
    size_t length;
    size_t threads;
    pthread_mutex_t mutex;
    pthread_cond_t ready;
    pthread_barrier_t stage;
    int start;
} Sort;

typedef struct {
    Sort *sort;
    size_t id;
} Worker;

static void put_text(int fd, const char *text)
{
    size_t remaining = strlen(text);
    while (remaining > 0) {
        ssize_t written = write(fd, text, remaining);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            exit(EXIT_FAILURE);
        text += written;
        remaining -= (size_t)written;
    }
}

static void put_uint(int fd, uintmax_t value)
{
    char digits[sizeof(value) * CHAR_BIT + 1];
    char *digit = digits + sizeof(digits) - 1;
    *digit = '\0';
    do {
        *--digit = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0);
    put_text(fd, digit);
}

static void put_decimal(double value, unsigned precision)
{
    unsigned scale = 1;
    for (unsigned i = 0; i < precision; ++i)
        scale *= 10;
    uintmax_t whole = (uintmax_t)value;
    unsigned fraction = (unsigned)((value - (double)whole) * scale + 0.5);
    put_uint(STDOUT_FILENO, whole + fraction / scale);
    put_text(STDOUT_FILENO, ".");
    fraction %= scale;
    for (unsigned place = scale / 10; place > 0; place /= 10)
        put_uint(STDOUT_FILENO, fraction / place % 10);
}

static void report_error(const char *operation, int error)
{
    put_text(STDERR_FILENO, operation);
    put_text(STDERR_FILENO, ": ");
    put_text(STDERR_FILENO, strerror(error));
    put_text(STDERR_FILENO, "\n");
}

static void check_pthread(int error, const char *operation)
{
    if (error != 0) {
        report_error(operation, error);
        exit(EXIT_FAILURE);
    }
}

static void sort_network(Sort *sort, size_t id)
{
    for (size_t p = 1; p < sort->length; p *= 2) {
        for (size_t k = p; k != 0; k /= 2) {
            size_t first = k % p;
            size_t stride = 2 * k;
            size_t blocks = (sort->length - k - first + stride - 1) / stride;

            for (size_t slot = id; slot < blocks * k; slot += sort->threads) {
                size_t left = first + (slot / k) * stride + slot % k;
                size_t right = left + k;
                if (right < sort->length && left / (2 * p) == right / (2 * p)
                    && sort->values[left] > sort->values[right]) {
                    int value = sort->values[left];
                    sort->values[left] = sort->values[right];
                    sort->values[right] = value;
                }
            }

            if (sort->threads > 1) {
                int result = pthread_barrier_wait(&sort->stage);
                if (result != PTHREAD_BARRIER_SERIAL_THREAD)
                    check_pthread(result, "pthread_barrier_wait");
            }
        }
    }
}

static void *worker_main(void *argument)
{
    Worker *worker = argument;
    Sort *sort = worker->sort;
    check_pthread(pthread_mutex_lock(&sort->mutex), "pthread_mutex_lock");
    while (sort->start == 0)
        check_pthread(pthread_cond_wait(&sort->ready, &sort->mutex), "pthread_cond_wait");
    int start = sort->start;
    check_pthread(pthread_mutex_unlock(&sort->mutex), "pthread_mutex_unlock");

    if (start > 0)
        sort_network(sort, worker->id);
    return NULL;
}

static struct timespec now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        report_error("clock_gettime", errno);
        exit(EXIT_FAILURE);
    }
    return time;
}

static double milliseconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) * 1000.0
           + (double)(end.tv_nsec - start.tv_nsec) / 1000000.0;
}

static int parallel(int *values, size_t length, size_t threads, unsigned pause,
                    double *elapsed)
{
    Sort sort = {.values = values, .length = length, .threads = threads};
    pthread_t handles[THREAD_LIMIT - 1];
    Worker workers[THREAD_LIMIT - 1];
    check_pthread(pthread_mutex_init(&sort.mutex, NULL), "pthread_mutex_init");
    check_pthread(pthread_cond_init(&sort.ready, NULL), "pthread_cond_init");
    check_pthread(pthread_barrier_init(&sort.stage, NULL, (unsigned)threads),
                  "pthread_barrier_init");

    size_t created = 0;
    int error = 0;
    while (created + 1 < threads) {
        workers[created] = (Worker){&sort, created + 1};
        error = pthread_create(&handles[created], NULL, worker_main, &workers[created]);
        if (error != 0)
            break;
        ++created;
    }

    if (error == 0 && pause != 0) {
        put_text(STDOUT_FILENO, "PID=");
        put_uint(STDOUT_FILENO, (uintmax_t)getpid());
        put_text(STDOUT_FILENO, ", threads=");
        put_uint(STDOUT_FILENO, threads);
        put_text(STDOUT_FILENO, ", pause=");
        put_uint(STDOUT_FILENO, pause);
        put_text(STDOUT_FILENO, " s\n");
        while (pause != 0)
            pause = sleep(pause);
    }

    check_pthread(pthread_mutex_lock(&sort.mutex), "pthread_mutex_lock");
    struct timespec start = now();
    sort.start = error == 0 ? 1 : -1;
    check_pthread(pthread_cond_broadcast(&sort.ready), "pthread_cond_broadcast");
    check_pthread(pthread_mutex_unlock(&sort.mutex), "pthread_mutex_unlock");
    if (error == 0) {
        sort_network(&sort, 0);
        *elapsed = milliseconds(start, now());
    }

    for (size_t i = 0; i < created; ++i)
        check_pthread(pthread_join(handles[i], NULL), "pthread_join");
    check_pthread(pthread_barrier_destroy(&sort.stage), "pthread_barrier_destroy");
    check_pthread(pthread_cond_destroy(&sort.ready), "pthread_cond_destroy");
    check_pthread(pthread_mutex_destroy(&sort.mutex), "pthread_mutex_destroy");

    if (error != 0)
        report_error("pthread_create", error);
    return error == 0;
}

static int validate(const int *values, size_t n, size_t padded, const size_t *expected)
{
    size_t counts[RANGE] = {0};
    for (size_t i = 0; i < n; ++i) {
        int value = values[i];
        if (value < -OFFSET || value > OFFSET || (i > 0 && values[i - 1] > value))
            return 0;
        ++counts[value + OFFSET];
    }
    for (size_t i = n; i < padded; ++i)
        if (values[i] != INT_MAX)
            return 0;
    for (size_t i = 0; i < RANGE; ++i)
        if (counts[i] != expected[i])
            return 0;
    return 1;
}

static uint32_t random_next(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    return state ^ (state << 5);
}

static int benchmark(size_t n, size_t threads, uint32_t seed, unsigned pause)
{
    size_t padded = 1;
    while (padded < n)
        padded *= 2;
    int *seq = malloc(padded * sizeof(*seq));
    int *par = malloc(padded * sizeof(*par));
    if (seq == NULL || par == NULL) {
        put_text(STDERR_FILENO, "Cannot allocate two arrays of ");
        put_uint(STDERR_FILENO, padded);
        put_text(STDERR_FILENO, " integers\n");
        free(seq);
        free(par);
        return 0;
    }

    size_t expected[RANGE] = {0};
    uint32_t state = seed;
    for (size_t i = 0; i < n; ++i) {
        state = random_next(state);
        unsigned bucket = state % RANGE;
        seq[i] = (int)bucket - OFFSET;
        ++expected[bucket];
    }
    for (size_t i = n; i < padded; ++i)
        seq[i] = INT_MAX;
    memcpy(par, seq, padded * sizeof(*seq));

    Sort serial = {.values = seq, .length = padded, .threads = 1};
    struct timespec start = now();
    sort_network(&serial, 0);
    double t_seq = milliseconds(start, now());
    double t_par = 0.0;
    int ok = parallel(par, padded, threads, pause, &t_par);
    if (ok && (!validate(seq, n, padded, expected) || !validate(par, n, padded, expected))) {
        put_text(STDERR_FILENO, "Validation failed\n");
        ok = 0;
    }
    if (ok) {
        put_text(STDOUT_FILENO, "n = ");
        put_uint(STDOUT_FILENO, n);
        put_text(STDOUT_FILENO, "\npadded = ");
        put_uint(STDOUT_FILENO, padded);
        put_text(STDOUT_FILENO, "\nseed = ");
        put_uint(STDOUT_FILENO, seed);
        put_text(STDOUT_FILENO, "\nVersion: sequential\n\tthreads: 1\n\t");
        put_text(STDOUT_FILENO, "ms: ");
        put_decimal(t_seq, 6);
        put_text(STDOUT_FILENO, "\n\tspeedup: 1.000\n\teffeciency: 100.00%\nVersion: parallel\n\tthreads: ");
        put_uint(STDOUT_FILENO, threads);
        put_text(STDOUT_FILENO, "\n\tms: ");
        put_decimal(t_par, 6);
        if (t_par > 0.0 && t_seq > 0.0) {
            double speedup = t_seq / t_par;
            put_text(STDOUT_FILENO, "\n\tspeedup: ");
            put_decimal(speedup, 3);
            put_text(STDOUT_FILENO, "\n\tefficiency: ");
            put_decimal(100.0 * speedup / (double)threads, 2);
            put_text(STDOUT_FILENO, "%\n");
        } else {
            put_text(STDOUT_FILENO, "\tn/a\tn/a\n");
        }
    }

    free(seq);
    free(par);
    return ok;
}

static uintmax_t parse(const char *text, uintmax_t min, uintmax_t max)
{
    char *end;
    errno = 0;
    uintmax_t value = strtoumax(text, &end, 10);
    if (*text < '0' || *text > '9' || *end != '\0' || errno != 0
        || value < min || value > max) {
        put_text(STDERR_FILENO, "Invalid number '");
        put_text(STDERR_FILENO, text);
        put_text(STDERR_FILENO, ")\n");
        exit(EXIT_FAILURE);
    }
    return value;
}

int main(int argc, char **argv)
{
    const size_t max_n = SIZE_MAX / sizeof(int) / 4;
    if (argc < 3 || argc > 5) {
        put_text(STDERR_FILENO, "N MAX_THREADS [SEED [PAUSE_SECONDS]]\n"
                                "MAX_THREADS includes the main thread and is capped at ");
        put_uint(STDERR_FILENO, THREAD_LIMIT);
        put_text(STDERR_FILENO, ".\n");
        return EXIT_FAILURE;
    }
    size_t n = (size_t)parse(argv[1], 0, max_n);
    size_t threads = (size_t)parse(argv[2], 1, THREAD_LIMIT);
    uint32_t seed = argc >= 4 ? (uint32_t)parse(argv[3], 1, UINT32_MAX) : 42;
    unsigned pause = argc == 5 ? (unsigned)parse(argv[4], 0, 300) : 0;
    return benchmark(n, threads, seed, pause) ? EXIT_SUCCESS : EXIT_FAILURE;
}
