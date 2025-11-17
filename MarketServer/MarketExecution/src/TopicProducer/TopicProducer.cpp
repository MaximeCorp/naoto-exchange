#include <TopicProducer.hpp>

namespace MarketExecution
{
    TopicProducer::TopicProducer(
        std::string brokers, std::string topic, StoragePool<Order> *orderPool,
        StoragePool<std::array<char, sizeof(Order)>> *binaryPool)
        : OrderPool(orderPool)
        , BinaryPool(binaryPool)
    {
        Producer = nullptr;
        std::string errstr;
        RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

        if (conf->set("bootstrap.servers", brokers, errstr)
            != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting bootstrap.servers: " << errstr
                      << std::endl;
            delete conf;
            return;
        }

        if (conf->set("linger.ms", "5", errstr) != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting linger.ms: " << errstr << std::endl;
            delete conf;
            return;
        }

        if (conf->set("batch.size", "524288", errstr) != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting batch.num.bytes: " << errstr
                      << std::endl;
            delete conf;
            return;
        }

        if (conf->set("socket.nagle.disable", "true", errstr)
            != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting socket.nagle.disable: " << errstr
                      << std::endl;
            delete conf;
            return;
        }

        if (conf->set("acks", "1", errstr) != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting acks: " << errstr << std::endl;
            delete conf;
            return;
        }

        if (conf->set("retries", "2", errstr) != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting retries: " << errstr << std::endl;
            delete conf;
            return;
        }

        Producer = RdKafka::Producer::create(conf, errstr);
        if (!Producer)
        {
            std::cerr << "Failed to create producer: " << errstr << std::endl;
            delete conf;
        }

        std::cout << "Kafka Producer created successfully. Target Broker: "
                  << brokers << ", Topic: " << Topic << std::endl;

        RdKafka::Conf *topic_conf =
            RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);

        if (topic_conf->set("acks", "1", errstr) != RdKafka::Conf::CONF_OK)
        {
            std::cerr << "Error setting acks: " << errstr << std::endl;
            delete conf;
            return;
        }

        Topic = RdKafka::Topic::create(Producer, topic, topic_conf, errstr);

        if (!Topic)
        {
            std::cerr << "Failed to create topic object: " << errstr
                      << std::endl;
            // Handle error, clean up Producer and conf
            delete Producer;
            Producer = nullptr;
            delete conf;
            return;
        }

        delete conf;
        delete topic_conf;
    }
    void TopicProducer::produceOrder(Order *order)
    {
        auto arrayPtr = BinaryPool->acquire();

        char *binOrder = reinterpret_cast<char *>(arrayPtr);

        serializeOrder(*order, binOrder);

        const char *key_c_str = order->getKey();

        RdKafka::ErrorCode err = Producer->produce(
            Topic, RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
            reinterpret_cast<char *>(binOrder), sizeof(Order), key_c_str,
            MAX_KEY_LEN, nullptr);
        BinaryPool->release(arrayPtr);

        if (err != RdKafka::ERR_NO_ERROR)
        {
            std::cerr << "Failed to produce message: " << RdKafka::err2str(err)
                      << std::endl;
            if (err == RdKafka::ERR__QUEUE_FULL)
            {
                std::cout << "Producer queue full. Waiting..." << std::endl;
            }
        }
        else
        {
            std::cout << "Sent message with key " << order->getKey()
                      << std::endl;
        }
    }

    void TopicProducer::startConsumingStatusQueue(
        boost::lockfree::queue<Order *> &OrdersQueue,
        std::atomic<bool> &running)
    {
        Order *curOrder;

        while (running || !OrdersQueue.empty())
        {
            if (OrdersQueue.pop(curOrder))
            {
                produceOrder(curOrder);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }
} // namespace MarketExecution
