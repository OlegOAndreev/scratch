#include "common.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <thread>

#include "testfixedfunction.h"
#include "testpools.h"
#include "testqueues.h"


void printUsage(const char* argv0)
{
    printf("Usage: %s [options] [test names]\n"
           "Options:\n"
           "\t--num-threads NUM\t\tSet number of threads in a pool (number of cores by default)\n"
           "Test names:\n"
           "\tfixed-function\n"
           "\tqueues\n"
           "\tsimple\n"
           "\tsimple-mpmc\n"
           "\twork-stealing\n",
           argv0);
}

int main(int argc, char** argv)
{
    int numThreads = std::thread::hardware_concurrency();
    std::set<std::string> poolNames;

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
        } else if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            printf("Unknown argument: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        } else {
            poolNames.insert(argv[i]);
            i++;
        }
    }

    if (poolNames.empty() || setContains(poolNames, "fixed-function")) {
        testFixedFunction();
    }

    if (poolNames.empty() || setContains(poolNames, "queues")) {
        testQueues(numThreads);
    }

    if (poolNames.empty() || setContains(poolNames, "simple")) {
        testPoolSimple(numThreads);
    }

    if (poolNames.empty() || setContains(poolNames, "simple-mpmc")) {
        testPoolSimpleMpMc(numThreads);
    }

    if (poolNames.empty() || setContains(poolNames, "work-stealing")) {
        testPoolWorkStealing(numThreads);
    }
}
