package main

import (
	"fmt"
	"log"
	"net/http"
	"os"
)

func main() {
	log.SetOutput(os.Stdout)
	log.Println("Starting WebSocket to Kafka Bridge...")

	initKafkaWriter()
	initSnowFlake()

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
