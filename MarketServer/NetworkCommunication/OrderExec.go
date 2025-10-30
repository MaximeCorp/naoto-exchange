package main

type OrderExec struct {
    Type    int32
    Side    int32
    Price   float32
    ClientId int32
    Amount  float32
    Asset   int32

    Timestamp int64 
}

func serializeOrder(o *Order) ([]byte, error) {
    var buf bytes.Buffer

    err := binary.Write(&buf, binary.LittleEndian, o) 
    if err != nil {
        return nil, err
    }
    return buf.Bytes(), nil
}
