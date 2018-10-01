#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "common.h"
#include "betterthreadpool.h"
#include "simplethreadpool.h"

#define ASSERT_THAT(expr) \
    do { \
        if (!(expr)) { \
            printf("FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        } \
    } while (0)

// Simplest sanity checks for SimpleThreadPool.
template<typename TP>
void basicTests(TP* stp)
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

    auto future3 = stp->submit((double(*)(double))sqrt, 1.0);
    ASSERT_THAT(future3.get() == 1.0);

    printf("Basic tests passed\n");
}

struct TinyJobInput {
    double start = 0.0;
    size_t iters = 0;
};

double tinyJob(TinyJobInput const& input)
{
    double ret = 0.0;
    for (size_t i = 0; i < input.iters; i++) {
        ret += cos(i * input.start);
    }
    return ret;
}

void prepareTinyJobInput(size_t numJobsPerBatch, size_t numItersPerJob, std::vector<TinyJobInput>* jobInput)
{
    jobInput->resize(numJobsPerBatch);
    for (size_t i = 0; i < numJobsPerBatch; i++) {
        TinyJobInput& in = (*jobInput)[i];
        in.iters = numItersPerJob;
        in.start = M_PI / (i + 1);
    }
}

template<typename TP>
void tinyJobsTest(TP* stp, size_t numItersPerJob)
{
    static const size_t NUM_JOBS_PER_BATCH = 1000;

    std::vector<TinyJobInput> jobInput;
    prepareTinyJobInput(NUM_JOBS_PER_BATCH, numItersPerJob, &jobInput);

    std::vector<std::future<double>> futures;
    futures.resize(NUM_JOBS_PER_BATCH);

    // Run the test for ~5 seconds.
    int64_t testStartTime = getTimeCounter();
    int64_t timeFreq = getTimeFreq();
    std::vector<int64_t> jobsPerSec;
    while (getTimeCounter() - testStartTime < timeFreq * 3) {
        int64_t batchStartTime = getTimeCounter();
        // Run a batch of tiny jobs and wait for them to complete.
        for (size_t i = 0; i < NUM_JOBS_PER_BATCH; i++) {
            futures[i] = stp->submit(tinyJob, jobInput[i]);
        }

        for (size_t i = 0; i < NUM_JOBS_PER_BATCH; i++) {
            futures[i].wait();
        }
        jobsPerSec.push_back(timeFreq * NUM_JOBS_PER_BATCH / (getTimeCounter() - batchStartTime));
    }
    std::sort(jobsPerSec.begin(), jobsPerSec.end());
    int64_t avgJobsPerSec = simpleAverage(jobsPerSec);
    int64_t medianJobsPerSec = jobsPerSec[jobsPerSec.size() / 2];
    int64_t maxJobsPerSec = jobsPerSec.back();
    printf("Tiny job test with jobs with %d iters for SimpleThreadPool: avg %lld, median %lld, max %lld jobs per sec\n",
           (int)numItersPerJob, (long long)avgJobsPerSec, (long long)medianJobsPerSec, (long long)maxJobsPerSec);
}

void printUsage(const char* argv0)
{
    printf("Usage: %s [options] [pool names]\n"
           "Options:\n"
           "\t--num-threads NUM\t\tSet number of threads in a pool (number of cores by default)\n"
           "Pool names:\n"
           "\tsimple\n"
           "\tbetter\n",
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

    if (poolNames.empty() || setContains(poolNames, "simple")) {
        SimpleThreadPool stp(numThreads);

        printf("Running simple pool with %d threads\n", stp.numThreads());

        basicTests(&stp);

        tinyJobsTest(&stp, 20);
        tinyJobsTest(&stp, 200);
        tinyJobsTest(&stp, 2000);
    }

    if (poolNames.empty() || setContains(poolNames, "better")) {
        BetterThreadPool btp(numThreads);

        printf("Running better pool with %d threads\n", btp.numThreads());

        basicTests(&btp);

        tinyJobsTest(&btp, 20);
        tinyJobsTest(&btp, 200);
        tinyJobsTest(&btp, 2000);
    }
}
