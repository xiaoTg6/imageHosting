package interceptor

import (
	"context"
	"shorturl-server/pkg/log"
	"shorturl-server/pkg/xerrors"

	"google.golang.org/grpc"
)

func StreamErrorInterceptor(srv interface{}, ss grpc.ServerStream, info *grpc.StreamServerInfo, handler grpc.StreamHandler) error {
	err := handler(srv, ss)
	switch err.(type) {
	case *xerrors.Error:
		//对error进行了一些处理，例如：多语言和统一错误消息
		log.Error(err)
		err = xerrors.New("触发了业务流限制")
	default:
		//其它类型，暂不处理
	}
	return err
}

func UnaryErrorInterceptor(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (resp interface{}, err error) {
	resp, err = handler(ctx, req)
	switch err.(type) {
	case *xerrors.Error:
		//对error进行一些处理，例如：多语言和统一错误消息
		log.Error(err)
		err = xerrors.New("触发了业务流限制")
	default:
		//其他类型，暂不处理
	}
	return resp, err
}
