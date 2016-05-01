#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#if defined(__APPLE__)
#include <sys/time.h>
#elif defined(__linux__)
#include <time.h>
#endif

int64_t getTimeCounter()
{
#if defined(__APPLE__)
    timeval tp;
    gettimeofday(&tp, nullptr);
    return tp.tv_sec * 1000000ULL + tp.tv_usec;
#elif defined(__linux__)
    timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1000000000ULL + tp.tv_nsec;
#endif
}

int64_t getTimeFreq()
{
#if defined(__APPLE__)
    return 1000000;
#elif defined(__linux__)
    return 1000000000;
#endif
}

// x[i] = horizontal position of queen on i-th vertical
int naiveIter(int* x, int y, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        bool good = true;
        for (int j = 0; j < y; j++) {
            if (x[j] == i) {
                good = false;
                break;
            }
            if (abs(x[j] - i) == abs(j - y)) {
                good = false;
                break;
            }
        }
        if (!good) {
            continue;
        }
        if (y == n - 1) {
            total++;
        } else {
            x[y] = i;
            total += naiveIter(x, y + 1, n);
        }
    }
    return total;
}

int naive(int n)
{
    std::vector<int> x(n);
    return naiveIter(x.data(), 0, n);
}

// x[i] = horizontal position of queen on i-th vertical, usedy[i] = true if y-th horizontal position is already used.
int naive2Iter(int* x, char* usedy, int y, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (usedy[i]) {
            continue;
        }
        bool good = true;
        for (int j = 0; j < y; j++) {
            if (abs(x[j] - i) == abs(j - y)) {
                good = false;
                break;
            }
        }
        if (!good) {
            continue;
        }
        if (y == n - 1) {
            total++;
        } else {
            x[y] = i;
            usedy[i] = true;
            total += naive2Iter(x, usedy, y + 1, n);
            usedy[i] = false;
        }
    }
    return total;
}

int naive2(int n)
{
    std::vector<int> x(n);
    // Oh std::vector<bool>, what the fuck are you!
    std::vector<char> usedy(n);
    return naive2Iter(x.data(), usedy.data(), 0, n);
}

// field[n * i + j] = number of queen hitting the (i, j) square. 
int fillingIter(char* field, int y, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (field[n * y + i] > 0) {
            continue;
        }
        if (y == n - 1) {
            total++;
        } else {
            // Fill two half-diagonals and horizontal.
            for (int j = y + 1, k = i + 1; j < n && k < n; j++, k++) {
                field[n * j + k]++;
            }
            for (int j = y + 1, k = i - 1; j < n && k >= 0; j++, k--) {
                field[n * j + k]++;
            }
            for (int j = y + 1; j < n; j++) {
                field[n * j + i]++;
            }
            total += fillingIter(field, y + 1, n);
            // Unfill two half-diagonals and horizontal.
            for (int j = y + 1, k = i + 1; j < n && k < n; j++, k++) {
                field[n * j + k]--;
            }
            for (int j = y + 1, k = i - 1; j < n && k >= 0; j++, k--) {
                field[n * j + k]--;
            }
            for (int j = y + 1; j < n; j++) {
                field[n * j + i]--;
            }
        }
    }
    return total;
}

int filling(int n)
{
    std::vector<char> field(n * n);
    return fillingIter(field.data(), 0, n);
}

// field[n * i + j] = number of queen hitting the (i, j) square. 
int fillingIter2(char* field, char* usedy, int y, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (usedy[i]) {
            continue;
        }
        if (field[n * y + i] > 0) {
            continue;
        }
        if (y == n - 1) {
            total++;
        } else {
            // Fill two half-diagonals.
            for (int j = y + 1, k = i + 1; j < n && k < n; j++, k++) {
                field[n * j + k]++;
            }
            for (int j = y + 1, k = i - 1; j < n && k >= 0; j++, k--) {
                field[n * j + k]++;
            }
            usedy[i] = 1;
            total += fillingIter2(field, usedy, y + 1, n);
            // Unfill two half-diagonals.
            for (int j = y + 1, k = i + 1; j < n && k < n; j++, k++) {
                field[n * j + k]--;
            }
            for (int j = y + 1, k = i - 1; j < n && k >= 0; j++, k--) {
                field[n * j + k]--;
            }
            usedy[i] = 0;
        }
    }
    return total;
}

int filling2(int n)
{
    std::vector<char> field(n * n);
    std::vector<char> usedy(n);
    return fillingIter2(field.data(), usedy.data(), 0, n);
}

// field[n * i + j] = number of queen hitting the (i, j) square. 
int copyingIter(char* field, int y, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (field[n * y + i] > 0) {
            continue;
        }
        if (y == n - 1) {
            total++;
        } else {
            std::vector<char> newField(field, field + n * n);
            // Fill two half-diagonals and horizontal.
            for (int j = y + 1, k = i + 1; j < n && k < n; j++, k++) {
                newField[n * j + k] = 1;
            }
            for (int j = y + 1, k = i - 1; j < n && k >= 0; j++, k--) {
                newField[n * j + k] = 1;
            }
            for (int j = y + 1; j < n; j++) {
                newField[n * j + i]++;
            }
            total += copyingIter(newField.data(), y + 1, n);
        }
    }
    return total;
}

int copying(int n)
{
    std::vector<char> field(n * n);
    return copyingIter(field.data(), 0, n);
}

// field[n * i + j] = number of queen hitting the (i, j) square. 
int copyingIter2(char* field, int y, int n)
{
    int total = 0;
    for (int i = 0; i < n; i++) {
        if (field[n * y + i] > 0) {
            continue;
        }
        if (y == n - 1) {
            total++;
        } else {
            // Limit the max size of field to ~31x31.
            char newField[1000];
            memcpy(newField, field, n * n);
            // Fill two half-diagonals and horizontal.
            for (int j = y + 1, k = i + 1; j < n && k < n; j++, k++) {
                newField[n * j + k] = 1;
            }
            for (int j = y + 1, k = i - 1; j < n && k >= 0; j++, k--) {
                newField[n * j + k] = 1;
            }
            for (int j = y + 1; j < n; j++) {
                newField[n * j + i]++;
            }
            total += copyingIter2(newField, y + 1, n);
        }
    }
    return total;
}

int copying2(int n)
{
    std::vector<char> field(n * n);
    return copyingIter2(field.data(), 0, n);
}

template <typename Func>
void runTest(const char* name, int n, const Func& func)
{
    printf("Running %s\n", name);
    int64_t timeFreq = getTimeFreq();
    int64_t startTime = getTimeCounter();
    int result = func(n);
    int64_t deltaTime = getTimeCounter() - startTime;
    int iters = 1;
    while (deltaTime < timeFreq) {
        result = func(n);
        deltaTime = getTimeCounter() - startTime;
        iters++;
    }
    printf("Got %d in %g msec\n", result, (deltaTime * 1000.0 / (timeFreq * iters)));
}

int main(int argc, char** argv)
{
    if (argc != 2) {
        printf("Usage: %s N\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    printf("Running N = %d queens\n", n);
    runTest("naive", n, naive);
    runTest("naive2", n, naive2);
    runTest("filling", n, filling);
    runTest("filling2", n, filling2);
    runTest("copying", n, copying);
    runTest("copying2", n, copying2);
    return 0;
}
