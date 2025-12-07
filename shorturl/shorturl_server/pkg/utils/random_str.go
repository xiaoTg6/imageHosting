package utils

import "math/rand"

func GenerateRandomString(length int) string {
	b := make([]byte, length)
	for i := range b {
		b[i] = chars[rand.Intn(len(chars))] //chars是62长度的字符串
	}
	return string(b)
}
