#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <vector>

struct A {
    int a;
    int b;
};

void fillAndIterVector(int size)
{
    uint64_t startTime = getTimeTicks();
    std::vector<std::unique_ptr<A>> container;
    for (int i = 0; i < size; i++) {
        container.push_back(std::make_unique<A>(A{i, i}));
    }
    printf("Filled vector of %d elements in %dusec\n", size,
           (int)((getTimeTicks() - startTime) * 1000000 / getTimeFreq()));

    startTime = getTimeTicks();
    int sum = 0;
    for (auto const& a : container) {
        sum += a->a + a->b;
    }
    printf("Summed vector of %d elements (%d) in %dusec\n", size, sum,
           (int)((getTimeTicks() - startTime) * 1000000 / getTimeFreq()));
}

void fillAndIterList(int size)
{
    uint64_t startTime = getTimeTicks();
    std::list<A> container;
    for (int i = 0; i < size; i++) {
        container.push_back(A{i, i});
    }
    printf("Filled list of %d elements in %dusec\n", size,
           (int)((getTimeTicks() - startTime) * 1000000 / getTimeFreq()));

    startTime = getTimeTicks();
    int sum = 0;
    for (auto const& a : container) {
        sum += a.a + a.b;
    }
    printf("Summed list of %d elements (%d) in %dusec\n", size, sum,
           (int)((getTimeTicks() - startTime) * 1000000 / getTimeFreq()));
}

int main(int argc, char** argv)
{
    std::list<A> lst;

    int size;
    if (argc == 1) {
        size = 10000;
    } else {
        size = atoi(argv[1]);
    }

    fillAndIterList(size);
    fillAndIterVector(size);
    fillAndIterVector(size);
    fillAndIterList(size);

    return 0;
}
