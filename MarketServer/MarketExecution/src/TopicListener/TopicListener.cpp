#include <TopicListener.hpp>
#include <cstdlib>

const std::string KAFKA_BROKERS = std::string(
    std::getenv("KAFKA_BROKERS") ? std::getenv("KAFKA_BROKERS") : "");
const std::string CONSUMER_GROUP = "asset_matcher_group";

namespace MarketExecution
{
    TopicListener::TopicListener(std::string topic, int totalSupply,
                                 int assetId, float initialPrice)
        : Topic(topic)
        , AssetMarket(BidAsk(Asset(assetId, totalSupply), initialPrice))
    {}

    bool TopicListener::parseOrder(void *command, size_t n, Order *output)
    {
        const char *binary_data = static_cast<const char *>(command);

        return parseBinOrder(binary_data, n, output);
    }

    void TopicListener::startReadLoop()
    {
        RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        std::string errstr;

        if (conf->set("bootstrap.servers", KAFKA_BROKERS, errstr)
                != RdKafka::Conf::CONF_OK
            || conf->set("group.id", CONSUMER_GROUP, errstr)
                != RdKafka::Conf::CONF_OK
            || conf->set("auto.offset.reset", "earliest", errstr)
                != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Configuration error: " << errstr << std::endl;
            delete conf;
            return;
        }

        RdKafka::KafkaConsumer *consumer =
            RdKafka::KafkaConsumer::create(conf, errstr);
        if (!consumer)
        {
            std::cerr << "Failed to create consumer: " << errstr << std::endl;
            delete conf;
            return;
        }
        delete conf;

        std::vector<std::string> topics = { Topic };
        if (consumer->subscribe(topics) != RdKafka::ERR_NO_ERROR)
        {
            std::cerr << "Failed to subscribe to topic " << Topic << ": "
                      << errstr << std::endl;
            delete consumer;
            return;
        }
        std::cout << "Subscribed to topic: " << Topic
                  << ". Waiting for orders..." << std::endl;

        while (true)
        {
            RdKafka::Message *msg = consumer->consume(10);

            switch (msg->err())
            {
            case RdKafka::ERR_NO_ERROR:
                std::cout << "--- New Order Received ---" << std::endl;
                std::cout << "Topic: " << msg->topic_name() << std::endl;
                std::cout << "Key: " << (msg->key() ? *msg->key() : "N/A")
                          << std::endl;

                std::cout << "Payload Size: " << msg->len() << std::endl;
                {
                    std::string command = std::string(
                        static_cast<const char *>(msg->payload()), msg->len());
                    std::cout << "Payload: " << command << std::endl;

                    Order newOrder;

                    bool success =
                        parseOrder(msg->payload(), msg->len(), &newOrder);

                    if (!success)
                    {
                        continue;
                    }

                    newOrder.log();

                    AssetMarket.AddOrder(newOrder);
                }
                break;
            default:
                break;
            }
        }

        consumer->close();
        delete consumer;
    }
} // namespace MarketExecution
