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


#Features:
- HFT: minimal latency order execution
- candles + depth: fast historical data queries + realtime updates
- Order status updates


```
New workflow:
Envoy
   |
   |
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
```
Everything above this line might be outdated.

# 1. Terminology
## 1.1 Services
- Matching Engine: The service that will match orders together and send order status to the rest of the system.
- Socket Gateway: A gateway is the middle man between a service and the client, for security and to help filter messages.
- User Details Provider: The middle man between DB and services that need user details.
## 1.2 Threads
- Socket Gateways Receivers: The threads in the socket gateway that will listen to updates from market engine and user details provider.
- Socket Gateways Writter: The thread that receives from receivers with SPSC lock-free queues (see bellow), it is the only producer of the client states array.
## 1.3 Data Structures
- Client States Array (Socket Gateway): It is a fixed size SoA (see bellow), if a client connection has a socket with fd = 5, then we can access user details at index 5. This allows to make the hot path much faster. It is worth noting that individual double-buffering (see bellow) is used to reduce slow downs due to concurrent writting/reading, it is individual to reduce impact of a swap on cache misses.
- Client ID to fd Map (Socket Gateway): A map used by receiver threads and modified by writter thread. The implementation of map can be changed but the principle remains the same.
- Storage Pool: A pool of preallocated instances of a class, the acquire method returns a pointer to an instance of the class and the release method pushes the pointer to the free queue. The implementation of the free queue depends on the number of consumers and producers. The pool has a fixed size and allows to skip delay from malloc syscalls, it also helps with cache locality.
## 1.4 Concepts
- SPSC/MPMC: Single producer, single consumer/Multiple producers, multiple consumers. The best is to keep things SPSC if possible.
- Sequence ID: The ID used in UDP connections when data loss is a problem.
- SoA: Structure of array and not array of structures, allows to access a particular field sequentially without having to request the other fieds in memory.
- Double-buffering: When there is a writter and a reader for the same buffer, we can duplicate the buffer and reader will read on one buffer while the writter will write on the other one. The buffers are swapped whenever the writter considers that its buffer can be read, swapping simply means that reader will start reading the other buffer, we do not copy/move any data.
- Cache misses: When a data is needed by the program, but it is not in the cache, meaning the data will be fetched from RAM, causing unwanted delay.
- Cache locality: A good cache locality is when the blocks of data needed by the program are close in memory, meaning a single cache line might be enough for multiple blocks. It also means that cache can store more blocks of data for the same amount of cache lines. Lastly, if memory accesses are sequential, the program can prefetch block from RAM in advance.

# 2. Socket Gateway
The socket gateways expect clients messages to have little endian memory order.
They are designed to be scaled out, they contain 5 threads each with one specific role.
A dynamic list of IP addresses will likely be accessible from Rest API in order to avoid overhead of a load balancer.
## 2.1 Components (threads)
### 2.1.1 Epoll Server
This thread will be dedicated to receiving clients packets with epoll on tcp connections, the optimal performances can be achieved with solarflare NIC card (kernel bypass).
The workflow:
- Acquire orders batch from the memory pool.
- Read packets into the orders batch (no waiting time).
- Push the batch's address into a SPSC lock-free queue (the pointer will be released by the consumer of the queue).
- If the message is an API key and not an order, send the API key to the fd that's connected to user details provider, one of the receivers will read on the same fd and get the response and handle the rest.

# 3. Assumptions
## 3.1 Socket Gateway
- A client won't be in the client ID to fd map until he's connected (unless it's an old connection).
- When a new client is connected (user details provider replied to a request), the information at corresponding fd (client states array) will be overwritten.
- One client can only be connected to the same socket gateway instance.
- Updates from market about a client that is not in the map will be ignored.
- If an update leads the program to find that the client ID in the client states array was overwritten, the client ID from the update (no longer relevant) will be removed from the map (client ID to fd).
- Upon client connection (from writter thread), the old client ID at the fd corresponding to the new client ID will be removed from the map.

# 4. Important Details
## 4.1 Socket Gateway
- The result given by the user details provider might not be up to date, it is necessary to add a behavior to ensure that missed updates will be seen by gateway.
- 
