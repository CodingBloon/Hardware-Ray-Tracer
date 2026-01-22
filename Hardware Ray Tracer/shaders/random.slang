#pragma once
uint hash(uint3 p) {
    const uint4 primes = uint4(2246822519U, 3266489917U, 668265263U, 374761393U);
    uint h32;
    h32 = p.z + primes.w + p.x * primes.y;
    h32 = primes.z * ((h32 << 17) | (h32 >> (32 - 17)));
    h32 += p.y * primes.y;
    h32 = primes.z * ((h32 << 17) | (h32 >> (32 - 17)));
    h32 = primes.x * (h32 ^ (h32 >> 15));
    h32 = primes.y * (h32 ^ (h32 >> 13));
    return h32 ^ (h32 >> 16);
}

uint pcg(inout uint state) {
    uint prev = state * 747796405u + 2891336453u;
    uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
    state = prev;
    return (word >> 22u) ^ word;
}

float rand(inout uint seed) {
    uint r = pcg(seed);
    return float(r) * (1.F / float(0xffffffffu));
}

uint srand(uint seed, uint max) {
    uint n = pcg(seed);

    return n % max; 
}