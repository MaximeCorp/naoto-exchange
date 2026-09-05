#include <iomanip>
#include <iostream>
#include <matching_engine.hpp>
#include <order.hpp>
#include <sstream>
#include <system_conf.hpp>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace naoto;
using namespace naoto::matching_engine;

int main(int argc, char **argv)
{
    MatchingEngine<MeEpollReceiveBatchSize, MeSkipListMaxLevel, MeFastMapSize,
                   MeOrderMapSize>
        engine(
            argc, argv,
            /*queueSize=*/MeOrderStateQueueSize, // ==
                                                 // MeOrderBookUpdateQueueSize;
                                                 // one param currently backs
                                                 // both
            /*skipListNodesPoolSize=*/MeMaxPriceLevels, // one skip list node
                                                        // per resting price
                                                        // level
            /*assetId=*/1, /*initialPrice=*/5, /*port=*/8080,
            /*maxEvents=*/16, // epoll_wait batch size, not a capacity limit -
                              // deployment-tuned, not derived from config
            /*maxPending=*/16, // listen() backlog, same as above
            /*nb_fds=*/1024, // only order gateways connect to a
                             // matching engine
            /*orderNodePoolSize=*/MeMaxOrderNodes,
            /*portId=*/0, // DPDK NIC port id - deployment-specific
            /*nbTxQueueSlots=*/512, // DPDK NIC TX ring depth - no
                                    // system_conf equivalent, manually
                                    // tuned to the NIC
            /*poolSize=*/MeOrderStateQueueSize
                + MeOrderBookUpdateQueueSize, // mbuf pool backs both
                                              // outgoing queues combined
            /*srcIp=*/RTE_IPV4(10, 0, 0, 20), /*srcPort=*/30000,
            /*dstOrderIp=*/RTE_IPV4(239, 1, 1, 1), /*dstOrderPort=*/30001,
            /*dstBookIp=*/RTE_IPV4(239, 1, 1, 2), /*dstBookPort=*/30002);

    engine.StartMatchingEngine();
}
