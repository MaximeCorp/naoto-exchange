#include <iomanip>
#include <iostream>
#include <matching_engine.hpp>
#include <order.hpp>
#include <sstream>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace naoto::matching_engine;

int main(int argc, char **argv)
{
    MatchingEngine<128, 10, 100, 1000> engine(
        argc, argv, 128, 128, 1, 5, 8080, 16, 16, 128, 128, 0, 512, 1024,
        RTE_IPV4(10, 0, 0, 20), 30000, RTE_IPV4(239, 1, 1, 1), 30001,
        RTE_IPV4(239, 1, 1, 2), 30002);

    engine.StartMatchingEngine();
}
