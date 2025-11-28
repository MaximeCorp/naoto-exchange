package main

import (
	"log"
	"net/http"
	"crypto/rand"
	"encoding/base64"
	"time"

	"github.com/gin-gonic/gin"
	"database/sql"
	"github.com/golang-jwt/jwt/v5"
	_ "github.com/lib/pq"
	"golang.org/x/crypto/bcrypt"
)

type Claims struct {
    UserID string `json:"user_id"`
    Role     string `json:"role"`
    jwt.RegisteredClaims
}

type SignUpRequest struct {
	Email string `json:"email"`
	UserName string `json:"user_name"`
	Password string `json:"password"`
}

type SignUpResponse struct {
	UserID string `json:"user_id"`
	AccessToken string `json:"access_token"`
	TokenType string `json:"token_type"`
	CreatedAt time.Time `json:"created_at"`
}

type LoginRequest struct {
	Email string `json:"email"`
	Password string `json: "password"`
}

type LoginResponse struct {
	AccessToken string `json:"access_token"`
	TokenType string `json:"token_type"`
}

type newApiKeyRequest struct {
	KeyName string `json:"key_name"`
}

type newApiKeyResponse struct {
	UserID string `json:"user_id"`
	ApiKeySecret string `json:"secret_key"`
}

type ApiKeyResponse struct {
	UserID string `json:"user_id"`
	ApiKeys []Key `json:"api_keys"`
}

type User struct {
	UserID        string `json:"user_id"`
	UserName 	  string `json:"user_name"`
	Role 		  string `json:"role"`
	Email         string `json:"email"`
	HashedPassword []byte `json:"-"`
	PasswordSalt  []byte `json:"-"`
	CreatedAt time.Time `json:"created_at"`
}

type Key struct {
	UserID        string `json:"user_id"`
	KeyName 	string `json:""key_name`
	HashedKey []byte `json:"-"`
	KeySalt  []byte `json:"-"`
	Prefix string `json:"key_prefix"`
}

var jwtKey = []byte("your-highly-secure-secret-key-from-k8s-secret")

var db *sql.DB

const expirationTime = 12 * time.Hour

func GenerateJWT(userId string, role string) (string, error) {
    expirationTime := time.Now().Add(expirationTime)
    claims := &Claims{
        UserID: userId,
        Role:     role,
        RegisteredClaims: jwt.RegisteredClaims{
            ExpiresAt: jwt.NewNumericDate(expirationTime),
            IssuedAt:  jwt.NewNumericDate(time.Now()),
            Subject:   userId,
        },
    }

    token := jwt.NewWithClaims(jwt.SigningMethodHS256, claims)
    tokenString, err := token.SignedString(jwtKey)
    return tokenString, err
}

func signUpHandler(c *gin.Context) {
	var request SignUpRequest

	if err := c.ShouldBindJSON(&request); err != nil {
		log.Printf("Invalid body %v\n", err)
        c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid body format"})
        return
    }

	HashedPassword, err := bcrypt.GenerateFromPassword([]byte(request.Password), bcrypt.DefaultCost)

	if err != nil {
		log.Printf("Error hashing password %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
		})
		return
	}

	newUser := &User{}

	err = db.QueryRow(
		"INSERT INTO users (user_name, role, email, password_hash) VALUES ($1, 'user', $2, $3) RETURNING user_id, role, created_at",
		request.UserName,
		request.Email,
		HashedPassword,
	).Scan(&newUser.UserID, &newUser.Role, &newUser.CreatedAt)

	if err != nil {
		log.Printf("Error inserting user %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
		})
		return
	}

	token, err := GenerateJWT(newUser.UserID, newUser.Role)

	if err != nil {
		log.Printf("Error creating JWT %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
			"status": "Account was created",
			"user_id": newUser.UserID,
		})
	}

	response := &SignUpResponse{
		UserID: newUser.UserID,
		AccessToken: token,
		TokenType: "Bearer",
		CreatedAt: newUser.CreatedAt,
	}

	c.JSON(http.StatusOK, response)
}


