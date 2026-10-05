#pragma once

#include "../valve/valve.hpp"
#include <random>


namespace internal {

class LeakDetector{
    inline static thread_local std::mt19937 generator{std::random_device{}()};
    public:
        static bool hasLeak(const Valve& valve){
            (void) valve;
            return std::bernoulli_distribution{0.0001}(generator);
        }
};

} // namespace internal
