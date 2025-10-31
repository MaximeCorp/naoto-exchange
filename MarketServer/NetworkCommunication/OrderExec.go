package main

import (
    "bytes"
    "encoding/binary"
)

const MAX_KEY_SIZE = 25

type OrderExec struct {
    Key [MAX_KEY_SIZE]byte
    Type    int32
    Side    int32
    Price   float32
    ClientId int32
    Amount  float32
    Asset   int32
    Timestamp int64 
}

func serializeOrder(o *OrderExec) ([]byte, error) {
    var buf bytes.Buffer

    err := binary.Write(&buf, binary.LittleEndian, o) 
    if err != nil {
        return nil, err
    }
    return buf.Bytes(), nil
}
