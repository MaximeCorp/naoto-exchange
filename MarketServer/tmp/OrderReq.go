package main

type OrderReq struct {
	UserID int32 `json:"user_id"`
	AssetID int32 `json:"asset_id""`
	Side string `json:"side"`
	Type string `json:"type"`
	Amount float32 `json:"amount"`
	Price float32 `json:"price"`
}
