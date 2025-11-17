package main

type OrderRes struct {
	UserID int32 `json:"user_id"`
	OrderID string `json:"order_id"`
	Status string `json:"status"`
}
