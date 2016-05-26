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

unsigned processOp(unsigned value, Op op)
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
    virtual unsigned process(unsigned value) const = 0;
};

struct OpInterfaceImpl1 : public OpInterface
{
    unsigned process(unsigned value) const override
    {
        return value * 2;
    }
};

struct OpInterfaceImpl2 : public OpInterface
{
    unsigned process(unsigned value) const override
    {
        return value * 3;
    }
};

struct OpInterfaceImpl3 : public OpInterface
{
    unsigned process(unsigned value) const override
    {
        return value / 4;
    }
};

struct OpInterfaceImpl4 : public OpInterface
{
    unsigned process(unsigned value) const override
    {
        return value * 5;
    }
};

struct OpInterfaceImpl5 : public OpInterface
{
    unsigned process(unsigned value) const override
    {
        return value * 6;
    }
};

struct OpInterfaceImpl6 : public OpInterface
{
    unsigned process(unsigned value) const override
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

using FunctionImpl = std::function<unsigned(unsigned)>;

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

int kRepeatCount = 50;

unsigned runSimpleFor(unsigned* data, size_t dataSize)
{
    int ret = 0;
    int64_t timeStart = getTimeCounter();
    // A very simple (vectorized) loop to make a baseline.
    for (int i = 0; i < kRepeatCount; i++) {
        for (size_t i = 0; i < dataSize; i++) {
            ret += data[i] * 7;
        }
    }
    printf("Run %d simple iters in %d msec\n", (int)dataSize * kRepeatCount,
        (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));
    return ret;
}

unsigned runFor(unsigned* data, size_t dataSize, Op op)
{
    int ret = 0;
    int64_t timeStart = getTimeCounter();
    // A very simple (vectorized) loop to make a baseline.
    for (int i = 0; i < kRepeatCount; i++) {
        for (size_t i = 0; i < dataSize; i++) {
            ret += processOp(data[i], op);
        }
    }
    printf("Run %d iters in %d msec\n", (int)dataSize * kRepeatCount,
        (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));
    return ret;
}

unsigned runSwitchFor(unsigned* data, size_t dataSize, Op* ops, bool same)
{
    unsigned ret = 0;
    int64_t timeStart = getTimeCounter();
    for (int i = 0; i < kRepeatCount; i++) {
        for (size_t i = 0; i < dataSize; i++) {
            ret += processOp(data[i], ops[i]);
        }
    }
    printf("Run %d %s switches in %d msec\n", (int)dataSize * kRepeatCount, same ? "same" : "varying",
        (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));
    return ret;
}

unsigned runInterfaceImplFor(unsigned* data, size_t dataSize, std::unique_ptr<OpInterface>* impls, bool same)
{
    unsigned ret = 0;
    int64_t timeStart = getTimeCounter();
    for (int i = 0; i < kRepeatCount; i++) {
        for (size_t i = 0; i < dataSize; i++) {
            ret += impls[i]->process(data[i]);
        }
    }
    printf("Run %d %s vcalls in %d msec\n", (int)dataSize * kRepeatCount, same ? "same" : "varying",
        (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));
    return ret;
}

unsigned runFunctionFor(unsigned* data, size_t dataSize, FunctionImpl* functionImpls, bool same)
{
    unsigned ret = 0;
    int64_t timeStart = getTimeCounter();
    for (int i = 0; i < kRepeatCount; i++) {
        for (size_t i = 0; i < dataSize; i++) {
            ret += functionImpls[i](data[i]);
        }
    }
    printf("Run %d %s std::functions in %d msec\n", (int)dataSize * kRepeatCount, same ? "same" : "varying",
        (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));
    return ret;
}

unsigned runFunctionPtrFor(unsigned* data, size_t dataSize, std::unique_ptr<FunctionImpl>* functionImpls, bool same)
{
    unsigned ret = 0;
    int64_t timeStart = getTimeCounter();
    for (int i = 0; i < kRepeatCount; i++) {
        for (size_t i = 0; i < dataSize; i++) {
            ret += (*functionImpls[i])(data[i]);
        }
    }
    printf("Run %d %s std::function ptrs in %d msec\n", (int)dataSize * kRepeatCount, same ? "same" : "varying",
        (int)((getTimeCounter() - timeStart) * 1000 / getTimeFreq()));
    return ret;
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

    printf("Running on %d elements (repeating %d times)\n", (int)dataSize, kRepeatCount);
    std::vector<unsigned> data;
    // We do both benchmarks with one implementation/op and varying implementations/ops.
    std::vector<Op> ops;
    std::vector<Op> sameOps;
    std::vector<std::unique_ptr<OpInterface>> impls;
    std::vector<std::unique_ptr<OpInterface>> sameImpls;
    std::vector<FunctionImpl> functionImpls;
    std::vector<FunctionImpl> sameFunctionImpls;
    std::vector<std::unique_ptr<FunctionImpl>> functionImplPtrs;
    std::vector<std::unique_ptr<FunctionImpl>> sameFunctionImplPtrs;
    for (size_t i = 0; i < dataSize; i++) {
        data.push_back(rand() % 100000);
        Op op = (Op)(rand() % (unsigned)Op::OP_MAX);
        ops.push_back(op);
        sameOps.push_back(Op::OP_6);
        impls.emplace_back(makeOpInterfaceImpl(op));
        sameImpls.emplace_back(makeOpInterfaceImpl(Op::OP_6));
        functionImpls.emplace_back(makeFunctionImpl(op));
        sameFunctionImpls.emplace_back(makeFunctionImpl(Op::OP_6));
        functionImplPtrs.emplace_back(new FunctionImpl(makeFunctionImpl(op)));
        sameFunctionImplPtrs.emplace_back(new FunctionImpl(makeFunctionImpl(Op::OP_6)));
    }
    printf("Finished init\n");

    unsigned simpleResult = runSimpleFor(data.data(), dataSize);
    unsigned forResult = runFor(data.data(), dataSize, Op::OP_6);
    unsigned switchResult = runSwitchFor(data.data(), dataSize, sameOps.data(), true);
    unsigned interfaceResult = runInterfaceImplFor(data.data(), dataSize, sameImpls.data(), true);
    unsigned functionResult = runFunctionFor(data.data(), dataSize, sameFunctionImpls.data(), true);
    unsigned functionPtrResult = runFunctionPtrFor(data.data(), dataSize, sameFunctionImplPtrs.data(), true);
    if (simpleResult != forResult || simpleResult != switchResult || simpleResult != interfaceResult
        || simpleResult != functionResult || simpleResult != functionPtrResult) {
        printf("ERROR: Different results\n");
    }

    switchResult = runSwitchFor(data.data(), dataSize, ops.data(), false);
    interfaceResult = runInterfaceImplFor(data.data(), dataSize, impls.data(), false);
    functionResult = runFunctionFor(data.data(), dataSize, functionImpls.data(), false);
    functionPtrResult = runFunctionPtrFor(data.data(), dataSize, functionImplPtrs.data(), false);
    if (switchResult != interfaceResult || switchResult != functionResult || switchResult != functionPtrResult) {
        printf("ERROR: Different results\n");
    }

    return simpleResult + switchResult + interfaceResult + functionResult + functionPtrResult;
}
