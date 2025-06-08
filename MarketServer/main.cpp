#include "MarketExecution/Assets/Asset.hpp"
#include "MarketExecution/BidAsk/BidAsk.hpp"
#include "MarketExecution/Orders/Order.hpp"
#include "NetworkCommunication/SocketServer.hpp"

using namespace MarketExecution;

int main(void)
{
    SocketServer *server = new SocketServer(8080);
    server->AddAsset(0, 1, 5);

    server->startServer().join();

    delete server;

    return 0;
}
