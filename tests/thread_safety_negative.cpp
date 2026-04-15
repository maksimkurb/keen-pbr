#include "../src/util/traced_mutex.hpp"

class SmokeCounter {
public:
    int read_without_lock() const {
        return value_;
    }

private:
    mutable keen_pbr3::TracedMutex mutex_;
    int value_ GUARDED_BY(mutex_){0};
};

int main() {
    SmokeCounter counter;
    return counter.read_without_lock();
}
