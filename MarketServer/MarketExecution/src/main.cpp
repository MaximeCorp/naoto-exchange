#include <Asset.hpp>
#include <BidAsk.hpp>
#include <Order.hpp>
#include <TopicListener.hpp>

using namespace MarketExecution;

int main(void)
{
    TopicListener test = TopicListener("test", 1000, 0, 10);

    test.startReadLoop();

    return 0;
}
