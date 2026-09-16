#!/bin/bash
# 统计 ELF 中各组件及 sdk/apps/vendor 各维度的 rom/ro/ram/data/bss/weak 占用
# 用法: ./tuyaos_statistics_ai.sh <app.elf>

ELF_FILE_NAME="$1"

if [ -z "$ELF_FILE_NAME" ]; then
	echo "no elf name"
	exit 1
fi

if [ ! -f "$ELF_FILE_NAME" ]; then
	echo "file not found: $ELF_FILE_NAME"
	exit 1
fi

if [ "$(head -c 4 "$ELF_FILE_NAME" | od -An -tx1 | tr -d ' \n')" != "7f454c46" ]; then
	echo "not an ELF file: $ELF_FILE_NAME (map/bin 文件不支持, 请传 app.elf)"
	exit 1
fi

rm -f symbols.all
nm -l -S -t d --size-sort "$ELF_FILE_NAME" > symbols.all

topics=()
topics+=( $(grep -o '/tuyaos-ai/components/\(.*\)' symbols.all | cut -d/ -f4 | sort | uniq) )
topics+=( $(grep -o '/tuyaos_demo_wukong_ai/src/miscs/\(.*\)' symbols.all | cut -d/ -f5 | sort | uniq) )
topics+=( $(grep -o '/tuyaos_demo_wukong_ai/src/drivers/\(.*\)' symbols.all | cut -d/ -f5 | sort | uniq) )
topics+=( "tuyaos_demo_wukong_ai" "wukong" "ui" "vendor" )

# 单遍扫描 symbols.all, 同时统计所有 topic、三个维度、no-path 与 all
# symbols.all 每行: 地址 大小 类型 符号名[\t路径:行号] (有路径 5 字段, 无路径 4 字段)
awk -v topics_str="${topics[*]}" '
BEGIN {
	ntopics = split(topics_str, topics, " ")
	ncls = 6
	clsre[1] = "^[tTdDrRWV]$"	# rom
	clsre[2] = "^[rR]$"		# readonly
	clsre[3] = "^[bBdD]$"		# ram
	clsre[4] = "^[dD]$"		# data
	clsre[5] = "^[bB]$"		# bss
	clsre[6] = "^W$"		# weak
}
{
	size = $2
	for (c = 1; c <= ncls; c++)
		hit[c] = ($3 ~ clsre[c])

	add("all")
	if (NF < 5)
		add("no-path")
	for (t = 1; t <= ntopics; t++)
		if (index($0, "/" topics[t] "/"))
			add(topics[t])
	if (index($0, "/tuyaos-ai/components/"))
		add("dim:components")
	if (index($0, "/tuyaos-ai/apps/"))
		add("dim:apps")
	if (index($0, "/tuyaos-ai/vendor/"))
		add("dim:vendor")
}
function add(key,   c) {
	for (c = 1; c <= ncls; c++)
		if (hit[c])
			sum[key, c] += size
}
function row(key, label,   c, line) {
	line = ""
	for (c = 1; c <= ncls; c++)
		line = line sprintf("%-8d", sum[key, c] + 0)
	print line label
}
END {
	hdr = "rom-----ro------ram-----data----bss-----weak----"
	print hdr "components"
	for (t = 1; t <= ntopics; t++)
		row(topics[t], topics[t])

	print ""
	print hdr "dimension"
	row("dim:components", "components")
	row("dim:apps", "apps")
	row("dim:vendor", "vendor")
	print ""

	row("no-path", "no-path")
	row("all", "all")
}
' symbols.all

rm -f symbols.all
#echo "d: 静态-已初始化变量,如 static int i = 1;"
#echo "D: 全局-已初始化变量,如 int j = 1"
#echo "b: 静态-未初始化变量,如 static int i = 0;或者 static int i;"
#echo "B: 全局-未初始化变量,如 int i = 0;或者 int i;"
#echo "r: 静态-常量数据（BK平台如此，CS是在t）,如 static const mbedtls_cipher_info_t aes_128_ecb_info = {MBEDTLS_CIPHER_AES_128_ECB,MBEDTLS_MODE_ECB}"
#echo "R: 全局-常量数据（BK平台如此，CS是在t）,如 const mbedtls_md_info_t mbedtls_md5_info {MBEDTLS_MD_MD5, 'MD5'}"
#echo "t: 静态-代码段,如 static void fun(void) {}"
#echo "T: 全局-代码段,如 void fun(void) {}"