func loginHandler(c *gin.Context) {
	var request LoginRequest
    if err := c.ShouldBindJSON(&request); err != nil {
		log.Printf("Invalid body %v\n", err)
        c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid body format"})
        return
    }

	user := User{}
	row := db.QueryRow("SELECT user_id, role, password_hash, email FROM users WHERE email = $1", request.Email)

	err := row.Scan(&user.UserID, &user.Role, &user.HashedPassword, &user.Email)
	
	if err == sql.ErrNoRows {
		log.Printf("User not found\n")

		c.JSON(http.StatusUnauthorized, gin.H{
			"error": "Invalid credentials",
		})
		return
	} else if err != nil {
		log.Printf("Error during select DB request %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
		})
		return
	}

	err = bcrypt.CompareHashAndPassword(user.HashedPassword, []byte(request.Password))

	if err != nil {
		log.Printf("loginHandler: Invalid password\n")

		c.JSON(http.StatusUnauthorized, gin.H{
			"error": "Invalid credentials",
		})
		return
	}

	/*
	expirationTime := time.Now().Add(expirationTime)
	claims := &Claims{
		UserID: user.UserID,
		Role: user.Role,
		StandardClaims: jwt.StandardClaims{
			ExpiresAt: expirationTime.Unix(),
		},
	}
	*/

	tokenString, err := GenerateJWT(user.UserID, user.Role)

	if err != nil {
		log.Printf("Failed JWT generation: %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
		})
		return
	}

	c.JSON(http.StatusOK, LoginResponse{
        AccessToken: tokenString,
        TokenType:   "Bearer",
    })
}

func GenerateNewSecretKey(length int) (string, string, error) {
	b := make([]byte, length) 

	_, err := rand.Read(b) 
	if err != nil {
		return "", "", err
	}

	key := base64.URLEncoding.EncodeToString(b)
	
	hashedKeyBytes, err := bcrypt.GenerateFromPassword([]byte(key), bcrypt.DefaultCost)
    if err != nil {
        return "", "", err
    }

    hashedKey := string(hashedKeyBytes)

	return hashedKey, key, nil 
}

func insertAPIKEY(userId string, keyName string, keyHash string, keyPref string) (error) {
	_, err := db.Exec(
		`INSERT INTO api_keys (user_id, key_name, key_hash, key_prefix) VALUES ($1, $2, $3, $4)`,
		userId,
		keyName,
		keyHash,
		keyPref,
	)

	if err != nil {
        log.Printf("Error inserting user: %v\n", err)
        return err
    }

	return nil
}

func makeAPIKEY(c *gin.Context) {
    userId := c.GetHeader("X-Authenticated-UserID")
    // role := c.GetHeader("X-Authenticated-UserRole")

	var request newApiKeyRequest

    if err := c.ShouldBindJSON(&request); err != nil {
		log.Printf("Invalid body %v\n", err)
        c.JSON(http.StatusBadRequest, gin.H{"error": "Invalid body format"})
        return
    }

	hashedKey, secret_key, err := GenerateNewSecretKey(32)

	err = insertAPIKEY(userId, request.KeyName, hashedKey, secret_key[:8])

	if err != nil {
		log.Printf("Error during insert DB request %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
		})
		return
	}

	c.JSON(http.StatusOK, &newApiKeyResponse{
		userId,
		secret_key,
	})
}

func dbAPIKEY(userId string) (*ApiKeyResponse, error) {
	rows, err := db.Query("SELECT user_id, key_name, key_prefix FROM api_keys WHERE user_id = $1", userId)

	if err != nil {
        return nil, err
    }

	defer rows.Close()

    keys := &ApiKeyResponse{
		UserID: userId,
		ApiKeys: []Key{},
	}

	for rows.Next() {
		var curKey Key

		err = rows.Scan(
                &curKey.UserID, &curKey.KeyName, &curKey.Prefix)

		if err != nil {
            return nil, err
        }

		keys.ApiKeys = append(keys.ApiKeys, curKey)
	}

    return keys, nil
}

func getAPIKEY(c *gin.Context) {
    userId := c.GetHeader("X-Authenticated-UserID")

	// role := c.GetHeader("X-Authenticated-UserRole")

	log.Printf("User ID is %s\n", userId)

	keys, err := dbAPIKEY(userId)

	if err != nil {
		log.Printf("Error during select DB request %v\n", err)
		c.JSON(http.StatusInternalServerError, gin.H{
			"error": "Internal error",
		})
		return
	}

    c.JSON(http.StatusOK, keys)
}

func main() {
	/*
	connStr := os.Getenv("DB_CONN_STRING")
	if connStr == "" {
		log.Fatal("DB_CONN_STRING is missing from env")
	}
	*/

	connStr := "postgres://maxime:1234@postgres_db:5432/testdb?sslmode=disable"

	var err error
	db, err = sql.Open("postgres", connStr)
	if err != nil {
		log.Fatalf("Failed when opening DB: %v", err)
	}
	defer db.Close()

	err = db.Ping()
	if err != nil {
		log.Fatalf("Not connected to DB: %v", err)
	}

	router := gin.Default()

	router.POST("/auth/login", loginHandler)
	router.POST("/auth/sign-up", signUpHandler)

	protected := router.Group("/api")
    {
        protected.GET("/keys", getAPIKEY)
		protected.POST("/keys", makeAPIKEY)
    }

	router.Run(":8080")
}
