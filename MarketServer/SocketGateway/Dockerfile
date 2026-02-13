FROM gcc:12 AS builder

WORKDIR /app

RUN apt-get update && apt-get install -y \
    pkg-config \
    libstdc++-12-dev \
    && rm -rf /var/lib/apt/lists/*

COPY Makefile .
COPY src/ ./src/
COPY include/ ./include/

RUN make

FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    netcat-openbsd \
    bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/bin

COPY --from=builder /app/marketExecution .

ENTRYPOINT ["/usr/local/bin/marketExecution"]

CMD [""]
