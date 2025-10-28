package main

type OrderReq struct {
	UserID string `json:user_id`
	AssetID int `json:asset_id`
	Side string `json:side`
	Type string `json:type`
	Amount float32 `json:amount`
	Price float32 `json:price`
}
