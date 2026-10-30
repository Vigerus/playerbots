#pragma once
#include "stubpch.h"
#include <random>

inline std::mt19937& HostRandom() { static std::mt19937 random(12345); return random; }
inline uint32 urand(uint32 min, uint32 max) { return std::uniform_int_distribution<uint32>(min, max)(HostRandom()); }
inline float frand(float min, float max) { return std::uniform_real_distribution<float>(min, max)(HostRandom()); }
inline bool roll_chance_f(float chance) { return frand(0.0f, 100.0f) < chance; }
