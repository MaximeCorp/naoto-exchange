package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"time"

	"github.com/gorilla/websocket"
	"github.com/segmentio/kafka-go"
)

const (
	kafkaBroker = "kafka:9092"
	kafkaTopic  = "test"
)

var kafkaWriter *kafka.Writer

var upgrader = websocket.Upgrader{
	ReadBufferSize: 1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func initKafkaWriter() {
	log.Printf("Initializing Kafka Writer to Broker: %s, Topic: %s", kafkaBroker, kafkaTopic)
	kafkaWriter = kafka.NewWriter(kafka.WriterConfig{
		Brokers:  []string{kafkaBroker},
		Topic:    kafkaTopic,
		Balancer: &kafka.LeastBytes{},
		BatchSize:  10,
		BatchTimeout: time.Millisecond * 10,
	})
	log.Println("Kafka Writer initialized successfully.")
}

func publishToKafka(msg string, conn *websocket.Conn) {
	kafkaMsg := kafka.Message{
		Value: []byte(msg),
		Time: time.Now(),
	}

	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer cancel()

	err := kafkaWriter.WriteMessages(ctx, kafkaMsg)

	if err != nil {
		log.Printf("ERROR: Failed to write message to Kafka: %v", err)
		conn.WriteMessage(websocket.TextMessage, []byte(fmt.Sprintf("ERROR: Failed to publish message: %v", err)))
	} else {
		log.Println("SUCCESS: Message published to Kafka.")
	}
}
