#include <cstdio>
#include <cstdlib>
#include <unordered_map>

int main(int argc, char** argv)
{
    double alpha = 1.5;
    if (argc == 2) {
        alpha = atof(argv[1]);
    } else if (argc > 2) {
        printf("Usage: %s [alpha]\n", argv[0]);
        return 1;
    }
    
    std::unordered_map<uintptr_t, size_t> oldPtrs;
    oldPtrs.reserve(1000);
    
    size_t i = 0;
    size_t curSize = 20;
    const size_t kMaxSize = 2 << 24;
    while (curSize < kMaxSize) {
        char* ptr = new char[curSize];
        printf("Size = %llu, ptr = %llx", (unsigned long long)curSize, (unsigned long long)ptr);
        if (oldPtrs.find((uintptr_t)ptr) != oldPtrs.end()) {
            printf(" (Same pointer as for size %llu)\n", (unsigned long long)oldPtrs[(uintptr_t)ptr]);
        } else {
            oldPtrs[(uintptr_t)ptr] = curSize;
            printf("\n");
        }
        delete[] ptr;
        curSize *= alpha;
        i++;
    }
    printf("Total iterations: %d\n", (int)i);
    
    return 0;
}
