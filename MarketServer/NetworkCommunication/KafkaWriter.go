package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"time"
	"encoding/json"

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

func publishToKafka(order OrderReq, conn *websocket.Conn) {
	key, err := generateUniqueKey(string(order.AssetID))

	if err != nil {
		log.Printf("ERROR: %v", err)
		writeError(1, fmt.Sprintf("Failed transmitting order: %v", err), conn)
	}

	orderResponse := OrderRes{
		UserID: order.UserID,
		OrderID: key,
		Status: "Received",
	}

	if err != nil {
		log.Printf("ERROR: %v", err)
		writeError(1, fmt.Sprintf("Failed transmitting order: %v", err), conn)
	}

	var type_val int32 = 0

	if order.Type == "MARKET" {
		type_val = 1
	}

	var side int32 = 0

	if order.Side == "SELL" {
		side = 1
	}

	now := time.Now().UTC()
	timestampNanos := now.UnixNano()

	var key_copy [MAX_KEY_SIZE]byte

	copy(key_copy[:], key)

	final_order := OrderExec{
		Key: key_copy,
		Type: type_val,
		Side: side,
		Price: order.Price,
		ClientId: order.UserID,
		Amount: order.Amount,
		Asset: order.AssetID,
		Timestamp: timestampNanos,
	}

	if err != nil {
		log.Printf("ERROR: %v", err)
		writeError(1, fmt.Sprintf("Failed transmitting order: %v", err), conn)
	}

	bytesExec , err := serializeOrder(&final_order)

	if err != nil {
		log.Printf("ERROR: %v", err)
		writeError(1, fmt.Sprintf("Failed transmitting order: %v", err), conn)
	}

	kafkaMsg := kafka.Message{
		Value: bytesExec,
		Time: time.Now(),
	}

	ctx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
	defer cancel()

	err = kafkaWriter.WriteMessages(ctx, kafkaMsg)

	if err != nil {
		log.Printf("ERROR: Failed to write message to Kafka: %v", err)
		writeError(1, fmt.Sprintf("Failed transmitting order: %v", err), conn)
	} else {
		log.Println("SUCCESS: Message published to Kafka.")
		resBytes, err := json.Marshal(orderResponse)
		if err != nil {
			writeError(1, fmt.Sprintf("Failed transmitting order: %v", err), conn)
		}

		conn.WriteMessage(websocket.TextMessage, resBytes)
	}
}
