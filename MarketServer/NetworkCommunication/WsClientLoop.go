package main

import (
	"log"
	"net/http"
	"encoding/json"
	"fmt"
	"bytes"
	
	"github.com/gorilla/websocket"
)

const (
	wsPort = ":8080"
)

func writeError(code uint16, msg string, conn *websocket.Conn) {
	errorResponse := ErrorRes{
		ErrorCode: code,
		ErrorMessage: msg,
	}

	jsonBytes, err := json.Marshal(errorResponse)

	if err != nil {
		conn.WriteMessage(websocket.TextMessage, []byte("Internal error occured"))
	}
	
	conn.WriteMessage(websocket.TextMessage, jsonBytes)
}

func checkOrder(msg string) (OrderReq, error) {
	var res OrderReq

	dataBytes := bytes.NewBuffer([]byte(msg))

	decoder := json.NewDecoder(dataBytes)

	decoder.DisallowUnknownFields()

	err := decoder.Decode(&res)

	if err != nil {
		log.Println("failed")
		return res, fmt.Errorf("failed to unmarshall msg: %w", err)
	}

	if res.Type != "LIMIT" && res.Type != "MARKET" {
		log.Println("failed")
		return res, fmt.Errorf("failed to unmarshall msg: %w", err)
	}

	if res.Side != "BUY" && res.Side != "SELL" {
		log.Println("failed")
		return res, fmt.Errorf("failed to unmarshall msg: %w", err)
	}

	if res.UserID < 0 {
		log.Println("failed")
		return res, fmt.Errorf("failed to unmarshall msg: %w", err)
	}

	if res.AssetID < 0 {
		log.Println("failed")
		return res, fmt.Errorf("failed to unmarshall msg: %w", err)
	}

	if res.Amount <= 0 {
		log.Println("failed")
		return res, fmt.Errorf("failed to unmarshall msg: %w", err)
	}

	return res, nil
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

			order, err := checkOrder(msg)

			if err != nil {
				writeError(1, "Invalid order format", conn)
				continue
			}

			log.Println(order)

			publishToKafka(order, conn)
		} else {
			log.Printf("Ignoring message of type %d from %s", msgType, conn.RemoteAddr())
		}
	}
}
