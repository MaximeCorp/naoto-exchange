package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"time"
    "os"

	"github.com/gorilla/websocket"
	"github.com/segmentio/kafka-go"
)

const (
	kafkaBroker = "localhost:9092"
	kafkaTopic  = "test"
	wsPort      = ":8080"
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
	})
	log.Println("Kafka Writer initialized successfully.")
}

func publishToKafka(msg string, conn *websocket.Conn) {
	kafkaMsg := kafka.Message{
		Value: []byte(msg),
		Time: time.Now(),
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	err := kafkaWriter.WriteMessages(ctx, kafkaMsg)

	if err != nil {
		log.Printf("ERROR: Failed to write message to Kafka: %v", err)
		conn.WriteMessage(websocket.TextMessage, []byte(fmt.Sprintf("ERROR: Failed to publish message: %v", err)))
	} else {
		log.Println("SUCCESS: Message published to Kafka.")
	}
}

func wsHandler(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("Failed to upgrade connection to WebSocket: %v", err)
		return
	}
	
	log.Printf("New WebSocket connection established from %s", conn.RemoteAddr())

	defer func() {
		log.Printf("Closing connection from %s", conn.RemoteAddr())
		conn.Close()
	}()

	for {
		msgType, p, err := conn.ReadMessage()
		
		if err != nil {
			if websocket.IsCloseError(err, websocket.CloseGoingAway, websocket.CloseNormalClosure) {
				log.Printf("Client disconnected (normal closure): %s", conn.RemoteAddr())
			} else {
				log.Printf("Read error from %s: %v", conn.RemoteAddr(), err)
			}
			return
		}

		if msgType == websocket.TextMessage {
			msg := string(p)
			log.Printf("Received message: %s", msg)

			publishToKafka(msg, conn)
		} else {
			log.Printf("Ignoring message of type %d from %s", msgType, conn.RemoteAddr())
		}
	}
}

func main() {
	log.SetOutput(os.Stdout)
	log.Println("Starting WebSocket to Kafka Bridge...")

	initKafkaWriter()
	defer func() {
		if err := kafkaWriter.Close(); err != nil {
			log.Fatalf("Failed to close Kafka writer: %v", err)
		}
		log.Println("Kafka writer closed cleanly.")
	}()

	http.HandleFunc("/ws", wsHandler)
	
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		fmt.Fprintf(w, "WebSocket to Kafka Bridge is ready.\nEndpoint: ws://localhost%s/ws\nKafka Topic: %s", wsPort, kafkaTopic)
	})

	log.Printf("Server listening on port %s...", wsPort)
	if err := http.ListenAndServe(wsPort, nil); err != nil {
		log.Fatalf("Server failed to start: %v", err)
	}
}
