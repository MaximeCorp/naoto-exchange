package main

import (
	"context"
	"log"
	"net"
	"strings"
	"fmt"

	
	"github.com/golang-jwt/jwt/v5"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	authv3 "github.com/envoyproxy/go-control-plane/envoy/service/auth/v3"
	corev3 "github.com/envoyproxy/go-control-plane/envoy/config/core/v3"
	"google.golang.org/protobuf/types/known/wrapperspb"
)

var jwtKey = []byte("your-highly-secure-secret-key-from-k8s-secret")

const (
	grpcPort = ":50051"
	authorizationHeader = "authorization"
	userIDHeader = "X-Authenticated-UserID"
)

type Claims struct {
    UserID string `json:"user_id"`
    Role     string `json:"role"`
    jwt.RegisteredClaims
}

type AuthZServer struct {
	authv3.UnimplementedAuthorizationServer
}

func extractJwt(jwtToken string) (*Claims, error) {
	claims := &Claims{}

	token, err := jwt.ParseWithClaims(
		jwtToken,
		claims,
		func(token *jwt.Token) (interface{}, error) {
			if _, ok := token.Method.(*jwt.SigningMethodHMAC); !ok {
				return nil, fmt.Errorf("unexpected signing method: %v", token.Method.Alg())
			}
			return jwtKey, nil
		},
	)

	if err != nil {
		return nil, fmt.Errorf("token parsing failed: %v", err)
	}

	if !token.Valid {
		return nil, fmt.Errorf("token is invalid or expired")
	}

	return claims, nil
}

func (s *AuthZServer) Check(ctx context.Context, req *authv3.CheckRequest) (*authv3.CheckResponse, error) {
	headers := req.GetAttributes().GetRequest().GetHttp().GetHeaders()
	authHeaderValue, ok := headers[authorizationHeader]
	
	log.Println("Received a new request")

	if !ok || !strings.HasPrefix(authHeaderValue, "Bearer ") {
		log.Println("Access Denied: Missing or malformed Authorization header.")
		return &authv3.CheckResponse{
			Status: status.New(codes.Unauthenticated, "Missing or invalid JWT token").Proto(),
		}, nil
	}

	jwtToken := strings.TrimPrefix(authHeaderValue, "Bearer ")


	claims, err := extractJwt(jwtToken)

	if err != nil {
		log.Println("Access Denied: Invalid token provided.")
		return &authv3.CheckResponse{
			Status: status.New(codes.Unauthenticated, "Invalid JWT token").Proto(),
		}, nil
	}

	log.Printf("Access Granted for UserID: %s as %s", claims.UserID, claims.Role)

	return &authv3.CheckResponse{
		Status: status.New(codes.OK, "").Proto(),
		HttpResponse: &authv3.CheckResponse_OkResponse{
			OkResponse: &authv3.OkHttpResponse{
                Headers: []*corev3.HeaderValueOption{
                    {
                        Header: &corev3.HeaderValue{Key: userIDHeader, Value: claims.UserID},
                        Append: &wrapperspb.BoolValue{Value: false},
                    },
                    {
                        Header: &corev3.HeaderValue{Key: "X-User-Role", Value: claims.Role},
                        Append: &wrapperspb.BoolValue{Value: false},
                    },
                },
            },
        },
    }, nil
}

func main() {
	lis, err := net.Listen("tcp", grpcPort)
	if err != nil {
		log.Fatalf("failed to listen on %s: %v", grpcPort, err)
	}

	s := grpc.NewServer()

	authv3.RegisterAuthorizationServer(s, &AuthZServer{})
	
	log.Printf("gRPC AuthZ server listening on %s", lis.Addr().String())
	if err := s.Serve(lis); err != nil {
		log.Fatalf("failed to serve: %v", err)
	}
}
