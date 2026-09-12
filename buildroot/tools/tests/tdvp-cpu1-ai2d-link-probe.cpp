// Cross-link only; the deployed diagnostic has an ownership-aware C caller.
#include <cstdint>
extern "C" int tdvp_cpu1_ai2d_selftest(volatile uint32_t *, int (*)(void *), void *);
int main() { return tdvp_cpu1_ai2d_selftest(nullptr, nullptr, nullptr); }
