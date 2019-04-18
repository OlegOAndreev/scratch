#include "common.h"

#include <cmath>
#include <string>

#include "fixedfunction.h"

void testFixedFunction()
{
    int src, dst;
    // Should have capture with sizeof == 2 * sizeof(int*).
    FixedFunction<void()> proc([&src, &dst] { dst = src; });
    ENSURE(!proc.empty(), "");
    src = 1;
    proc();
    ENSURE(dst == 1, "");
    src = 123;
    proc();
    ENSURE(dst == 123, "");

    FixedFunction<void()> movedProc(std::move(proc));
    ENSURE(!movedProc.empty(), "");
    ENSURE(proc.empty(), "");
    src = 456;
    movedProc();
    ENSURE(dst == 456, "");

    FixedFunction<void()> moveAssignedProc;
    ENSURE(moveAssignedProc.empty(), "");
    moveAssignedProc = std::move(movedProc);
    ENSURE(!moveAssignedProc.empty(), "");
    ENSURE(movedProc.empty(), "");
    src = 789;
    moveAssignedProc();
    ENSURE(dst == 789, "");

    double coeff = 1.0;
    FixedFunction<double(double)> computeFunc1([&coeff] (double x) { return sqrt(x) * coeff; });
    ENSURE(computeFunc1(1.0) == 1.0, "");
    ENSURE(computeFunc1(4.0) == 2.0, "");
    coeff = 3.0;
    ENSURE(computeFunc1(1.0) == 3.0, "");

    FixedFunction<double(double)> computeFunc2(sqrt);
    ENSURE(computeFunc2(1.0) == 1.0, "");
    ENSURE(computeFunc2(4.0) == 2.0, "");

    struct RatherBigStruct {
        double d1 = 1.0;
        double d2 = 2.0;
        double d3 = 3.0;
        double d4 = 4.0;
        double d5 = 5.0;
        double d6 = 6.0;
        double d7 = 7.0;
    };

    FixedFunction<double(double)> smallAndBigFunc1([](double param) { return param + 1.0; });
    ENSURE(smallAndBigFunc1(0.0) == 1.0, "");
    ENSURE(smallAndBigFunc1(1.0) == 2.0, "");

    RatherBigStruct rbs;
    FixedFunction<double(double)> smallAndBigFunc2([=](double param) {
        return rbs.d1 + rbs.d2 + rbs.d3 + rbs.d4 + rbs.d5 + rbs.d6 + rbs.d7 + param;
    });
    ENSURE(smallAndBigFunc2(0.0) == 28.0, "");

    FixedFunction<double(double)> smallAndBigFunc3;
    smallAndBigFunc3 = std::move(smallAndBigFunc2);
    smallAndBigFunc2 = std::move(smallAndBigFunc1);
    ENSURE(smallAndBigFunc2(0.0) == 1.0, "");
    ENSURE(smallAndBigFunc3(0.0) == 28.0, "");

    FixedFunction<std::string(std::string)> copyStringFunc([](std::string s) {
        return s + "abc";
    });
    ENSURE(copyStringFunc("123") == "123abc", "");

    FixedFunction<std::string(std::string const&)> crefStringFunc([](std::string const& s) {
        return s + "abcd";
    });

    ENSURE(crefStringFunc("1234") == "1234abcd", "");

    FixedFunction<void(std::string const&, std::string&)> refStringFunc(
                [](std::string const& srcs, std::string& dsts) {
        dsts = srcs + "abcde";
    });

    std::string out;
    refStringFunc("12345", out);
    ENSURE(out == "12345abcde", "");

    printf("FixedFunction tests passed\n");
    printf("=====\n");
}
