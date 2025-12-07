package services

import (
	"context"
	grpcclientpool "shorturl-proxy/pkg/grpc_client_pool"
	"shorturl-proxy/pkg/log"

	"google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"
	"google.golang.org/grpc/metadata"
)

type DefaultClient struct {
}

func (c *DefaultClient) GetPool(addr string) grpcclientpool.ClientPool {
	pool, err := grpcclientpool.GetPool(addr, c.GetOptions()...)
	if err != nil {
		log.Error(err)
		return nil
	}
	return pool
}

func (c *DefaultClient) GetOptions() []grpc.DialOption {
	opts := make([]grpc.DialOption, 0)
	opts = append(opts, grpc.WithTransportCredentials(insecure.NewCredentials())) //to ask?
	return opts
}

func AppendBearerTokenToContext(ctx context.Context, accessToken string) context.Context {
	md := metadata.Pairs("authorization", "Bearer "+accessToken)
	return metadata.NewOutgoingContext(ctx, md)
}
