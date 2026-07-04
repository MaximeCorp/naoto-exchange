#include <SocketGateway.hpp>

using namespace Gateways;

int main(void)
{
    SocketGateway<16, 16, 16> gateway(100, 1000, 8000, 32, 32, "localhost",
                                      8080, "localhost", 2030, 100);

    gateway.StartGateway();
}
