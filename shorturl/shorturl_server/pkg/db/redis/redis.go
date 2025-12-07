package redis

import (
	"context"
	"fmt"
	"shorturl-server/pkg/config"
	"sync"

	"github.com/redis/go-redis/v9"
)

type RedisPool interface {
	Get() *redis.Client
	Put(client *redis.Client)
}

var pool RedisPool

type redisPool struct {
	pool sync.Pool
}

func (p *redisPool) Get() *redis.Client { //to ask?
	client := p.pool.Get().(*redis.Client)              //需要类型断言，不然编译器不知道client有ping方法
	if client.Ping(context.Background()).Err() != nil { //这句话的意思是用一个空白的不会超时的上下文取pingRedis，这是一种心跳检测机制
		client = p.pool.New().(*redis.Client)
	}
	return client
}

func (p *redisPool) Put(client *redis.Client) {
	if client.Ping(context.Background()).Err() != nil {
		return
	}
	p.pool.Put(client)
}

func getPool(cnf *config.Config) RedisPool { //这里返回的是一个*redisPool但是需要的返回值是一个RedisPool是因为是*redisPool实现了接口不是redisPool实现的接口
	return &redisPool{
		pool: sync.Pool{
			New: func() any { //any本质是一个接口
				rdb := redis.NewClient(&redis.Options{
					Addr:     fmt.Sprintf("%s:%d", cnf.Redis.Host, cnf.Redis.Port),
					Password: cnf.Redis.Pwd,
				})
				return rdb
			},
		},
	}
}

func InitRedisPool(cnf *config.Config) { //config.Config中config是包名，
	pool = getPool(cnf)
}

func GetPool() RedisPool {
	return pool
}
