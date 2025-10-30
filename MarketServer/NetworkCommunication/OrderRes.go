package main

type OrderRes struct {
	UserID string `json:"user_id"`
	OrderID string `json:"order_id"`
	Status string `json:"status"`
}
