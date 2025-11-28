FROM golang:1.24-bookworm AS builder

WORKDIR /app

COPY go.mod go.sum ./

RUN go mod download

COPY . .

RUN go mod tidy

ENV GOCACHE=/tmp/.cache/go-build

RUN CGO_ENABLED=0 go build -o /app/rest_server .

FROM alpine:latest

RUN apk --no-cache add ca-certificates

WORKDIR /app

COPY --from=builder /app/rest_server .

EXPOSE 8080

ENTRYPOINT ["/app/rest_server"]
