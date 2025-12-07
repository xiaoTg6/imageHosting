package cache

import (
	"context"
	redis_pkg "shorturl-server/pkg/db/redis"
	"time"

	"github.com/redis/go-redis/v9"
)

type redisKVCache struct {
	redisClient *redis.Client
	destroy     func()
}

func newRedisKVCache(client *redis.Client, destroy func()) KVCache {
	return &redisKVCache{
		redisClient: client,
		destroy:     destroy,
	}
}

func (c *redisKVCache) Get(key string) (string, error) {
	key = redis_pkg.GetKey(key)
	res, err := c.redisClient.Get(context.Background(), key).Result()
	if err != nil {
		return "", nil
	}
	return res, err
}

func (c *redisKVCache) Set(key, value string, ttl int) error {
	key = redis_pkg.GetKey(key)
	return c.redisClient.SetEx(context.Background(), key, value, time.Second*time.Duration(ttl)).Err()
}

func (c *redisKVCache) Expire(key string, ttl int) error {
	key = redis_pkg.GetKey(key)
	return c.redisClient.Expire(context.Background(), key, time.Second*time.Duration(ttl)).Err()
}

func (c *redisKVCache) Destroy() {
	if c.destroy != nil {
		c.destroy()
	}
}

type redisCahceFactory struct {
	redisPool redis_pkg.RedisPool
}

func NewRedisCacheFactory(redisPool redis_pkg.RedisPool) CacheFactory {
	return &redisCahceFactory{
		redisPool: redisPool,
	}
}

func (f *redisCahceFactory) NewKvCache() KVCache {
	redisClient := f.redisPool.Get()
	destroy := func() {
		f.redisPool.Put(redisClient)
	}
	return newRedisKVCache(redisClient, destroy)
}
