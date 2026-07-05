# 1 Terminology
## 1.1 Services
- Matching Engine: The service that will match orders together and send order status to the rest of the system.
- Socket Gateway: A gateway is the middle man between a service and the client, for security and to help filter messages.
- User Details Provider: The middle man between DB and services that need user details.
- Packets Recovery: A dedicated service that will be in the optimal conditions to listen to the udp multicast of the matching engine (only role is listening and replying to recovery requests, less likely to drop packets).

## 1.2 Threads
- Socket Gateways Receivers: The threads in the socket gateway that will listen to updates from market engine and user details provider.
- Socket Gateways Writer: The thread that receives from listeners with SPSC lock-free queues (see bellow), it is the only producer of the client states array.
- Socket Gateway Listeners: Threads dedicated to receiving and processing updates. They push the updates to the central writer (from above). If listening to UDP, they're responsible for keeping track of sequence IDs and requesting missed packets to the message recovery service (see bellow, not written yet).

## 1.3 Data Structures
- Client States Array (Socket Gateway): It is a fixed size SoA (see bellow), if a client connection has a socket with fd = 5, then we can access user details at index 5. This allows to make the hot path much faster. It is worth noting that individual double-buffering (see bellow) is used to reduce slow downs due to concurrent writting/reading, it is individual to reduce impact of a swap on cache misses.
- Client ID to fd Map (Socket Gateway): A map used by receiver threads and modified by writer thread. The implementation of map can be changed but the principle remains the same.
- Storage Pool: A pool of preallocated instances of a class, the acquire method returns a pointer to an instance of the class and the release method pushes the pointer to the free queue. The implementation of the free queue depends on the number of consumers and producers. The pool has a fixed size and allows to skip delay from malloc syscalls, it also helps with cache locality.

## 1.4 Concepts
- SPSC/MPMC: Single producer, single consumer/Multiple producers, multiple consumers. The best is to keep things SPSC if possible.
- Sequence ID: The ID used in UDP connections when data loss is a problem.
- SoA: Structure of array and not array of structures, allows to access a particular field sequentially without having to request the other fieds in memory.
- Double-buffering: When there is a writer and a reader for the same buffer, we can duplicate the buffer and reader will read on one buffer while the writer will write on the other one. The buffers are swapped whenever the writer considers that its buffer can be read, swapping simply means that reader will start reading the other buffer, we do not copy/move any data.
- Cache misses: When a data is needed by the program, but it is not in the cache, meaning the data will be fetched from RAM, causing unwanted delay.
- Cache locality: A good cache locality is when the blocks of data needed by the program are close in memory, meaning a single cache line might be enough for multiple blocks. It also means that cache can store more blocks of data for the same amount of cache lines. Lastly, if memory accesses are sequential, the program can prefetch block from RAM in advance.
- DPDK: Data plane development kit is a high performance C library that includes many optimized data structures and allows program to the NIC (see bellow) directly. The library was built to allow multi-thread and multi process apps, the main purpose of DPDK is to do busy polling on the NIC.
- Busy Polling: Busy polling is the opposite of event-driven, might waste lot of CPU time but allows lowest possible latency.
- NIC: Network interface card is a component of the computer that handles network communication, it usually talks with OS, but can talk directly to user programs by using kernel bypass (see bellow).
- Kernel Bypass: Kernel bypass is used to avoid the unwanted latency of the kernel. This is generally to avoid the double copy, to avoid syscall, and to bypass some unnecessary features from the kernel implementation of UDP/TCP. The main ways of doing that are to use DPDK or SolarFlare NIC (a NIC that allows you to natively do kernel bypass while keeping the socket programming). If you use TCP, Solarflake is recommended and if you use UDP, DPDK will be faster, but much more complex to write.

# 2 Socket Gateway
The socket gateways expect clients messages to have little endian memory order.
They are designed to be scaled out, they contain 5 threads.
A dynamic list of IP addresses will likely be accessible from Rest API in order to avoid overhead of a load balancer.
## 2.1 Components (threads)
### 2.1.1 Epoll Server
This thread will be dedicated to receiving clients packets with epoll on tcp connections, the optimal performances can be achieved with solarflare NIC card (kernel bypass).

Workflow:
- Acquire orders batch from the memory pool.
- Read packets into the orders batch (no waiting time).
- Push the batch's address into a SPSC lock-free queue (the pointer will be released by the consumer of the queue).
- If the message is an API key and not an order, send the API key to the fd that's connected to user details provider, one of the receivers will read on the same fd and get the response and handle the rest.
### 2.1.2 Risk Service
Risk service pops order batches from the queue where epoll server pushes orders and checks orders with details from client states array. This thread is in charge of releasing order batches and of sending orders to matching engines via TCP connections. 

Workflow:
- Pop a batch from the queue
- Check user details in the map (all orders from the same batch come from the same fd/client)
- If valid, send tcp message to matching engine and update the attempt value of the client, the attempt value is the sum of two fields in the client details map: attempt and risk service's local attempt (one for each fd), each value can be negative but the sum has to be positive. This allows the attempt value to remain SPSC, while instantly updating the attempt value for next order checks.
- If invalid, directly send a TCP message to the client fd (to do later: another thread should do the syscalls).

### 2.1.3 Central Writer
The client states array is a structure that was designed for spsc acces, but there are multiple threads receiving updates both from market and client details provider, that is why we want a single writter receiving updates from all threads that focus on reading new updates. This thread writes into the not ready buffer of the double buffering and swaps buffers per batches to avoid over-swapping.

The workflow:
- Pop up to N updates (N is a fixed size that can be determined at compile time) from each spsc queue. If N updates are processed or queues are empty, swap changes if any. Each client has its own independent double buffer, and an array of size N keeps track of the fds (clients) that need to be swapped.
- Writter doesn't operate any kind of syscall, its only role is to drain update queues and write to client details map. It is designed to be faster than listener threads, because no syscalls nor update processing are involved.

### 2.1.4 Market Updates Listener
Market updates listener plays is one of the threads that have the simple yet important role of listening and filtering updates before pushing them to the writter. This one in particular listens to the UDP multicast coming from the market engine meaning it has to keep track of sequence IDs (a circular buffer is required for that) and ask packets recovery service for missed packets. This part could be done using DPDK. This part needs to have read access on the client ID to fd map, the map is owned by a structure that encapsulates all listeners.

The workflow:
- Receive UDP packets and put them into the circular buffer according to their sequence ID.
- Process, filter and push them to the writter
- If a packet is missing, request it to the recovery service.
- In case the buffer size is not enough (we have some missed packets and we're still full), the consequences will be that the gateway will think some funds are still blocked (the source of truth of the system won't be mistaken). This is a risk the client will have to be aware of.   

# 3 Assumptions
## 3.1 Socket Gateway
- A client won't be in the client ID to fd map until he's connected (unless it's an old connection).
- When a new client is connected (user details provider replied to a request), the information at corresponding fd (client states array) will be overwritten.
- One client can only be connected to the same socket gateway instance.
- Updates from market about a client that is not in the map will be ignored.
- If an update leads the program to find that the client ID in the client states array was overwritten, the client ID from the update (no longer relevant) will be removed from the map (client ID to fd).
- Upon client connection (from writer thread), the old client ID at the fd corresponding to the new client ID will be removed from the map.
- The socket gateway can go down anytime, without making the system lose any data (meaning all its data are just replica of real world).

# 4 Important Details
## 4.1 Socket Gateway
- The result given by the user details provider might not be up to date, it is necessary to add a behavior to ensure that missed updates will be seen by gateway.
- The epoll server will read into batches, but should store remainder into buffers.
