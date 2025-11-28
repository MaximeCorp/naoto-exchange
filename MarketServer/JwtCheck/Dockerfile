FROM golang:1.24-bookworm AS builder

WORKDIR /app

COPY . .

RUN go mod init jwt_check || true

RUN go mod edit -require=github.com/envoyproxy/go-control-plane@v0.12.0

RUN go mod tidy

ENV GOCACHE=/tmp/.cache/go-build

RUN CGO_ENABLED=0 go build -o /app/jwt_check .

FROM alpine:latest

RUN apk --no-cache add ca-certificates

WORKDIR /app

COPY --from=builder /app/jwt_check .

EXPOSE 50051

ENTRYPOINT ["/app/jwt_check"]
