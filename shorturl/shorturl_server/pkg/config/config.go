package config

import (
	"log"

	"github.com/spf13/viper"
)

type Config struct {
	Server struct {
		IP          string `mapstructure:"ip"`
		Port        int
		AccessToken string `mapstructure:"accseeToken"`
	}
	Mysql struct {
		DSN         string
		MaxLifeTime int
		MaxOpenConn int
		MaxIdleConn int
	}
	ShortDomain     string `mapstructure:"shortDomain"`
	UserShortDomain string `mapstructure:"userShortDomain"`
	Redis           struct {
		Host string
		Port int
		Pwd  string
	}
	Log struct {
		Level   string
		LogPath string `mapstructure:"logPath"`
	}
}

var conf *Config

func InitConfig(filepath string, typ ...string) {
	v := viper.New()
	v.SetConfigFile(filepath)
	if len(typ) > 0 {
		v.SetConfigType(typ[0])
	}
	err := v.ReadInConfig()
	if err != nil {
		log.Fatal(err)
	}
	conf = &Config{}
	err = v.Unmarshal(conf)
	if err != nil {
		log.Fatal(err)
	}
}

func GetConfig() *Config {
	return conf
}
