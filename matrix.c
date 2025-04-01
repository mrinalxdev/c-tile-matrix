#include "matrix.h"
#include "threadpool.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

Matrix *matrix_create(int rows, int cols) {
    Matrix *matrix = malloc(sizeof(Matrix));
    matrix->rows = rows;
    matrix->cols = cols;

    matrix->data = malloc(sizeof(double *) * rows);
    for (int i = 0; i < rows; i++) {
        matrix->data[i] = malloc(sizeof(double) * cols);
    }

    return matrix;
}

void matrix_free(Matrix *matrix) {
    for (int i = 0; i < matrix->rows; i++) {
        free(matrix->data[i]);
    }
    free(matrix->data);
    free(matrix);
}

void matrix_fill_random(Matrix *matrix) {
    srand(time(NULL));
    for (int i = 0; i < matrix->rows; i++) {
        for (int j = 0; j < matrix->cols; j++) {
            matrix->data[i][j] = (double)rand() / RAND_MAX * 10.0;
        }
    }
}

void matmul_worker(void *arg) {
    MatMulTask *task = (MatMulTask *)arg;
    Matrix *a = task->a;
    Matrix *b = task->b;
    Matrix *result = task->result;
    int tile_size = task->tile_size;

    for (int i = task->start_row; i < task->end_row; i += tile_size) {
        for (int j = 0; j < b->cols; j += tile_size) {
            for (int k = 0; k < a->cols; k += tile_size) {
                // Multiply tiles A[i..i+ts][k..k+ts] and B[k..k+ts][j..j+ts]
                int i_end = (i + tile_size) < a->rows ? (i + tile_size) : a->rows;
                int j_end = (j + tile_size) < b->cols ? (j + tile_size) : b->cols;
                int k_end = (k + tile_size) < a->cols ? (k + tile_size) : a->cols;

                for (int ii = i; ii < i_end; ii++) {
                    for (int jj = j; jj < j_end; jj++) {
                        double sum = 0.0;
                        for (int kk = k; kk < k_end; kk++) {
                            sum += a->data[ii][kk] * b->data[kk][jj];
                        }
                        result->data[ii][jj] += sum;
                    }
                }
            }
        }
    }

    free(task);
}

void matrix_multiply_tiled(Matrix *a, Matrix *b, Matrix *result, int tile_size, ThreadPool *pool) {
    for (int i = 0; i < result->rows; i++) {
        for (int j = 0; j < result->cols; j++) {
            result->data[i][j] = 0.0;
        }
    }

    int rows_per_thread = (a->rows + pool->num_workers - 1) / pool->num_workers;

    for (int i = 0; i < a->rows; i += rows_per_thread) {
        MatMulTask *task = malloc(sizeof(MatMulTask));
        task->a = a;
        task->b = b;
        task->result = result;
        task->tile_size = tile_size;
        task->start_row = i;
        task->end_row = (i + rows_per_thread) < a->rows ? (i + rows_per_thread) : a->rows;

        threadpool_add_task(pool, matmul_worker, task);
    }
}
