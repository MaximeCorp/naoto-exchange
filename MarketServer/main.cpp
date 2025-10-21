#include "MarketExecution/Assets/Asset.hpp"
#include "MarketExecution/BidAsk/BidAsk.hpp"
#include "MarketExecution/Orders/Order.hpp"
#include "TopicListener/TopicListener.hpp"

using namespace MarketExecution;

int main(void)
{
    TopicListener test = TopicListener("test", 1000, 0, 10);

    test.startReadLoop();

    return 0;
}
