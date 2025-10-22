FROM gcc:12 AS builder

WORKDIR /app

RUN apt-get update && apt-get install -y \
    librdkafka-dev \
    pkg-config \
    libstdc++-12-dev \
    && rm -rf /var/lib/apt/lists/*

COPY Makefile .
COPY src/ ./src/
COPY include/ ./include/

RUN make

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    librdkafka++1 \
    librdkafka1 \
    libstdc++6 \
    netcat-openbsd \
    bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/bin

COPY --from=builder /app/marketExecution .

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]

CMD ["/usr/local/bin/marketExecution"]
