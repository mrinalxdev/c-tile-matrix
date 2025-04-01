#include "threadpool.h"
#include "matrix.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h> 

double get_current_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec / 1000000.0;
}

int main(void) {
    const int size = 1024; // Matrix size (size x size)
    const int tile_size = 64; // Tile size for multiplication
    const int num_threads = 8; // Number of worker threads

    Matrix *a = matrix_create(size, size);
    Matrix *b = matrix_create(size, size);
    Matrix *result = matrix_create(size, size);

    matrix_fill_random(a);
    matrix_fill_random(b);

    ThreadPool *pool = threadpool_create(num_threads);
    if (!pool) {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }

    double start = get_current_time();
    matrix_multiply_tiled(a, b, result, tile_size, pool);

    sleep(1);

    double end = get_current_time();
    printf("Parallel multiplication with %d threads took %.3f seconds\n", num_threads, end - start);

    threadpool_destroy(pool);
    matrix_free(a);
    matrix_free(b);
    matrix_free(result);

    return 0;
}
