#include <boost/asio.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
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
        std::mutex AssetMarketsMutex;

        void startListenLoop();
        void startClientLoop(tcp::socket clientSocket);

    public:
        SocketServer(int port);
        ~SocketServer() = default;

        std::thread startServer();

        void AddAsset(int totalSupply, int assetId, float initialPrice);
    };

} // namespace MarketExecution
