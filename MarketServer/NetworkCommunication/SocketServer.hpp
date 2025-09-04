#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../MarketExecution/BidAsk/BidAsk.hpp"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace MarketExecution
{
    class SocketServer
    {
    private:
        int Port;
        boost::asio::io_context Ioc;
        boost::asio::ip::tcp::acceptor Acceptor;
        std::vector<std::unique_ptr<BidAsk>> AssetMarkets;
        std::vector<
            std::shared_ptr<boost::beast::websocket::stream<tcp::socket>>>
            SubscribedSockets;

        std::mutex AssetMarketsMutex;
        std::mutex SubscribedSocketsMutex;

        void startListenLoop();
        void updateLoop();
        void startClientLoop(tcp::socket clientSocket);
        int isSubscribed(
            std::shared_ptr<boost::beast::websocket::stream<tcp::socket>> &ws);

    public:
        SocketServer(int port);
        ~SocketServer() = default;

        std::thread startServer();

        void AddAsset(int totalSupply, int assetId, float initialPrice);
    };

} // namespace MarketExecution
