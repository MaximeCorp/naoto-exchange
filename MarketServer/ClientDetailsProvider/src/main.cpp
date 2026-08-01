#include <ClientDetailsProvider.hpp>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace AccountService;

int main(int argc, char **argv)
{
    ClientDetailsProvider<126, 16, 128, 32> test(
        argc, argv, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 8082, 16, 16, 0,
        256, 1024, RTE_IPV4(10, 0, 0, 20), 30000);
}
