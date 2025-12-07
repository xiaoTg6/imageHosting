package utils

import "strings"

// 字母的顺序可以适当调整，增加安全性。这里是62字符
const chars = "klmOPQRnopqrs012wxyz34FGHIJK5tuABCDEv67TUVW89abhijLMNScdefgXYZ"

// 把自增id转换为62进制字符串
func ToBase62(num int64) string {
	result := ""
	for num > 0 {
		result = string(chars[num%62]) + result
		num /= 62
	}
	return result
}

// 把短链转换为自增id
func ToBase10(str string) int64 {
	var res int64 = 0
	for _, s := range str {
		index := strings.IndexRune(chars, s) //(string,int32)这个函数在chars中寻找字符s出现的下标
		res = res*62 + int64(index)
	}
	return res
}
