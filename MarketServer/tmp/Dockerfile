FROM golang:1.23 AS builder

WORKDIR /app

COPY go.mod go.sum ./

RUN go mod download

COPY . .

RUN go mod tidy

RUN CGO_ENABLED=0 go build -o /app/main_server .

FROM alpine:latest

RUN apk --no-cache add ca-certificates

WORKDIR /app

COPY --from=builder /app/main_server .

EXPOSE 8080

ENTRYPOINT ["/app/main_server"]
