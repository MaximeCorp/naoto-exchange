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
