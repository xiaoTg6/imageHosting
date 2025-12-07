package config

import (
	"log"

	"github.com/fsnotify/fsnotify"
	"github.com/spf13/viper"
	_ "github.com/spf13/viper/remote"
)

type Config struct {
	Http struct {
		IP   string `mapstructure:"ip"`
		Port int    `mapstructure:"port"`
	}
	Cos struct {
		BucketUrl string `mapstructure:"bucketUrl"`
		SecretID  string `mapstructure:"secretId"`
		SecretKey string `mapstructure:"secretKey"`
		CDNDomain string `mapstructure:"cdnDomain"`
	} `mapstructure:"cos"`
	DependOn struct {
		ShortUrl struct {
			Address     string
			AccessToken string
		}
	} `mapstructure:"dependOn"`
	Log struct {
		Level   string
		LogPath string `mapstructure:"logPath"`
	}
}

var conf *Config

// 从文件初始化配置文件
func InitConfig(filepath string, typ ...string) {
	v := viper.New()
	v.SetConfigFile(filepath)
	if len(typ) > 0 {
		v.SetConfigType(typ[0]) //to ask?
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
	//配置热更新
	v.OnConfigChange(func(in fsnotify.Event) {
		v.Unmarshal(conf)
	})
	v.WatchConfig() //to ask?
}

func GetConfig() *Config {
	return conf
}
