#!/bin/bash

print_statistics()
{
	grep -E " [tTdDrRWV] " $1.$2 > $1.$2.rom
	awk 'BEGIN{size=0;}{size=size+$2;}END{printf "%-8d", size;}' $1.$2.rom
	rm -rf $1.$2.rom

	grep -E " [rR] " $1.$2 > $1.$2.readonly
	awk 'BEGIN{size=0;}{size=size+$2;}END{printf "%-8d", size;}' $1.$2.readonly
	rm -rf $1.$2.readonly


	grep -E " [bBdD] " $1.$2 > $1.$2.ram
	awk 'BEGIN{size=0;}{size=size+$2;}END{printf "%-8d", size;}' $1.$2.ram
	rm -rf $1.$2.ram


	grep -E " [dD] " $1.$2 > $1.$2.data
	awk 'BEGIN{size=0;}{size=size+$2;}END{printf "%-8d", size;}' $1.$2.data
	rm -rf $1.$2.data

	grep -E " [bB] " $1.$2 > $1.$2.bss
	awk 'BEGIN{size=0;}{size=size+$2;}END{printf "%-8d", size;}' $1.$2.bss
	rm -rf $1.$2.bss

	grep -E " [W] " $1.$2 > $1.$2.weak
	awk 'BEGIN{size=0;}{size=size+$2;}END{printf "%-8d", size;}' $1.$2.weak
	rm -rf $1.$2.weak

	echo $2
}

print_statistics_ext()
{
	grep -E " [tTdDrRWV] " $1.$2 > $1.$2.$3.rom
	awk 'BEGIN{size=0;}{if(NF<5)size=size+$2;}END{printf "%-8d", size;}' $1.$2.$3.rom
	rm -rf $1.$2.$3.rom

	grep -E " [rR] " $1.$2 > $1.$2.$3.readonly
	awk 'BEGIN{size=0;}{if(NF<5)size=size+$2;}END{printf "%-8d", size;}' $1.$2.$3.readonly
	rm -rf $1.$2.$3.readonly


	grep -E " [bBdD] " $1.$2 > $1.$2.$3.ram
	awk 'BEGIN{size=0;}{if(NF<5)size=size+$2;}END{printf "%-8d", size;}' $1.$2.$3.ram
	rm -rf $1.$2.$3.ram


	grep -E " [dD] " $1.$2 > $1.$2.$3.data
	awk 'BEGIN{size=0;}{if(NF<5)size=size+$2;}END{printf "%-8d", size;}' $1.$2.$3.data
	rm -rf $1.$2.$3.data

	grep -E " [bB] " $1.$2 > $1.$2.$3.bss
	awk 'BEGIN{size=0;}{if(NF<5)size=size+$2;}END{printf "%-8d", size;}' $1.$2.$3.bss
	rm -rf $1.$2.$3.bss

	grep -E " [W] " $1.$2 > $1.$2.$3.weak
	awk 'BEGIN{size=0;}{if(NF<5)size=size+$2;}END{printf "%-8d", size;}' $1.$2.$3.weak
	rm -rf $1.$2.$3.weak

	echo $3
}

print_statistics_list()
{
	echo "rom-----ro------ram-----data----bss-----weak----components"
	for topic in "$@"
	do
	grep -E "\/${topic}\/" symbols.all > symbols.${topic}
	print_statistics symbols ${topic}
	done
}

ELF_FILE_NAME=$1

if [ -z $ELF_FILE_NAME ]; then
	echo "no elf name"
	exit 1
fi

rm -rf symbols.*

nm -l -S -t d --size-sort $ELF_FILE_NAME > symbols.all

topics+=(`grep -o '/tuyaos-ai/components/\(.*\)' symbols.all | cut -d/ -f4  | sort | uniq` )
topics+=(`grep -o '/tuyaos_demo_wukong_ai/src/miscs/\(.*\)' symbols.all | cut -d/ -f5  | sort | uniq` )
topics+=(`grep -o '/tuyaos_demo_wukong_ai/src/drivers/\(.*\)' symbols.all | cut -d/ -f5  | sort | uniq` )
topics+=("tuyaos_demo_wukong_ai" "wukong"  "vendor")

print_statistics_list ${topics[*]}

print_statistics_ext symbols all no-path
print_statistics symbols all

rm -rf symbols.*
#echo "d: 静态-已初始化变量,如 static int i = 1;"
#echo "D: 全局-已初始化变量,如 int j = 1"
#echo "b: 静态-未初始化变量,如 static int i = 0;或者 static int i;"
#echo "B: 全局-未初始化变量,如 int i = 0;或者 int i;"
#echo "r: 静态-常量数据（BK平台如此，CS是在t）,如 static const mbedtls_cipher_info_t aes_128_ecb_info = {MBEDTLS_CIPHER_AES_128_ECB,MBEDTLS_MODE_ECB}"
#echo "R: 全局-常量数据（BK平台如此，CS是在t）,如 const mbedtls_md_info_t mbedtls_md5_info {MBEDTLS_MD_MD5, 'MD5'}"
#echo "t: 静态-代码段,如 static void fun(void) {}"
#echo "T: 全局-代码段,如 void fun(void) {}"
