#include "common.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <thread>

void testCountWaiter();
void testQueues(int numThreads);
void testSemaphores(int numThreads);

void printUsage(const char* argv0)
{
    printf("Usage: %s [options] [test names]\n"
           "Options:\n"
           "\t--num-threads NUM\t\tSet number of threads in a pool (number of cores by default)\n"
           "\t--background-threads NUM\t\tSet number of threads doing some work in the background "
           "(0 by default)\n"
           "Test names:\n"
           "\tcountwaiter\n"
           "\tqueues\n"
           "\tsemaphore\n",
           argv0);
}

void backgroundWork()
{
    int const kSize = 1024 * 1024;
    uint64_t* n = new uint64_t[kSize];
    for (int i = 0; i < kSize; i++) {
        n[i] = i;
    }
    for (int i = 0;; i = (i + 1) % kSize) {
        n[i] = n[i / 2] * n[i / 3] - n[i / 4];
    }
}

void startBackgroundThreads(int numThreads)
{
    for (int i = 0; i < numThreads; i++) {
        new std::thread([] { backgroundWork(); });
    }
}

int main(int argc, char** argv)
{
    int numThreads = std::thread::hardware_concurrency();
    int backgroundThreads = 0;
    std::set<std::string> testNames;

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
        } else if (strcmp(argv[i], "--background-threads") == 0) {
            if (i == argc - 1) {
                printf("Missing argument for --background-threads\n");
                return 1;
            }
            backgroundThreads = atoi(argv[i + 1]);
            if (backgroundThreads <= 0) {
                printf("Positive number must be specified for --background-threaads\n");
                return 1;
            }
            i += 2;
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            printf("Unknown argument: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        } else {
            testNames.insert(argv[i]);
            i++;
        }
    }

    if (testNames.empty() || setContains(testNames, "countwaiter")) {
        testCountWaiter();
    }

    startBackgroundThreads(backgroundThreads);
    if (testNames.empty() || setContains(testNames, "queues")) {
        testQueues(numThreads);
    }

    if (testNames.empty() || setContains(testNames, "semaphore")) {
        testSemaphores(numThreads);
    }
}
