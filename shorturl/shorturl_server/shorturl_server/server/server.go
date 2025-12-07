package server

import (
	"context"
	"fmt"
	"shorturl-server/pkg/config"
	"shorturl-server/pkg/log"
	"shorturl-server/pkg/utils"
	"shorturl-server/pkg/xerrors"
	"shorturl-server/proto"
	cache2 "shorturl-server/shorturl_server/cache"
	"shorturl-server/shorturl_server/data"
	"strconv"
	"time"
)

type shortUrlServer struct {
	proto.UnimplementedShortUrlServer
	config            *config.Config
	log               log.ILogger
	cacheFactory      cache2.CacheFactory
	urlMapDataFactory data.IUrlMapDataFactory
}

func NewService(config *config.Config, logger log.ILogger, cachefactory cache2.CacheFactory, urlMapDataFactory data.IUrlMapDataFactory) proto.ShortUrlServer {
	return &shortUrlServer{
		config:            config,
		log:               logger,
		cacheFactory:      cachefactory,
		urlMapDataFactory: urlMapDataFactory,
	}
}

func (s *shortUrlServer) GetShortUrl(ctx context.Context, in *proto.Url) (*proto.Url, error) {
	if in.Url == "" {
		s.log.Error("参数检测失败：", in.Url)
		return nil, xerrors.New("参数检查失败")
	}
	if !utils.IsUrl(in.Url) {
		s.log.Error("参数检查失败：")
		return nil, xerrors.New("参数检查失败")
	}
	keyPrefix := ""
	domain := s.config.ShortDomain
	if !in.IsPublic {
		keyPrefix = "user_"
		domain = s.config.UserShortDomain
	}
	//根据长链查询数据库是否已经存在记录
	data := s.urlMapDataFactory.NewUrlMapData(in.IsPublic)
	entity, err := data.GetByOriginal(in.Url)
	if err != nil {
		s.log.Error(err)
		return nil, err
	}
	entity.OriginalUrl = in.Url

	if entity.ShortKey == "" {
		//新增记录
		id, err := data.GenerateId(time.Now().Unix(), time.Now().Unix())
		if err != nil {
			s.log.Error(err)
			return nil, err
		}
		entity.ShortKey = utils.ToBase62(id)
		entity.OriginalUrl = in.Url
		entity.ID = id
		entity.UpdateAt = time.Now().Unix()
		err = data.Update(entity)
		if err != nil {
			s.log.Error(err)
			return nil, err
		}
	}
	cache := s.cacheFactory.NewKvCache()
	defer cache.Destroy()
	key := keyPrefix + entity.ShortKey
	err = cache.Set(key, entity.OriginalUrl, cache2.DefaultTTL)
	if err != nil {
		s.log.Error(err)
		return nil, err
	}
	return &proto.Url{
		Url:      domain + entity.ShortKey,
		IsPublic: in.IsPublic,
	}, nil
}
func (s *shortUrlServer) GetOriginalUrl(ctx context.Context, in *proto.ShortKey) (*proto.Url, error) {
	if in.Key == "" {
		s.log.Error("参数检查失败")
		return nil, xerrors.New("参数检查失败")
	}
	id := utils.ToBase10(in.Key)
	if id == 0 {
		s.log.Error("参数检查失败")
		return nil, xerrors.New("参数检查失败")
	}
	keyPrefix := ""
	if !in.IsPublic {
		keyPrefix = "user_"
	}
	cache := s.cacheFactory.NewKvCache()
	defer cache.Destroy()
	key := keyPrefix + in.Key
	originalUrl, err := cache.Get(key)
	if err != nil {
		s.log.Error(err)
		return nil, err
	}
	if originalUrl == "" {
		//添加过滤器，防止恶意访问，缓存穿透
		err = s.idFilter(id, cache, in.IsPublic)
		if err != nil {
			s.log.Error(err)
			return nil, err
		}
	}

	data := s.urlMapDataFactory.NewUrlMapData(in.IsPublic)
	if originalUrl == "" {
		entity, err := data.GetByID(id)
		if err != nil {
			s.log.Error(err)
			return nil, err
		}
		originalUrl = entity.OriginalUrl
	}
	//重新设置有效时长
	err = cache.Set(key, originalUrl, cache2.DefaultTTL)
	if err != nil {
		s.log.Error(err)
		return nil, err
	}
	return &proto.Url{
		Url:      originalUrl,
		IsPublic: in.IsPublic,
	}, nil
}
func (s *shortUrlServer) idFilter(id int64, cache cache2.KVCache, isPublic bool) error { //to ask?
	key := fmt.Sprintf("%s_%s", "url_map", "maxid")
	if !isPublic {
		key = fmt.Sprintf("%s_%s", "url_map_user", "maxid")
	}
	idStr, err := cache.Get(key)
	if err != nil {
		s.log.Error(err)
		return err
	}
	var res int64
	if idStr != "" {
		res, err = strconv.ParseInt(idStr, 10, 64)
		if err != nil {
			s.log.Error(err)
			return err
		}
	}
	if res < id {
		err = xerrors.New("非法短链")
		s.log.Error(err)
		return err
	}
	return nil
}
