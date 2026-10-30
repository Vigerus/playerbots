#pragma once
#include <functional>
#include <vector>

#define DECLARE_MULTICAST_DELEGATE(Name) \
    class Name \
    { \
    public: \
        template<class F> void AddLambda(F f) { calls.push_back(std::function<void()>(f)); } \
        void Broadcast() const { for (const auto& call : calls) call(); } \
    private: \
        std::vector<std::function<void()>> calls; \
    };
