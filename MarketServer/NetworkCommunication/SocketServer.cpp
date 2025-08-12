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
        return std::thread(&MarketExecution::SocketServer::startListenLoop,
                           this);
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

    void SocketServer::startClientLoop(tcp::socket clientSocket)
    {
        std::cout << "new client\n";
        websocket::stream<tcp::socket> ws{ std::move(clientSocket) };
        ws.accept();

        beast::flat_buffer buffer;

        try
        {
            while (true)
            {
                buffer.consume(buffer.size());
                ws.read(buffer);

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
                            ws.write(net::buffer(response));
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
                            ws.write(net::buffer(response));
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
                        ws.write(net::buffer(response));
                    }
                    else
                    {
                        std::string response = "\"" + msg + "\" is invalid\n";
                        ws.write(net::buffer(response));
                    }
                }
                else
                {
                    std::string response = "\"" + msg + "\" is invalid\n";
                    ws.write(net::buffer(response));
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
        ws.close(websocket::close_code::normal, ec);

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
