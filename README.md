# marketplace
## todo:

Websocket server
- subscribe: subroutine consume orderstatus topic, connect to the right service with gRPC and send the order status
- pair clientID/websocket instance on redis and on a dict locally

Order execution
- Publish to kafka orderstatus after order executed
- Make gRPC contract for initialization of order execution service

Rest API
- Go rest API
- gRPC contract for database operations

Threadpool
- Consume kafka topic orderstatus and persist (threadpool)
- Persist historical and cold data on questDB
- store hot data on redis
- Make gRPC contract for threadpool server

QuestDB
- Make DB




New workflow:
Gateway (socket/websocket) -- checks  (Aeron) -- Security services (confirmed state/tentative state)---|
     |                                                                                                 |
    TCP                                                                                              kafka
     |                                                                                                 |
  Matching engine (epoll server -> queue -> matching logic -> threadpool -> kafka "order updates") ----|
     |                                                                                                 |------- Pub/Sub (Price/orders status updates)
   kafka
     |
  Persistence service (persist orders + price history + depth book history)
     |
    gRPC
     |
  QuestDB wrapper ----gRPC---- gRPC gateway (for clients to get candles and depth book)
