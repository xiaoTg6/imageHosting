package shorturl

import (
	"shorturl-proxy/pkg/config"
	grpcclientpool "shorturl-proxy/pkg/grpc_client_pool"
	"shorturl-proxy/services"
	"sync"
)

var pool grpcclientpool.ClientPool
var once sync.Once

type shortUrlClient struct {
	services.DefaultClient
}

func GetShortUrlClientPool() grpcclientpool.ClientPool {
	once.Do(func() {
		cnf := config.GetConfig()
		client := &shortUrlClient{}
		pool = client.GetPool(cnf.DependOn.ShortUrl.Address)
	})
	return pool
}
