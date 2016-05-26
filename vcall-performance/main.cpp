#include <functional>
#include <memory>
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

enum class Op
{
    OP_1,
    OP_2,
    OP_3,
    OP_4,
    OP_5,
    OP_6,
    OP_MAX
};

int processOp(int value, Op op)
{
    switch (op) {
    case Op::OP_1:
        return value * 2;
    case Op::OP_2:
        return value * 3;
    case Op::OP_3:
        return value / 4;
    case Op::OP_4:
        return value * 5;
    case Op::OP_5:
        return value * 6;
    case Op::OP_6:
        return value * 7;
    default:
        return 0;
    }
}

struct OpInterface
{
    virtual int process(int value) const = 0;
};

struct OpInterfaceImpl1 : public OpInterface
{
    int process(int value) const override
    {
        return value * 2;
    }
};

struct OpInterfaceImpl2 : public OpInterface
{
    int process(int value) const override
    {
        return value * 3;
    }
};

struct OpInterfaceImpl3 : public OpInterface
{
    int process(int value) const override
    {
        return value / 4;
    }
};

struct OpInterfaceImpl4 : public OpInterface
{
    int process(int value) const override
    {
        return value * 5;
    }
};

struct OpInterfaceImpl5 : public OpInterface
{
    int process(int value) const override
    {
        return value * 6;
    }
};

struct OpInterfaceImpl6 : public OpInterface
{
    int process(int value) const override
    {
        return value * 7;
    }
};

OpInterface* makeOpInterfaceImpl(Op op)
{
    switch (op) {
    case Op::OP_1:
        return new OpInterfaceImpl1();
    case Op::OP_2:
        return new OpInterfaceImpl2();
    case Op::OP_3:
        return new OpInterfaceImpl3();
    case Op::OP_4:
        return new OpInterfaceImpl4();
    case Op::OP_5:
        return new OpInterfaceImpl5();
    case Op::OP_6:
        return new OpInterfaceImpl6();
    default:
        return nullptr;
    }
}

using FunctionImpl = std::function<int(int)>;

FunctionImpl makeFunctionImpl(Op op)
{
    switch (op) {
    case Op::OP_1:
        return FunctionImpl([] (int value) { return processOp(value, Op::OP_1); });
    case Op::OP_2:
        return FunctionImpl([] (int value) { return processOp(value, Op::OP_2); });
    case Op::OP_3:
        return FunctionImpl([] (int value) { return processOp(value, Op::OP_3); });
    case Op::OP_4:
        return FunctionImpl([] (int value) { return processOp(value, Op::OP_4); });
    case Op::OP_5:
        return FunctionImpl([] (int value) { return processOp(value, Op::OP_5); });
    case Op::OP_6:
        return FunctionImpl([] (int value) { return processOp(value, Op::OP_6); });
    default:
        return FunctionImpl();
    }
}

int main(int argc, char** argv)
{
    srand(0);

    size_t dataSize;
    if (argc == 1) {
        dataSize = 10000000;
    } else if (argc == 2) {
        dataSize = atoi(argv[1]);
    } else {
        printf("Usage: %s [data size]\n", argv[0]);
        return 1;
    }

    printf("Running on %d elements\n", (int)dataSize);
    std::vector<int> data;
    std::vector<Op> ops;
    std::vector<std::unique_ptr<OpInterface>> impls;
    std::vector<FunctionImpl> functionImpls;
    for (size_t i = 0; i < dataSize; i++) {
        data.push_back(rand() % 100000);
        Op op = (Op)(rand() % (int)Op::OP_MAX);
        ops.push_back(op);
        impls.emplace_back(makeOpInterfaceImpl(op));
        functionImpls.emplace_back(makeFunctionImpl(op));
    }

    int64_t timeStart = getTimeCounter();
    int simplecode = 0;
    // A very simple (vectorized) loop to make a baseline.
    for (size_t i = 0; i < dataSize; i++) {
        simplecode += processOp(data[i], Op::OP_6);
    }
    printf("Run %d iters in %d msec\n", (int)dataSize, (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));

    timeStart = getTimeCounter();
    int switchcode = 0;
    for (size_t i = 0; i < dataSize; i++) {
        switchcode += processOp(data[i], ops[i]);
    }
    printf("Run %d switches in %d msec\n", (int)dataSize, (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));

    timeStart = getTimeCounter();
    int vcode = 0;
    for (size_t i = 0; i < dataSize; i++) {
        vcode += impls[i]->process(data[i]);
    }
    printf("Run %d vcalls in %d msec\n", (int)dataSize, (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));

    timeStart = getTimeCounter();
    int functioncode = 0;
    for (size_t i = 0; i < dataSize; i++) {
        functioncode += functionImpls[i](data[i]);
    }
    printf("Run %d std::functions in %d msec\n", (int)dataSize, (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));

    if (vcode != switchcode || vcode != functioncode) {
        printf("ERROR, RESULTS UNEQUAL\n");
    }

    return simplecode + switchcode + vcode + functioncode;
}
