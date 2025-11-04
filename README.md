# marketplace
## todo:
Order execution
- Publish to kafka orderstatus after order executed
- Make gRPC contract for initialization of order execution service

Rest API
- Go rest API
- gRPC contract for database operations

Threadpool
- Consume kafka topic orderstatus and persist (threadpool)
- Make gRPC contract for threadpool server
