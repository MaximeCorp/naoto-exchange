package main

import (
	"log"
	"net/http"
	
	"github.com/gorilla/websocket"
)

const (
	wsPort      = ":8080"
)

func checkOrder(msg string) (string, error) {
	return "caca", nil
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
