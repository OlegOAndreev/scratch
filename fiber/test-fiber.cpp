#include "common.h"

#include <memory>
#include <thread>

#include "marl-fiber.h"
#include "os-fiber.h"

template<typename FiberType>
struct FiberTestState {
    int numIterations;
    double startOutput;
    double* output;
    typename FiberType::Handle* thisFiber;
    typename FiberType::Handle* nextFiber;
};

template<typename FiberType>
void fiberTestFunc(void* arg)
{
    FiberTestState<FiberType>* state = (FiberTestState<FiberType>*)arg;
    for (int i = 0; i < state->numIterations; i++) {
        state->output[i] = i * state->startOutput;
        FiberType::switchTo(state->thisFiber, state->nextFiber);
    }
}

template<typename FiberType>
struct TrivialFiberTestState {
    int numIterations;
    typename FiberType::Handle* thisFiber;
    typename FiberType::Handle* nextFiber;
};

template<typename FiberType>
void trivialFiberTestFunc(void* arg)
{
    TrivialFiberTestState<FiberType>* state = (TrivialFiberTestState<FiberType>*)arg;

    for (int i = 0; i < state->numIterations; i++) {
        FiberType::switchTo(state->thisFiber, state->nextFiber);
    }
}

template<typename FiberType>
void doTest(const char* fiberTypeName)
{
    {
        // Create two groups of fibers on one thread, each of the groups circularly pointing
        // to the next in-group fiber. Start the first group, start the second group, see that
        // all fibers have completed.
        int const kNumIterations = 10000;
        // kNumFibers must be divisible by 2.
        int const kNumFibers = 8;
        std::unique_ptr<double[]> output{new double[kNumIterations * kNumFibers]};
        FiberTestState<FiberType> state[kNumFibers];
        typename FiberType::Handle* fibers[kNumFibers];

        for (int i = 0; i < kNumFibers; i++) {
            state[i].numIterations = kNumIterations;
            state[i].startOutput = i;
            state[i].output = &output[i * kNumIterations];
            fibers[i] = FiberType::create(256 * 1024, fiberTestFunc<FiberType>, &state[i]);
            state[i].thisFiber = fibers[i];
        }
        for (int i = 0; i < kNumFibers; i++) {
            // Make two groups of fibers: 0-2-4-... and 1-3-5-...
            state[i].nextFiber = fibers[(i + 2) % kNumFibers];
        }

        // Run one group of fibers on another thread to test if anything breaks when sharing fibers
        // between the threads.
        std::thread fiberRunnerThread([&] {
            // Run the first group of fibers.
            FiberType::switchFromThread(fibers[0]);
        });
        // Run the second group of fibers.
        FiberType::switchFromThread(fibers[1]);
        fiberRunnerThread.join();

        for (int j = 0; j < kNumFibers; j++) {
            double startOutput = j;
            for (int i = 0; i < kNumIterations; i++) {
                ENSURE(output[j * kNumIterations + i] == startOutput * i, "");
            }
        }

        for (int i = 0; i < kNumFibers; i++) {
            FiberType::destroy(fibers[i]);
        }
    }

    int64_t switchesPerSecond;
    {
        // Run trivial fibers switching to next one in a loop.
        int const kNumIterations = 1000000;
        int const kNumFibers = 10;
        TrivialFiberTestState<FiberType> state[kNumFibers];
        typename FiberType::Handle* fibers[kNumFibers];

        for (int i = 0; i < kNumFibers; i++) {
            state[i].numIterations = kNumIterations;
            fibers[i] = FiberType::create(256 * 1024, trivialFiberTestFunc<FiberType>, &state[i]);
            state[i].thisFiber = fibers[i];
        }
        for (int i = 0; i < kNumFibers; i++) {
            state[i].nextFiber = fibers[(i + 1) % kNumFibers];
        }

        int64_t startTime = getTimeTicks();
        FiberType::switchFromThread(fibers[0]);
        int64_t runTime = getTimeTicks() - startTime;
        switchesPerSecond = kNumIterations * kNumFibers * getTimeFreq() / runTime;

        for (int i = 0; i < kNumFibers; i++) {
            FiberType::destroy(fibers[i]);
        }
    }

    printf("Fiber tests for %s passed. %lld fiber switches per second\n", fiberTypeName,
           (long long)switchesPerSecond);
}

int main()
{
    doTest<OsFiber>("OsFiber");
    doTest<MarlFiber>("MarlFiber");
}
