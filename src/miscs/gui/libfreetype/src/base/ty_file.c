
#include <stdio.h>
//#include <os/os.h>
//#include <os/mem.h>

#include "freetype/config/ftstdlib.h"

#include "freetype/ty_file.h"
#include "freetype/ty_mem.h"
#include "tkl_fs.h"

//打开文件
TY_FILE *ty_fopen (const char *filename,const char *mode)
{
    TY_FILE *lf = (TY_FILE *)tkl_fopen(filename, mode);

    return lf;
}


//关闭文件
int ty_fclose (TY_FILE *file)
{
    if (file) {
        TUYA_FILE lf = (TUYA_FILE)file;
        tkl_fclose(lf);
		file = NULL;
        return 0;
    }
    return -1;
}


//读取文件
//缓冲指针，缓冲元素大小，元素个数
unsigned int ty_fread (void *ptr,unsigned int size, unsigned int nmemb, TY_FILE *file)
{
	unsigned int rb = 0;
    TUYA_FILE lf = (TUYA_FILE)file;
	if(!file) {
		return -1;
	}
    rb = tkl_fread((uint8_t *)ptr, nmemb * size, lf);;
    return rb;
}


//文件偏移
int ty_fseek (TY_FILE *file,long int offset,int whence)
{
    TUYA_FILE lf = (TUYA_FILE)file;
	if (file) {
        return tkl_fseek(lf, offset, whence);
	}

    return -1;
}


//返回相对与文件开头的偏移
long int ty_ftell (TY_FILE *file)
{
    TUYA_FILE lf = (TUYA_FILE)file;
    if (file) {
		return tkl_ftell(lf);
    }
    return -1;
}

