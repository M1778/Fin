#include <cstdio>
#include <cstdlib>

constexpr int N = 1000000;

int arr[N];

int partition(int lo, int hi) {
    int pivot = arr[hi];
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        if (arr[j] < pivot) {
            i++;
            int t = arr[i]; arr[i] = arr[j]; arr[j] = t;
        }
    }
    int t = arr[i+1]; arr[i+1] = arr[hi]; arr[hi] = t;
    return i + 1;
}

void quicksort(int lo, int hi) {
    if (lo < hi) {
        int p = partition(lo, hi);
        quicksort(lo, p - 1);
        quicksort(p + 1, hi);
    }
}

int main() {
    unsigned seed = 12345;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245 + 12345;
        arr[i] = (int)(seed / 65536) % 1000000;
    }
    quicksort(0, N - 1);
    std::printf("%d\n", arr[N/2]);
    return 0;
}
