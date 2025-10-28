#include <Asset.hpp>
#include <BidAsk.hpp>
#include <Order.hpp>
#include <TopicListener.hpp>

using namespace MarketExecution;

int main(void)
{
    Asset asset = Asset(0, 10);

    BidAsk test = BidAsk(asset, 15);

    test.AddLimitOrder(OrderSide::SELL, 16, 0, 5);
    test.AddMarketOrder(OrderSide::BUY, 0, 4);

    return 0;
}
