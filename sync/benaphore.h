#include "common.h"

#include <atomic>

// A wrapper around Semaphore which has fast-path and configurable number of spins if the OS
// semaphore does not have a fast-path (e.g. on Windows). Described in
// https://www.haiku-os.org/legacy-docs/benewsletter/Issue1-26.html
template<int numSpins>
class Benaphore {
public:
    void post()
    {
        int wasCount = count.fetch_add(1, std::memory_order_seq_cst);
        if (wasCount < 0) {
            sema.post();
        }
    }

    void wait()
    {
        for (int i = 0; i < numSpins; i++) {
            if (count.load(std::memory_order_relaxed) > 0) {
                break;
            }
        }
        int wasCount = count.fetch_add(-1, std::memory_order_seq_cst);
        if (wasCount <= 0) {
            sema.wait();
        }
    }

private:
    std::atomic<int> count;
    Semaphore sema;
};
