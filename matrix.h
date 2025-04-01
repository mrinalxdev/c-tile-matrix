#ifndef MATRIX_H
#define MATRIX_H

#include "threadpool.h"  // Add this line to get ThreadPool definition

typedef struct {
    double **data;
    int rows;
    int cols;
} Matrix;

// Matrix operations
Matrix *matrix_create(int rows, int cols);
void matrix_free(Matrix *matrix);
void matrix_fill_random(Matrix *matrix);
void matrix_multiply_tiled(Matrix *a, Matrix *b, Matrix *result, int tile_size, ThreadPool *pool);

// Task structure for parallel multiplication
typedef struct {
    Matrix *a;
    Matrix *b;
    Matrix *result;
    int tile_size;
    int start_row;
    int end_row;
} MatMulTask;

#endif // MATRIX_H
