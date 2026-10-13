#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 200

int main(void) {
    static int a[N][N], b[N][N], c[N][N];
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            a[i][j] = i + j;
            b[i][j] = i - j;
            c[i][j] = 0;
        }
    }
    for (int i = 0; i < N; i++) {
        for (int k = 0; k < N; k++) {
            int aik = a[i][k];
            for (int j = 0; j < N; j++) {
                c[i][j] += aik * b[k][j];
            }
        }
    }
    printf("%d\n", c[N-1][N-1]);
    return 0;
}
