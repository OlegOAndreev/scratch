#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "simplethreadpool.h"

#define ASSERT_THAT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        } \
    } while (0) \

// Simplest sanity checks for SimpleThreadPool.
void basicTests(SimpleThreadPool* stp)
{
    auto future1 = stp->submit([] { return 1; });
    ASSERT_THAT(future1.get() == 1);

    auto lambda2 = [](int i) { return i * i; };
    std::vector<std::future<int>> futures2;
    for (int i = 0; i < 10000; i++) {
        futures2.push_back(stp->submit(lambda2, i));
    }
    for (int i = 0; i < 10000; i++) {
        ASSERT_THAT(futures2[i].get() == i * i);
    }
}

void printUsage(const char* argv0)
{
    printf("Usage: %s [options]\n"
           "Options:\n"
           "\t--num-threads NUM\t\tSet number of threads in a pool (number of cores by default)\n",
           argv0);
}

int main(int argc, char** argv)
{
    int numThreads = std::thread::hardware_concurrency();

    for (int i = 1; i < argc;) {
        if (strcmp(argv[i], "--num-threads") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --num-threads\n");
                return 1;
            }
            numThreads = atoi(argv[i + 1]);
            if (numThreads <= 0) {
                printf("Positive number must be specified for --num-threaads\n");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--help")) {
            printUsage(argv[0]);
            return 0;
        } else {
            printf("Unknown argument: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    SimpleThreadPool stp(numThreads);

    printf("Running pool with %d threads\n", stp.numThreads());

    basicTests(&stp);
}
