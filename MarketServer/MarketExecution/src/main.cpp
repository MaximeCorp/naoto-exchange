#include <Asset.hpp>
#include <BidAsk.hpp>
#include <EpollServer.hpp>
#include <Order.hpp>
#include <StoragePool.hpp>
#include <TopicProducer.hpp>
#include <array>
#include <atomic>
#include <thread>

using namespace MarketExecution;

int main(void)
{
    boost::lockfree::queue<Order *> ordersQueue(128);
    boost::lockfree::queue<Order *> statusQueue(128);
    StoragePool<Order> orderPool(1024);
    StoragePool<std::array<char, sizeof(Order)>> binaryPool(1024);

    std::atomic<bool> running;

    EpollServer server = EpollServer(1234, 64, 128, &orderPool);

    Asset marketAsset = Asset(1, 2);

    BidAsk matchingEngine = BidAsk(marketAsset, 15.0);

    TopicProducer test =
        TopicProducer("kafka:9092", "ORDER_STATUS", &orderPool, &binaryPool);

    std::thread tcpServerThread(startSocketLoop, std::ref(server),
                                std::ref(ordersQueue));
    std::thread mathchingThread(activateOrdersLoop, std::ref(matchingEngine),
                                std::ref(ordersQueue), std::ref(statusQueue),
                                std::ref(running));

    tcpServerThread.detach();
    mathchingThread.detach();

    test.startConsumingStatusQueue(statusQueue, running);

    return 0;
}
