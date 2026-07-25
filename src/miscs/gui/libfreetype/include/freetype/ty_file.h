#ifndef _TY_FILE_H__
#define _TY_FILE_H__

typedef void TY_FILE;

//打开文件
TY_FILE *ty_fopen(const char *filename,const char *mode);

//关闭文件
int ty_fclose (TY_FILE *file);

//读取文件
//缓冲指针，缓冲元素大小，元素个数
unsigned int ty_fread (void *ptr,unsigned int size,unsigned int nmemb, TY_FILE *file);

//文件偏移
int ty_fseek (TY_FILE *file,long int offset,int whence);

//返回相对与文件开头的偏移
long int ty_ftell (TY_FILE *file);

#endif