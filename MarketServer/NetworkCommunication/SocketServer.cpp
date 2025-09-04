#include "SocketServer.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace MarketExecution
{
    SocketServer::SocketServer(int port)
        : Port(port)
        , Ioc()
        , Acceptor(Ioc, tcp::endpoint(tcp::v4(), port))
    {
        {
            std::lock_guard<std::mutex> lock(AssetMarketsMutex);
            AssetMarkets = std::vector<std::unique_ptr<BidAsk>>();
        }
    }

    std::thread SocketServer::startServer()
    {
        std::thread(&MarketExecution::SocketServer::updateLoop, this).detach();
        return std::thread(&MarketExecution::SocketServer::startListenLoop,
                           this);
    }

    void SocketServer::updateLoop()
    {
        std::vector<float> lastPrices = std::vector<float>();

        {
            std::lock_guard<std::mutex> lock(AssetMarketsMutex);
            for (int i = 0; i < static_cast<int>(AssetMarkets.size()); ++i)
            {
                lastPrices.push_back(AssetMarkets.at(i)->getMarketPrice());
            }
        }

        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(SubscribedSocketsMutex);
                if (SubscribedSockets.empty())
                {
                    continue;
                }
            }
            {
                std::lock_guard<std::mutex> lock(AssetMarketsMutex);

                for (int i = 0; i < static_cast<int>(AssetMarkets.size()); ++i)
                {
                    BidAsk *curMarket = AssetMarkets.at(i).get();

                    if (curMarket->getMarketPrice() != lastPrices.at(i))
                    {
                        lastPrices.at(i) = curMarket->getMarketPrice();
                        std::lock_guard<std::mutex> lock(
                            SubscribedSocketsMutex);

                        for (auto &&ws : SubscribedSockets)
                        {
                            std::stringstream ss;
                            ss << "UPDATE " << curMarket->getMarketAssetId()
                               << " " << curMarket->getMarketPrice();
                            ws->write(net::buffer(ss.str()));
                        }
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void SocketServer::startListenLoop()
    {
        while (true)
        {
            tcp::socket socket{ Ioc };
            Acceptor.accept(socket);
            std::thread(&SocketServer::startClientLoop, this, std::move(socket))
                .detach();
        }
    }

    int SocketServer::isSubscribed(
        std::shared_ptr<websocket::stream<tcp::socket>> &ws)
    {
        std::lock_guard<std::mutex> lock(SubscribedSocketsMutex);

        auto it =
            std::find(SubscribedSockets.begin(), SubscribedSockets.end(), ws);

        if (it != SubscribedSockets.end())
        {
            return std::distance(SubscribedSockets.begin(), it);
        }

        return -1;
    }

    void SocketServer::startClientLoop(tcp::socket clientSocket)
    {
        auto ws = std::make_shared<websocket::stream<tcp::socket>>(
            std::move(clientSocket));
        try
        {
            ws->accept();
            std::cout << "new client\n";
        }
        catch (...)
        {
            std::cout << "Failed connecting to a client\n";
            return;
        }

        beast::flat_buffer buffer;

        try
        {
            while (true)
            {
                buffer.consume(buffer.size());
                ws->read(buffer);

                std::string msg = beast::buffers_to_string(buffer.data());
                std::cout << "Received : " << msg << "\n\n";
                std::istringstream iss(msg);
                std::string command;
                iss >> command;

                if (command == "ADD_ORDER")
                {
                    std::string type, side;
                    float price;
                    int amount, assetId, clientId;

                    if (iss >> side >> type >> price >> amount >> assetId
                        >> clientId)
                    {
                        std::cout << "deadlock 1\n";
                        std::lock_guard<std::mutex> lock(AssetMarketsMutex);

                        std::cout << "clear\n";

                        if (AssetMarkets.size() == 0)
                        {
                            std::string response = "\"" + msg
                                + "\" is invalid: No asset to trade yet.\n";
                            ws->write(net::buffer(response));
                            continue;
                        }

                        int curMarketIndex = -1;

                        for (size_t i = 0; i < AssetMarkets.size(); i++)
                        {
                            std::cout << i << ":"
                                      << AssetMarkets[i]->getMarketAssetId()
                                      << "\n";
                            if (AssetMarkets[i]->getMarketAssetId() == assetId)
                            {
                                curMarketIndex = i;
                                break;
                            }
                        }

                        if (curMarketIndex == -1)
                        {
                            std::string response = "\"" + msg
                                + "\" is invalid: Invalid asset ID\n";
                            ws->write(net::buffer(response));
                            continue;
                        }

                        BidAsk *curMarket = AssetMarkets[curMarketIndex].get();

                        OrderType newOrderType = type == "LIMIT"
                            ? OrderType::LIMIT
                            : OrderType::MARKET;

                        OrderSide newOrderSide =
                            side == "BUY" ? OrderSide::BUY : OrderSide::SELL;

                        Order newOrder =
                            Order(newOrderType, newOrderSide, price, clientId,
                                  amount, assetId);

                        std::cout << "adding order\n";
                        curMarket->AddOrder(newOrder);

                        std::string response =
                            "\"" + msg + "\" was added to the market\n";
                        ws->write(net::buffer(response));
                    }

                    else
                    {
                        std::string response = "\"" + msg + "\" is invalid\n";
                        ws->write(net::buffer(response));
                    }
                }
                else if (command == "SUBSCRIBE")
                {
                    if (isSubscribed(ws) == -1)
                    {
                        std::lock_guard<std::mutex> lock(
                            SubscribedSocketsMutex);
                        SubscribedSockets.emplace_back(ws);
                    }
                    ws->write(net::buffer("SUBSCRIBED"));
                }
                else if (command == "UNSUBSCRIBE")
                {
                    int index = isSubscribed(ws);
                    if (index >= 0)
                    {
                        std::lock_guard<std::mutex> lock(
                            SubscribedSocketsMutex);
                        SubscribedSockets.erase(SubscribedSockets.begin()
                                                + index);
                    }

                    ws->write(net::buffer("SUBSCRIBED"));
                }
                else
                {
                    std::string response = "\"" + msg + "\" is invalid\n";
                    ws->write(net::buffer(response));
                }
            }
        }
        catch (beast::system_error const &se)
        {
            if (se.code() != websocket::error::closed)
                std::cerr << "WebSocket error: " << se.code().message()
                          << std::endl;
        }

        std::cout << "out of the loop\n";

        boost::system::error_code ec;
        ws->close(websocket::close_code::normal, ec);

        if (ec)
            std::cerr << "Error closing websocket: " << ec.message()
                      << std::endl;
    }

    void SocketServer::AddAsset(int totalSupply, int assetId,
                                float initialPrice)
    {
        std::lock_guard<std::mutex> lock(AssetMarketsMutex);

        Asset newAsset = Asset(assetId, totalSupply);

        AssetMarkets.push_back(
            std::make_unique<BidAsk>(newAsset, initialPrice));

        AssetMarkets.at(AssetMarkets.size() - 1)
            .get()
            ->StartMarketExecution()
            .detach();
    }
} // namespace MarketExecution
