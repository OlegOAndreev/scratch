#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <list>
#include <memory>
#include <vector>

struct A {
    int a;
    int b;
    int c;
    int d;
};

void fillAndIterVector(int size)
{
    uint64_t startTime = getTimeTicks();
    std::vector<std::unique_ptr<A>> container;
    for (int i = 0; i < size; i++) {
        container.push_back(std::make_unique<A>(A{i, i, i, i}));
    }
    printf("Filled vector of %d elements in %dusec\n", size, elapsedUsec(startTime));

    startTime = getTimeTicks();
    int sum = 0;
    for (const auto& a : container) {
        sum += a->a + a->b + a->c + a->d;
    }
    printf("Summed vector of %d elements (%d) in %dusec\n", size, sum, elapsedUsec(startTime));
}

void fillAndIterList(int size)
{
    uint64_t startTime = getTimeTicks();
    std::list<A> container;
    for (int i = 0; i < size; i++) {
        container.push_back(A{i, i, i, i});
    }
    printf("Filled list of %d elements in %dusec\n", size, elapsedUsec(startTime));

    startTime = getTimeTicks();
    int sum = 0;
    for (const auto& a : container) {
        sum += a.a + a.b + a.c + a.d;
    }
    printf("Summed list of %d elements (%d) in %dusec\n", size, sum, elapsedUsec(startTime));
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

    // Do a few iterations with different sizes. Run list and vector tests in different order.
    int sizes[] = {size, size + 100, size * 2, size};
    for (int s : sizes) {
        fillAndIterList(s);
        fillAndIterVector(s);
        fillAndIterVector(s);
        fillAndIterList(s);
        printf("-----\n");
    }

    return 0;
}
