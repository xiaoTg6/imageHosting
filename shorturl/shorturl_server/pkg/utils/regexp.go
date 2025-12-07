package utils

import "net/url"

func IsUrl(urlString string) bool {
	u, err := url.Parse(urlString)
	if err != nil {
		return false
	}
	if u.Scheme != "http" && u.Scheme != "https" {
		return false
	}
	if u.Host == "" {
		return false
	}
	return true
}
