#!/bin/bash

KAFKA_HOST=$(echo $KAFKA_BROKERS | cut -d ':' -f 1)
KAFKA_PORT=$(echo $KAFKA_BROKERS | cut -d ':' -f 2)

echo "Waiting for Kafka broker ($KAFKA_HOST:$KAFKA_PORT) to be ready..."

while ! nc -z -w 1 $KAFKA_HOST $KAFKA_PORT; do
  echo "Kafka not yet available. Sleeping..."
  sleep 1
done

echo "Kafka is ready! Starting market-server application..."

exec "$@"
