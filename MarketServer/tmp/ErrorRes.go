package main

type ErrorRes struct {
	ErrorCode uint16 `json:"error_code"`
	ErrorMessage string `json:"error_message"`
}
