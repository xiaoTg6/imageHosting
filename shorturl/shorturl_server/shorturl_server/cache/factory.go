package cache

type CacheFactory interface {
	NewKvCache() KVCache
}
