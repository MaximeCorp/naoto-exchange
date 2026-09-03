#include <order_gateway_service.hpp>
#include <system_conf.hpp>

#define IPV4(a, b, c, d)                                                       \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8)      \
     | (uint32_t)(d))

using namespace naoto;
using namespace naoto::order_gateway;

int main(int argc, char **argv)
{
    OrderGatewayService<GatewayEpollReceiveBatchSize, MaxAssets, MaxPositions,
                        GatewayMaxClients, GatewayUpdateBufferSize,
                        TradeReportReceiveBufferSize>
        gateway(
            argc, argv,
            /*portId=*/0, // DPDK NIC port id - deployment-specific
            /*nbRxQueueSlots=*/256, // DPDK NIC RX ring depth - no
                                    // system_conf equivalent, manually
                                    // tuned to the NIC
            /*poolSize=*/TradeReportReceiveBufferSize,
            /*dstIp=*/RTE_IPV4(239, 1, 1, 1), /*dstPort=*/30001,
            /*queue_size=*/GatewayUpdateBufferSize, // shared depth for the
                                                     // gateway's internal
                                                     // SPSC handoff queues
            /*port=*/8081,
            /*maxEvents=*/16, // epoll_wait batch size, not a capacity
                              // limit - deployment-tuned, not derived
            /*maxPending=*/16); // listen() backlog, same as above

    gateway.StartGateway();
}
