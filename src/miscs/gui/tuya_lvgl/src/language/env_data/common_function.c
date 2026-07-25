#include "tuya_app_gui_core_config.h"
#include "ty_gui_fs.h"
#include "tkl_memory.h"
#include "common_function.h"

OPERATE_RET cf_read_file_to_psram(char *file_path, char **file_content)
{
#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    int fd = -1;
#else
    TUYA_FILE fd = NULL;
#endif
    UINT32_T once_read_len = 1024;
    OPERATE_RET ret = OPRT_COM_ERROR;
    int read_len = 0;
    UINT8_T *sram_addr = NULL;
    UINT32_T* paddr = NULL;
    void *content_addr = NULL;
    int content_len = 0;

    do{
    #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        struct stat statbuf;
        ret = stat(file_path, &statbuf);
        TY_GUI_LOG_PRINT("[%s][%d] statbuf.st_size:%d, ret:%d\r\n", __FUNCTION__, __LINE__, statbuf.st_size, ret);
        if(OPRT_OK != ret || 0 == statbuf.st_size)
        {
            TY_GUI_LOG_PRINT("[%s][%d] %s is null\r\n", __FUNCTION__, __LINE__, file_path);
            ret = OPRT_COM_ERROR;
            break;
        }

        content_len = (4 - statbuf.st_size % 4) + statbuf.st_size;  //四字节对齐
    #else
        int file_len = 0;
        file_len = ty_gui_fgetsize(file_path);
        if(file_len <= 0)
        {
            TY_GUI_LOG_PRINT("[%s][%d] sta fail:%s\r\n", __FUNCTION__, __LINE__, file_path);
            ret = OPRT_COM_ERROR;
            break;
        }
        content_len = (4 - file_len % 4) + file_len;  //四字节对齐
    #endif
        paddr = (UINT32_T*)tkl_system_psram_malloc(content_len);
        if(!paddr){
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }

        content_addr = paddr;
        //os_memset_word((uint32_t *)paddr, 0, content_len);
        memset((void *)paddr, 0, content_len);
        //TY_GUI_LOG_PRINT("[%s][%d] filelen:%d\r\n", __FUNCTION__, __LINE__, content_len);
    #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
        fd = open(file_path, O_RDONLY);
        if(fd < 0)
        {
            TY_GUI_LOG_PRINT("[%s][%d] open fail:%s\r\n", __FUNCTION__, __LINE__, file_path);
            ret = OPRT_COM_ERROR;
            break;
        }
    #else
        fd = ty_gui_fopen(file_path, "r");
        if(fd == NULL)
        {
            TY_GUI_LOG_PRINT("[%s][%d]open %s fail\r\n", __func__, __LINE__, file_path);
            tkl_system_psram_free(content_addr);
            ret = OPRT_COM_ERROR;
            break;
        }
    #endif
        sram_addr = tkl_system_malloc(once_read_len);
        if (sram_addr == NULL) {
            TY_GUI_LOG_PRINT("[%s][%d] malloc fail\r\n", __FUNCTION__, __LINE__);
            ret = OPRT_COM_ERROR;
            break;
        }

        while(1)
        {
            memset(sram_addr, 0, once_read_len);
        #ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
            read_len = read(fd, sram_addr, once_read_len);
        #else
            read_len = ty_gui_fread(sram_addr, once_read_len, fd);
        #endif
            if(read_len < 0)
            {
                TY_GUI_LOG_PRINT("[%s][%d] read file fail.\r\n", __FUNCTION__, __LINE__);
                ret= OPRT_COM_ERROR;
                break;
            }

            //TY_GUI_LOG_PRINT("[%s][%d] len:%d\r\n", __FUNCTION__, __LINE__, read_len);
            if (read_len == 0)
            {
                ret = OPRT_OK;
                break;
            }
            
            if(once_read_len != read_len)
            {
                if (read_len % 4)
                {
                    read_len = (read_len / 4 + 1) * 4;
                }
                ty_psram_word_memcpy(paddr, sram_addr, read_len);
            }
            else
            {
                ty_psram_word_memcpy(paddr, sram_addr, once_read_len);
                paddr += (once_read_len/4);
            }
        }
    }while(0);

    if(OPRT_OK == ret)
    {
        *file_content = content_addr;
    }
    else
    {
        if(content_addr)  tkl_system_psram_free(content_addr);
    }
    
    if(sram_addr)
        tkl_system_free(sram_addr);

#ifdef TUYA_APP_USE_INTERNAL_FLASH_FS
    if(fd > 0)
        close(fd);
#else
    if (fd != NULL)
        ty_gui_fclose(fd);
#endif
    return ret;
}
