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
    // Queue depths, pool capacities (incl. skip list nodes / order nodes)
    // and batch sizes are all compile-time now: see system_conf.hpp.
    auto engine = std::make_unique<MatchingEngine>(
        argc, argv,
        /*assetId=*/1, /*initialPrice=*/5, /*port=*/8080,
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

    engine->StartMatchingEngine();
}
