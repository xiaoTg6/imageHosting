package main

import (
	"flag"
	"fmt"
	"shorturl-proxy/pkg/config"
	"shorturl-proxy/pkg/log"
	"shorturl-proxy/proxy"

	"github.com/gin-gonic/gin"
)

var (
	configFile = flag.String("config", "dev.config.yaml", "") //to ask?
)

func main() {
	flag.Parse()
	config.InitConfig(*configFile) //to ask?*
	conf := config.GetConfig()

	logger := log.NewLogger()
	logger.SetOutput(log.GetRotateWriter(conf.Log.LogPath))
	logger.SetLevel(conf.Log.Level)
	logger.SetPrintCaller(true)

	p := proxy.NewProxy(conf, logger)

	r := gin.Default()

	r.GET("/health", func(ctx *gin.Context) {}) //to ask?

	public := r.Group("/p")
	public.GET("/:short_key", p.PublicProxy)

	user := r.Group("/u")
	user.GET("/:short_key", p.UserProxy)

	r.Run(fmt.Sprintf("%s:%d", conf.Http.IP, conf.Http.Port))

}
