#include "tuya_cloud_types.h"
#include "tkl_adc.h"
#include "tal_log.h"
#include "tuya_error_code.h"

// #include "adc_key_main.h"

// #include <os/os.h>
// #include <os/mem.h>
// #include "adc_key_main.h"
// #include "modules/pm.h"

#include "tal_uart.h"

#include "log_seq.h"
#include "media_src_zh.h"
#include "svc_ai_player.h"

#include "wukong_ai_mode.h"
#include "wukong_kws.h"
#include "tuya_ai_toy.h"
#include "wukong_tm_internal.h"

#include "tal_queue.h"
#include "tal_workq_service.h"

#include "tkl_fs.h"
// #include "bk_gpio.h"
// #include "gpio_driver.h"
#include "wukong_ai_agent.h"
#include "wukong_ai_skills.h"
#include "skill_cloudevent.h"
#include "wukong_audio_player.h"
#include "hugo_ai_face.h"

#include "tuya_uf_db.h"

// #include "eth_mac_types.h"

// // #include "wukong_ai_skills.h"
/***********************************************************
*************************micro define***********************
***********************************************************/

#define AI_FACE_FLAG_PATH   "faceflag"

/***********************************************************
***********************typedef define***********************
***********************************************************/



/***********************************************************
***********************variable define**********************
***********************************************************/
CONST UINT8_T  mEncKey[KEY_SIZE] = {'e','e','7','1','5','3','5','3','5','7','a','d','9','b','b','4'};

STATIC UINT8_T  Face_RXtime=0;
STATIC UINT8_T  Face_Rxln=0;
STATIC unsigned char Face_rxlog=0;
STATIC unsigned char Face_rxbuff[Face_Buffln];
STATIC char Face_rxdata[RXBUFFERSIZE];
STATIC unsigned char RX_Msgid;

STATIC unsigned int  FACE_TIME=8000;

STATIC unsigned int  FACE_ADD_TIME = 0;
STATIC unsigned char FACE_Work_Mode=FACE_STANDBY;
//unsigned char PIR_DATA=0;
STATIC unsigned char PIR_IRQ_FLG=0;
STATIC unsigned char PIR_DATA=0,PIR_DATA_FLG=0,PIR_TS=60;
STATIC unsigned char FACE_WORK_FLG=ENABLE;
STATIC unsigned char FACE_POWER_FLG=DISABLE;
STATIC unsigned char opendoor_time=0;
STATIC UINT8_T  FACE_ID=0;
STATIC unsigned int  PIR_READ_TIME=0;
STATIC unsigned char Face_error_ts=0;
STATIC unsigned char FACE_MODE_ERROR=1;

// unsigned int  PIR_SEN_DATA=DELTA_H;
STATIC unsigned char FACE_TX_FLG=0;

STATIC UINT8_T setting_flag = SET_INPW;
UINT8_T face_voice_flag = 0;
STATIC BYTE_T  face_flag_buf[100] = {0};
/***********************************************************
***********************function define**********************
***********************************************************/
VOID Face_Init(VOID);
VOID Face_seddata(unsigned char *data,unsigned int LN);
VOID Face_uart_task(VOID);
int encBytes(unsigned char *bytes, int length, unsigned char *out);
int DencBytes(unsigned char *bytes, int length, unsigned char *out);
unsigned char GetCRC(const unsigned char *pData, unsigned char len);


VOID face_flag_write(BYTE_T *data)
{
    INT_T  rt = OPRT_OK;
    uFILE *fp = NULL;
    INT_T  cnt = 0;

    fp = ufopen(AI_FACE_FLAG_PATH, "w+");
    if (NULL == fp) {
        TAL_PR_ERR("uf file %s can't open and read data!", AI_FACE_FLAG_PATH);
        return OPRT_NOT_EXIST;
    }

    if (0 != ufseek(fp, 0, UF_SEEK_SET)) {
        ufclose(fp);
        TAL_PR_ERR("uf file %s Set file offset to 0 error!", AI_FACE_FLAG_PATH);
        return OPRT_NOT_EXIST;
    }

    cnt = ufwrite(fp, data, 100);
    if (cnt != 100) {
        TAL_PR_ERR("uf file %s write data error!", AI_FACE_FLAG_PATH);
    }

    rt = ufclose(fp);

    face_flag_read(data);

    if (rt != OPRT_OK) {
        TAL_PR_ERR("uf file %s close error!", AI_FACE_FLAG_PATH);
        return rt;
    }
    return rt;


}
VOID face_flag_read(BYTE_T *data)
{
    INT_T    rt = OPRT_OK;
    uFILE   *fp = NULL;
    INT_T    cnt = 0;

    fp = ufopen(AI_FACE_FLAG_PATH, "r+");
    if (NULL == fp) {
        TAL_PR_ERR("uf file %s can't open and read data!", AI_FACE_FLAG_PATH);
        return OPRT_NOT_EXIST;
    }

    TAL_PR_DEBUG("uf open OK");
    cnt = ufread(fp, data, 100);
    TAL_PR_DEBUG("uf file %s read data %.2X %.2X!", AI_FACE_FLAG_PATH, data[0],data[1]);

    rt = ufclose(fp);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("uf file %s close error!", AI_FACE_FLAG_PATH);
        return rt;
    }
    return rt;

}
UINT8_T FACE_query(BYTE_T face_num)
{
    return face_flag_buf[face_num];
}
UINT8_T FACE_WR(BYTE_T face_num)
{
    face_flag_buf[face_num] = 1;
    face_flag_write(face_flag_buf);
}

INT_T face_name_write(BYTE_T face_id, BYTE_T *data)
{
    CHAR_T face_name_path[16] = "face";
    face_name_path[4] = face_id/10%10 + '0';
    face_name_path[5] = face_id%10 + '0';
    face_name_path[6] = '\0';
    // sprintf(face_name_path,"/face/%0.2X",face_id);

    // tkl_fs_mkdir("/face");
    TAL_PR_NOTICE("face_name_path = %s",face_name_path);



    INT_T  rt = OPRT_OK;
    uFILE *fp = NULL;
    INT_T  cnt = 0;

    fp = ufopen(face_name_path, "w+");
    if (NULL == fp) {
        TAL_PR_ERR("uf file %s can't open and read data!", face_name_path);
        return OPRT_NOT_EXIST;
    }

    if (0 != ufseek(fp, 0, UF_SEEK_SET)) {
        ufclose(fp);
        TAL_PR_ERR("uf file %s Set file offset to 0 error!", face_name_path);
        return OPRT_NOT_EXIST;
    }

    cnt = ufwrite(fp, data, 31);
    if (cnt != 31) {
        TAL_PR_ERR("uf file %s write data error!", face_name_path);
    }

    rt = ufclose(fp);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("uf file %s close error!", face_name_path);
        return rt;
    }
    return rt;


    // return TRUE;
}
UINT8_T face_name_read(BYTE_T face_id, BYTE_T *data)
{
    // CHAR_T *face_name_path;
    // sprintf(face_name_path,"/face/f%0.2X",face_id);
    CHAR_T face_name_path[16] = "face";
    face_name_path[4] = face_id/10%10 + '0';
    face_name_path[5] = face_id%10 + '0';
    face_name_path[6] = '\0';

    INT_T    rt = OPRT_OK;
    uFILE   *fp = NULL;
    INT_T    cnt = 0;

    fp = ufopen(face_name_path, "r+");
    if (NULL == fp) {
        TAL_PR_ERR("uf file %s can't open and read data!", face_name_path);
        return OPRT_NOT_EXIST;
    }

    TAL_PR_DEBUG("uf open OK");
    cnt = ufread(fp, data, 31);
    // TAL_PR_DEBUG("uf file %s read data %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X %.2X!!", face_name_path, data[0],data[1],data[2],data[3],data[4],data[5],data[6],data[7],data[8],data[9]);

    rt = ufclose(fp);
    if (rt != OPRT_OK) {
        TAL_PR_ERR("uf file %s close error!", face_name_path);
        return rt;
    }
    return rt;
}

UINT8_T face_name_store(BYTE_T *data)
{
    if(FACE_ADD_TIME > 0)
    {
        face_name_write(FACE_ID,data);
        FACE_WR(FACE_ID);            
        return TRUE;
    }
    return FALSE;


    // face_name_write(1,"hugo");
    // face_name_read(1,data);

    // return 1;
}
UINT8_T face_name_get(BYTE_T *data)
{
    if(face_name_read(FACE_ID,data)==0)
    {
        return TRUE;
    }
    return FALSE;
}
// /**
// * @brief a thread OPERATE_RET adc port and read the adc very 2 seconds
// *
// * @param[in] param:Task parameters
// * @return none
// */
VOID Face_Init(VOID)
{
    OPERATE_RET rt = OPRT_OK;
    //Face_En 电源控制口；
    TUYA_GPIO_BASE_CFG_T out_pin_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = TUYA_GPIO_LEVEL_LOW
    };
    TUYA_CALL_ERR_LOG(tkl_gpio_init(FACE_PW_PIN, &out_pin_cfg));
    tkl_gpio_write(FACE_PW_PIN, TUYA_GPIO_LEVEL_LOW);

    //UART2
    tkl_io_pinmux_config(TUYA_IO_PIN_40, TUYA_UART2_RX);
    tkl_io_pinmux_config(TUYA_IO_PIN_41, TUYA_UART2_TX);
    
    TAL_UART_CFG_T cfg;
    UINT_T baud = 115200;
    UINT32_T bufsz = 128;
    memset(&cfg, 0, sizeof(TAL_UART_CFG_T));
    cfg.base_cfg.baudrate = baud;
    cfg.base_cfg.databits = TUYA_UART_DATA_LEN_8BIT;
    cfg.base_cfg.parity = TUYA_UART_PARITY_TYPE_NONE;
    cfg.base_cfg.stopbits = TUYA_UART_STOP_LEN_1BIT;
    cfg.rx_buffer_size = bufsz;

    tal_uart_init(TUYA_UART_NUM_2, &cfg);


    face_flag_read(face_flag_buf);
    FACE_query(face_flag_buf);

}
VOID Face_disable(VOID)
{
    tkl_gpio_write(FACE_PW_PIN, TUYA_GPIO_LEVEL_LOW);
    FACE_POWER_FLG =DISABLE;
}
VOID Face_enable(VOID)
{
    tkl_gpio_write(FACE_PW_PIN, TUYA_GPIO_LEVEL_HIGH);
    FACE_POWER_FLG = ENABLE;
}
VOID Face_seddata(unsigned char *data,unsigned int LN)
{
    tal_uart_write(TUYA_UART_NUM_2, data, LN);
}
unsigned char GetCRC(const unsigned char *pData, unsigned char len)
{
    UINT8_T xor = 0;
    UINT8_T i = 0;
    for( i = 0 ; i<len; i++ )
    {
        xor ^= pData[i];
    }
    return (xor);
}
///**********************************加密*******************************************/
int encBytes(unsigned char *bytes, int length, unsigned char *out)
{
    int stride = KEY_SIZE;
    int index;
    uint8_t *key = (uint8_t *)mEncKey;

    if (!out)out = bytes;
    for( index = 0; index < length; index++)
    {
        out[index] = bytes[index] ^ key[index % stride];
        out[index] = ~out[index];
    }
    return 0;
}

/**********************************解密******************************************/
int DencBytes(uint8_t *bytes, int length, uint8_t *out)
{
    int stride = KEY_SIZE;
    int index;
    if (!out) out = bytes;
    for(index = 0; index < length; index++)
    {
        out[index] = ~bytes[index];
        out[index] = out[index] ^ mEncKey[index % stride];
    }
    return 0;
}

/*
    pData:要不要传数据看cmd后面有没有接数据
    dataLen:
*/
VOID Send_FaceCmd(unsigned char msgid, unsigned char *pData, unsigned int dataLen) // dataLen要包含命令字在里面
{
    uint8_t sum;
// #ifdef Debug_ENABLE
    unsigned char i = 0;
// #endif
    uint8_t SendLen = 0;
    uint8_t SendCmd[RXBUFFERSIZE] = {0};
    if(FACE_MODE_ERROR==1) FACE_MODE_ERROR=2;
    if(dataLen<=(RXBUFFERSIZE-10))
    {
        SendCmd[SendLen++] = FACE_CMD_HEAD;
        SendCmd[SendLen++] = FACE_CMD_HEAD1;
#ifdef SET_RELEASE_KEY
        if(FACE_Work_Mode>=FACE_SET_RELEASE_KEY)  SendLen=6;
#endif
        SendCmd[SendLen++] = msgid;     // command pkg
        SendCmd[SendLen++] = (uint8_t)(dataLen >> 8);           // length
        SendCmd[SendLen++] = (uint8_t)(dataLen & 0xff);   // length
        if ( dataLen > 0 && pData != NULL )
        {
            memcpy(&SendCmd[SendLen], pData, (size_t)dataLen);
            SendLen += dataLen;
        }
#ifdef SET_RELEASE_KEY
        if(FACE_Work_Mode>=FACE_SET_RELEASE_KEY)
        {   //工作模式已设置加密模式后加密码
#ifdef Debug_ENABLE
            TAL_PR_NOTICE("tx_cmd:");
            for(i=0; i<(dataLen+3); i++) {
                printf("%0.2X ",SendCmd[i+6]);
            }
            printf("\r\n");
#endif
            encBytes(SendCmd+6,dataLen+3,SendCmd+4);
            SendCmd[2]=((dataLen+3)>>8);
            SendCmd[3]=(dataLen+3);
            sum = GetCRC(&SendCmd[4],dataLen+3);
            SendCmd[dataLen+7] = (uint8_t)(sum);  // 包校验和
            // 发送指令部分数据
#ifdef Debug_ENABLE
            printf("tx_cmd_keyin_data:");
            for(i=0; i<(dataLen+8); i++) {
                printf("%0.2x",SendCmd[i]);
            }
            printf("\r\n");
#endif
            Face_seddata(SendCmd,dataLen+8);
        }
        else
#endif
        {
            sum = GetCRC(&SendCmd[2], dataLen + 3);
            SendCmd[SendLen++] = (uint8_t)(sum);    // 包校验和
            // 发送指令部分数据
            Face_seddata(SendCmd, SendLen);
#ifndef Debug_ENABLE
            TAL_PR_NOTICE("Face_tx:");
            for(i = 0; i < SendLen; i++) {
                printf("%0.2X ", SendCmd[i]);
            }
            printf("\r\n");
#endif
            
        }
    }
}

unsigned char Face_Rx_data(char *data)
{
    unsigned int i, t;
    unsigned char sum1 = 0, sum2 = 0;
    for(i = 0; i < RXBUFFERSIZE; i++)
    {
        data[i] = 0; //清空接收数组
    }
    i = 0;
    if(Face_rxlog == 1 && Face_RXtime > 100)
    {
#ifdef Debug_ENABLE
        TAL_PR_NOTICE("Face_rx:");
        for(i = 0; i < Face_Rxln; i++) {
            printf("%0.2X ", Face_rxbuff[i]);   //打印接收人脸数据
        }
        printf("\r\n");
#endif
        for(t = 0; t < Face_Rxln; t++)
        {
            if(Face_rxbuff[t] == FACE_CMD_HEAD && Face_rxbuff[t + 1] == FACE_CMD_HEAD1)
            {
                sum1 = sum2 = 0;
#ifdef SET_RELEASE_KEY
                if(FACE_Work_Mode >= FACE_SET_RELEASE_KEY)
                {
                    sum1 = Face_rxbuff[(Face_rxbuff[t + 2] + Face_rxbuff[t + 3]) + 4 + t];
                    sum2 = GetCRC((Face_rxbuff + t + 4), ((Face_rxbuff[t + 2] + Face_rxbuff[t + 3])));
                }
                else
#endif
                {
                    sum1 = Face_rxbuff[(Face_rxbuff[t + 3] + Face_rxbuff[t + 4]) + 5 + t];
                    sum2 = GetCRC((Face_rxbuff + t + 2), ((Face_rxbuff[t + 3] + Face_rxbuff[t + 4]) + 3));
                }
#ifdef Debug_ENABLE
                printf("sum1=%0.2x %0.2x \r\n", sum1,sum2);
#endif
                if(sum1 == sum2)
                {
#ifdef SET_RELEASE_KEY
                    if(FACE_Work_Mode >= FACE_SET_RELEASE_KEY)
                    {
                        //工作模式已设置加密模式后需要进行解密码
                        i = Face_rxbuff[t + 2];
                        i >>= 8;
                        i += Face_rxbuff[t + 3];
                        len = i;
                        DencBytes(Face_rxbuff + t + 4, i, Face_rxbuff + t + 2); //解密
#ifdef Debug_ENABLE
                        printf("keyout_data:");
                        for(i = 0; i < len; i++) {
                            printf("%0.2x", Face_rxbuff[t + 2 + i]);
                        }
                        printf("\r\n");
#endif
                    }
#endif
                    RX_Msgid = Face_rxbuff[t + 2]; //用于判断消息ID
                    memcpy(data, Face_rxbuff + t + 5, (Face_rxbuff[t + 3] + Face_rxbuff[t + 4]));

                    TAL_PR_NOTICE("Face_rx:");
                    for(i = 0; i < Face_Rxln; i++) {
                        printf("%0.2X ", Face_rxbuff[i]);   //打印接收人脸数据
                    }
                    TAL_PR_NOTICE("\r\n");
//                    for(i = 0; i < Face_Rxln; i++) Face_rxbuff[i] = 0; //清空数组
//                    Face_rxlog = 0;
//                    Face_RXtime = 0;
//                    Face_Rxln = 0;
                    Face_rxbuff[t] =0XFF;
                    Face_rxbuff[t+1] =0XFF;
                    Face_rxbuff[t+2]=0XFF;
                    return 1;
                }
            }
        }
        for(i = 0; i < Face_Rxln; i++) Face_rxbuff[i] = 0; //清空数组
        Face_rxlog = 0;
        Face_RXtime = 0;
        Face_Rxln = 0;
        return 0;
    }
    return 0;
}

unsigned char WR_ENROLL_ITG(unsigned char *pdata, unsigned char DIR)
{
    unsigned char i;
    pdata[0] = 0; //admin：设置改录入的人为管理员；0非管理员，1管理员
    for(i = 0; i < 32; i++) pdata[i + 1] = 0; //用户名称暂定全部写0
    pdata[i + 1] = DIR; //录入人脸方向
    pdata[i + 2] = 1; //enroll_type：采用的录入方式，0表示交互式上中下左右5个方向的录入方式、1表示单帧录入方式；
    pdata[i + 3] = 0; //enable_duplicate：0表示同一个人不能重复录入，1表示同一个人脸能够多次录入；username都可以重复
    pdata[i + 4] = 12; //录入时间为12秒

    pdata[i + 5] = 0;
    pdata[i + 6] = 0;
    pdata[i + 7] = 0;
    return i + 8;
}


VOID  USER_EXISTED(VOID)
{
//    unsigned char i;
    if(RX_Msgid==MR_SUCCESS&&(Face_rxdata[0]==MID_ENROLL_ITG||Face_rxdata[0]==PALM_ENROLL_SINGLE)&&Face_rxdata[1]==MR_FAILED4_FACEENROLLED)
    {
        // SPK_Wdata(CN_USER_EXISTED); //用户已注册
        // Bread_STANDBY();
        // FACE_TIME=3000;
        // if(APP_ADD_FACE_FLG==1||APP_ADD_PALM_FLG==1)
        // {
        //     APP_ADD_FACE_FLG=0;
        //     BLE_time=0;
        //     KEY_TIME=500;
        //     LED_Wdata(0,0);
        //     if(APP_ADD_FACE_FLG==1) Report_APP(0X01,0x04,0xfd,0xfc,0x02);
        //     if(APP_ADD_PALM_FLG==1) Report_APP(0X01,0x05,0xfd,0xfc,0x02);

        // }
        // if(setting_flag==ADD_USER_FACE) setting_flag=FACE_MANAGER;
        // if(setting_flag==ADD_USER_PALM) setting_flag=PALM_MANAGER;
    }
    if(RX_Msgid==MR_SUCCESS&&(Face_rxdata[0]==MID_ENROLL_ITG||Face_rxdata[0]==PALM_ENROLL_SINGLE)&&Face_rxdata[1]==MR_FAILED4_MAXUSER)
    {
        // SPK_Wdata(CN_USER_FULL); //用户已满
        // Bread_STANDBY();
        // FACE_TIME=3000;
        // if(APP_ADD_FACE_FLG==1||APP_ADD_PALM_FLG==1)
        // {
        //     APP_ADD_FACE_FLG=0;
        //     BLE_time=0;
        //     KEY_TIME=500;
        //     LED_Wdata(0,0);
        //     if(APP_ADD_FACE_FLG==1)  Report_APP(0X01,0x04,0xfd,0x00,0x03);
        //     if(APP_ADD_PALM_FLG==1) Report_APP(0X01,0x05,0xfd,0x00,0x03);
        // }
        // if(setting_flag==ADD_USER_FACE) setting_flag=FACE_MANAGER;
        // if(setting_flag==ADD_USER_PALM) setting_flag=PALM_MANAGER;
    }
//    for(i=0; i<RXBUFFERSIZE; i++)  Face_rxdata[i]=0; //清空数组
}

//人脸待机模式
VOID Bread_STANDBY(VOID)
{
    FACE_Work_Mode=FACE_STANDBY;//待机模式
    FACE_WORK_FLG=ENABLE;
    FACE_TIME=20000;
    PIR_DATA=0;
    if(FACE_POWER_FLG == ENABLE)
    {
        Face_disable();
    }
}
VOID Check_FACE_TimeOut(VOID)
{
    if(FACE_TIME==0)  //超时
    {
        Bread_STANDBY();
#ifdef Debug_ENABLE
        printf("FACE_T_Out\r\n");
#endif
        // if(setting_flag==DELALL_USER_FACE_ACK||setting_flag==DEL_USER_FACE||setting_flag==ADD_USER_FACE||APP_ADD_FACE_FLG==1||APP_ADD_PALM_FLG==1)
        // {
        //     SPK_Wdata(CN_OPERATION_NG);
        //     if(System_int==4)  System_int=2;
        //     else  setting_flag=FACE_MANAGER;
        // }
        // else if(setting_flag==DELALL_USER_PALM_ACK||setting_flag==DEL_USER_PALM||setting_flag==ADD_USER_PALM||APP_ADD_PALM_FLG==1||APP_ADD_PALM_FLG==1)
        // {
        //     SPK_Wdata(CN_OPERATION_NG);
        //     if(System_int==4)  System_int=2;
        //     else  setting_flag=PALM_MANAGER;
        // }
        // PIR_DATA=0;
        // if(READ_PIR()==1)  FACE_TIME=500;//1000 Modified by Hugo 25.11.18
        // else FACE_TIME=3000;
        // if(APP_ADD_FACE_FLG==1||APP_ADD_PALM_FLG==1)
        // {
        //     BLE_time=0;
        //     KEY_TIME=500;
        //     LED_Wdata(0,0);
        //     if(APP_ADD_FACE_FLG==1) Report_APP(0X01,0x04,0xfd,0xfc,0x02);
        //     if(APP_ADD_PALM_FLG==1) Report_APP(0X01,0x05,0xfd,0xfc,0x02);
        //     APP_ADD_FACE_FLG=0;
        //     APP_ADD_PALM_FLG=0;
        // }
    }
}
VOID Dispay_fled(char t)
{
    char i=0;
    // LED_Wdata(0,0);
    // for(i = 0; i< 20; i++)
    // {
    //     LED_Wdata(LED_Value1[11]+LED_Value1[t], 600);
    //     LED_Wdata(LED_Value1[11], 600);
    // }
    // BLE_time = 30000;
}
unsigned char  Face_Start(VOID)
{
    if(FACE_TIME==0) return 1;   
    else return 0;
}
VOID Face_uart_rx(VOID)
{
    OPERATE_RET cnt = 0;
    UINT8_T data[Face_Buffln];
    cnt = tal_uart_read(TUYA_UART_NUM_2,data,Face_Buffln);
    if (cnt > 0)
    {
        if (Face_Rxln+cnt < Face_Buffln)
        {
            // uart_rxbuff[uart_Rxln++] = data;
            memcpy(Face_rxbuff+Face_Rxln,data,cnt);
            Face_Rxln+=cnt;
        }
        Face_rxlog=1;
        Face_RXtime=0;
    }
}
VOID Face_uart_task(VOID)
{
#ifdef FACE_ENABLE
    unsigned char pdata[RXBUFFERSIZE];
    unsigned int i;
    switch(FACE_Work_Mode)
    {
    case FACE_STANDBY: //待机模式||(READ_PIR() == 1&&Face_error_ts<2)
        PIR_DATA=0;
        if((Face_Start()==1&&FACE_WORK_FLG == ENABLE)
                ||setting_flag==ADD_USER_FACE||setting_flag==DEL_USER_FACE|| setting_flag == DELALL_USER_FACE_ACK||FACE_MODE_ERROR==1)

        {
            // if(ERROR_TS < 5)
            {
#ifdef Debug_ENABLE
                printf("FACE_OPEN\r\n");
#endif
                Face_Rxln = 0;
                Face_enable();//打开人脸模块电源
                FACE_TIME = 1000;//1S
                FACE_Work_Mode = FACE_SET_RELEASE_KEY;
            }
        }
        break;
    case FACE_SET_RELEASE_KEY:
        if(Face_Rx_data(Face_rxdata)||FACE_TIME==0)
        {
            if(RX_Msgid == MID_NOTE||FACE_TIME==0) //模组READY
            {
                if(setting_flag == SET_INPW ||setting_flag == DEL_USER_FACE)
                {   //识别人脸
#ifdef Debug_ENABLE
                    printf("MID_VERIFY \r\n");
#endif
                    tal_system_sleep(150);
                    pdata[0] = 1; //自动关机
                    //pdata[0] = 0; //不马上关机
                    pdata[1] = 5; //5秒
#ifdef Palm_EN
                    Send_FaceCmd(PALM_VERIFY, pdata, 2); // 鉴权解锁 自动辨别人脸和掌静脉
#else
                    Send_FaceCmd(MID_VERIFY, pdata, 2); // 鉴权解锁
#endif
                    FACE_Work_Mode = FACE_VERIFY_ACK;
                    FACE_TIME = 5500;
                }
                if(setting_flag==ADD_USER_FACE)
                {   //注册人脸
                    tal_system_sleep(300);
                    i=WR_ENROLL_ITG(pdata,FACE_MIDDLE);
                    Send_FaceCmd(MID_ENROLL_ITG,pdata,i); //集成支持并扩展所有录入方式
                    FACE_Work_Mode=FACE_ADD_USER_ACK1;
                    FACE_TIME=12000;
                    // SET_TIME=set_maxtime*3;
                }
                if(setting_flag==DELALL_USER_FACE_ACK||setting_flag==System_Rest_Wait)
                {   //删除全部注册人脸
                    tal_system_sleep(500);
                    Send_FaceCmd(MID_DELALL,NULL,0);
                    tal_system_sleep(200);
                    Send_FaceCmd(MID_DELALL,NULL,0);
                    FACE_Work_Mode=FACE_DEL_ALL_ACK;
                    FACE_TIME=10000;
                }
            }
        }
        break;
    case FACE_VERIFY_ACK://人脸识别回复
        if(Face_Rx_data(Face_rxdata)) //从第五个字节开始取地址
        {
            // if(FACE_MODE_ERROR==2)
            // {
            //     FACE_MODE_ERROR=0;
            //     Bread_STANDBY();
            //     for(i = 0; i < Face_Rxln; i++) Face_rxbuff[i] = 0; //清空数组
            //     Face_rxlog = 0;
            //     Face_RXtime = 0;
            //     Face_Rxln = 0;
            //     break;
            // }
            if(RX_Msgid == MR_SUCCESS && Face_rxdata[0] == MID_VERIFY && Face_rxdata[1] == MR_SUCCESS)
            {
                FACE_ID = Face_rxdata[2]; //FACE_ID=(Face_rxdata[2]<<8)|Face_rxdata[3];
                FACE_ID <<= 8;
                FACE_ID += Face_rxdata[3];
#ifdef Debug_ENABLE
                printf("FACE_ID=%0.2X\r\n", FACE_ID);
#endif
                i = FACE_query(FACE_ID); //人脸查询  OK返回1 NG返回0


                TAL_PR_NOTICE("====get Id = %d, query = %d",FACE_ID,i);
                // if(setting_flag == DEL_USER_FACE)
                // {
                //     //删除单个人脸
                //     // tal_system_sleep(50);
                //     pdata[0] = (FACE_ID >> 8);
                //     pdata[1] = FACE_ID;
                //     Send_FaceCmd(MID_DELUSER, pdata, 2); // 删除一个注册用户
                //     FACE_Work_Mode = FACE_DEL_ONE_ACK;
                //     FACE_TIME = 10000;
                // }
                // else
                // {                    
                //     Bread_STANDBY();
                // }
                if(i == 1)
                {
                    //                    
                    face_voice_flag = 3;

                }
                else
                {
                    face_voice_flag = 1;
                    FACE_ADD_TIME = 80000;
                    FACE_TIME = 15000;
                    // setting_flag = ADD_USER_FACE;
                }
            }
            else if(RX_Msgid == MR_SUCCESS && Face_rxdata[0] == MID_VERIFY && ((Face_rxdata[1] == MR_FAILED4_UNKNOWNUSER) /*|| (Face_rxdata[1] == MR_FAILED4_TIMEOUT)*/)) // 没有已录入的用户
            {
                // // 没有已录入的用户
                // if(setting_flag == DEL_USER_FACE)
                // {
                //     // SPK_Wdata(CN_USER_NOT_EXISTED); //用户不存在；
                //     // face_voice_flag = 1;
                //     if(setting_flag == DEL_USER_FACE)
                //     {
                //         setting_flag = FACE_MANAGER;
                //         Bread_STANDBY();//人脸待机模式
                //     }
                // }
                // else
                {

                    // WR_Lock_Open(FACE_OPEN_DOOR, User_not_exists);
                    // face_voice_flag = 1;

                    Bread_STANDBY();//人脸待机模式
                    Face_error_ts++;
                    // PIR_TS=20;
                    setting_flag = ADD_USER_FACE;
                    // if(Face_error_ts>=5)
                    // {
                    //     Face_error_ts=0;
                    //     FACE_TIME=30000;
                    // }
                    // else
                    // {
                    //     if(Face_error_ts<2)  FACE_TIME=5000;
                    //     else  FACE_TIME=10000;
                    // }
                    if(Face_error_ts>10)
                    {
                        // Face_error_ts=0;
                        FACE_TIME=10000;
                    }
                    else
                    {
                        if(Face_error_ts<=5)  FACE_TIME=3000;
                        else  FACE_TIME=5000;
                    }
                }
            }
            //Deleted by Hugo 25.11.18
           else
           {
//#ifdef Palm_EN
//                pdata[0] = 1; //自动关机
//                pdata[1] = 2; //2秒
//                Send_FaceCmd(PALM_VERIFY, pdata, 2); // 鉴权解锁 自动辨别人脸和掌静脉
//#else
               pdata[0] = 1; //自动关机
               pdata[1] = 2; //2秒
               Send_FaceCmd(MID_VERIFY, pdata, 2); // 鉴权解锁
//#endif
           }
        }
        else if(FACE_TIME>3000&&FACE_TIME<3500)
        {
            FACE_TIME=3000;
            pdata[0]=1;  //自动关机
            pdata[1]=2;  //2秒
#ifdef Palm_EN
            Send_FaceCmd(PALM_VERIFY, pdata, 2); // 鉴权解锁 自动辨别人脸和掌静脉
#else
            Send_FaceCmd(MID_VERIFY,pdata,2); // 鉴权解锁
#endif
        }
        Check_FACE_TimeOut();
        break;
    case FACE_ADD_USER_ACK1:
        if(Face_Rx_data(Face_rxdata))
        {   //判断正向人脸录入成功
            if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_ENROLL_ITG&&Face_rxdata[1]==MR_SUCCESS
                    &&Face_rxdata[4]==FACE_MIDDLE)
            {
                tal_system_sleep(600);
                // i=WR_ENROLL_ITG(pdata,FACE_RIGHT); // 录入朝右人脸
                // Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
                // SPK_Wdata(CN_PLS_FACE_RIGHT);//请把脸偏向右手边
                FACE_Work_Mode=FACE_STANDBY;
                setting_flag = SET_INPW;
                FACE_TIME=12000;//12S
                // SET_TIME=set_maxtime*3;
                // Dispay_fled(6);

                FACE_ID=Face_rxdata[2];//第七个字节
                FACE_ID<<=8;
                FACE_ID+=Face_rxdata[3];//第8个字节
                // FACE_WR(FACE_ID);

                face_voice_flag = 1;
                FACE_ADD_TIME = 80000;
                FACE_TIME = 80000;
            }
            else
            {
                USER_EXISTED();
            }
            break;
        }
        // if((FACE_TIME<10500&&FACE_TIME>9500)||(FACE_TIME<6500&&FACE_TIME>5500))
        // {
        //     if(FACE_TIME<6500&&FACE_TIME>5500) FACE_TIME=5500;
        //     if(FACE_TIME<10500&&FACE_TIME>9500) FACE_TIME=9500;
        //     i=WR_ENROLL_ITG(pdata,FACE_MIDDLE);
        //     Send_FaceCmd(MID_ENROLL_ITG,pdata,i); //集成支持并扩展所有录入方式
        // }
        Check_FACE_TimeOut();
        break;
//     case FACE_ADD_USER_ACK2:
//         if(Face_Rx_data(Face_rxdata))
//         {
//             if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_ENROLL_ITG&&Face_rxdata[1]==MR_SUCCESS
//                     &&Face_rxdata[4]==(FACE_MIDDLE+FACE_RIGHT))
//             {
//                 tal_system_sleep(600);
//                 i=WR_ENROLL_ITG(pdata,FACE_LEFT);// 录入朝左人脸
//                 Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//                 // SPK_Wdata(CN_PLS_FACE_LEFT);
//                 FACE_Work_Mode=FACE_ADD_USER_ACK3;
//                 FACE_TIME=12000;
//                 // SET_TIME=set_maxtime*3;
//                 Dispay_fled(4);
//             }
//             else
//             {
//                 USER_EXISTED();
//             }
//             break;
//         }
//         if((FACE_TIME<10500&&FACE_TIME>9500)||(FACE_TIME<6500&&FACE_TIME>5500))
//         {
//             if(FACE_TIME<6500&&FACE_TIME>5500) FACE_TIME=5500;
//             if(FACE_TIME<10500&&FACE_TIME>9500) FACE_TIME=9500;
//             i=WR_ENROLL_ITG(pdata,FACE_RIGHT); // 录入朝右人脸
//             Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//         }
//         Check_FACE_TimeOut();
//         break;
//     case FACE_ADD_USER_ACK3:
//         if(Face_Rx_data(Face_rxdata))
//         {
//             if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_ENROLL_ITG&&Face_rxdata[1]==MR_SUCCESS
//                     &&Face_rxdata[4]==FACE_MIDDLE+FACE_RIGHT+FACE_LEFT)
//             {
//                 tal_system_sleep(600);
//                 i=WR_ENROLL_ITG(pdata,FACE_DOWN);// 录入朝下人脸
//                 Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//                 // SPK_Wdata(CN_FACE_DOWN);//请微微低头
//                 FACE_Work_Mode=FACE_ADD_USER_ACK4;
//                 FACE_TIME=12000;
//                 // SET_TIME=set_maxtime*3;
//                 Dispay_fled(8);
//             }
//             else
//             {
//                 USER_EXISTED();
//             }
//             break;
//         }
//         if((FACE_TIME<10500&&FACE_TIME>9500)||(FACE_TIME<6500&&FACE_TIME>5500))
//         {
//             if(FACE_TIME<6500&&FACE_TIME>5500) FACE_TIME=5500;
//             if(FACE_TIME<10500&&FACE_TIME>9500) FACE_TIME=9500;
//             i=WR_ENROLL_ITG(pdata,FACE_LEFT); // 录入朝右人脸
//             Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//         }
//         Check_FACE_TimeOut();
//         break;
//     case FACE_ADD_USER_ACK4:
//         if(Face_Rx_data(Face_rxdata))
//         {
//             if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_ENROLL_ITG&&Face_rxdata[1]==MR_SUCCESS
//                     &&Face_rxdata[4]==FACE_MIDDLE+FACE_RIGHT+FACE_LEFT+FACE_DOWN)
//             {
//                 tal_system_sleep(600);
//                 i=WR_ENROLL_ITG(pdata,FACE_UP);// 录入朝上人脸
//                 Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//                 // SPK_Wdata(CN_FACE_UP);//请微微抬头
//                 FACE_Work_Mode=FACE_ADD_USER_ACK5;
//                 FACE_TIME=12000;
//                 // SET_TIME=set_maxtime*3;
//                 Dispay_fled(2);
//             }
//             else
//             {
//                 USER_EXISTED();
//             }
//             break;
//         }

//         if((FACE_TIME<10500&&FACE_TIME>9500)||(FACE_TIME<6500&&FACE_TIME>5500))
//         {
//             if(FACE_TIME<6500&&FACE_TIME>5500) FACE_TIME=5500;
//             if(FACE_TIME<10500&&FACE_TIME>9500) FACE_TIME=9500;
//             i=WR_ENROLL_ITG(pdata,FACE_DOWN); // 录入朝右人脸
//             Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//         }
//         Check_FACE_TimeOut();
//         break;
//     case FACE_ADD_USER_ACK5:
//         if(Face_Rx_data(Face_rxdata))
//         {
//             if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_ENROLL_ITG&&Face_rxdata[1]==MR_SUCCESS
//                     &&Face_rxdata[4]==FACE_MIDDLE+FACE_RIGHT+FACE_LEFT+FACE_DOWN+FACE_UP)
//             {
//                 FACE_ID=Face_rxdata[2];//第七个字节
//                 FACE_ID<<=8;
//                 FACE_ID+=Face_rxdata[3];//第8个字节
// #ifdef Debug_ENABLE
//                 printf("FACE_ID=%d\r\n",FACE_ID);
// #endif
//                 FACE_TIME=2000;
//                 tal_system_sleep(100);
//                 // if(FACE_WR(FACE_ID)==User_full) SPK_Wdata(CN_USER_FULL);        //flash写入人脸
//                 // else
//                 {
//                     // SPK_Wdata(CN_OPERATION_OK); //操作成功；
//                     if(setting_flag==ADD_USER_FACE)
//                     {
//                         setting_flag=FACE_MANAGER;
//                     }
//                     Bread_STANDBY();//人脸待机模式
//                     // SET_TIME=set_maxtime*3;
//                     // if(APP_ADD_FACE_FLG==1)
//                     // {
//                     //     APP_ADD_FACE_FLG=0;
//                     //     BLE_time=0;
//                     //     KEY_TIME=500;
//                     //     LED_Wdata(0,0);
//                     //     Report_APP(0X01,0x04,0xFF,0X00,0x00);
//                     //     FACE_TIME=1000;
//                     // }
//                     // if(ALL_TEST_FLG == 1)
//                     // {
//                     //     ALL_TEST_FLG = 0;
//                     // }
//                     break;
//                 }
//             }
//             else
//             {
//                 USER_EXISTED();
//             }
//             break;
//         }
//         else
//         {
//             if((FACE_TIME<10500&&FACE_TIME>9500)||(FACE_TIME<6500&&FACE_TIME>5500))
//             {
//                 if(FACE_TIME<6500&&FACE_TIME>5500) FACE_TIME=5500;
//                 if(FACE_TIME<10500&&FACE_TIME>9500) FACE_TIME=9500;
//                 i=WR_ENROLL_ITG(pdata,FACE_UP); // 录入朝右人脸
//                 Send_FaceCmd(MID_ENROLL_ITG,pdata,i);
//             }
//             else  Check_FACE_TimeOut();
//         }
//         break;
    case FACE_DEL_ONE_ACK://删除单个人脸应答
        if(Face_Rx_data(Face_rxdata))
        {
            if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_DELUSER&&Face_rxdata[1]==MR_SUCCESS)
            {
                // DEL_ONE_FACE(FACE_ID); //单个删除人脸
                // SPK_Wdata(CN_OPERATION_OK); //操作成功；
                setting_flag=FACE_MANAGER;
                tal_system_sleep(200);
                Bread_STANDBY();
                break;
            }
            else if(RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_DELUSER&&Face_rxdata[1]==MR_FAILED4_UNKNOWNUSER)
            {
                // SPK_Wdata(CN_USER_NOT_EXISTED); //用户不存在
                setting_flag=FACE_MANAGER;
                Bread_STANDBY();
                break;
            }
        }
        Check_FACE_TimeOut(); //超时
        break;
    case FACE_DEL_ALL_ACK:
        if(Face_Rx_data(Face_rxdata))
        {
            if((RX_Msgid==MR_SUCCESS&&Face_rxdata[0]==MID_DELALL&&Face_rxdata[1]==MR_SUCCESS)
                    ||(RX_Msgid==MR_REJECTED&&Face_rxdata[0]==MR_SUCCESS))
            {
                // DEL_ALL_FACE();
//                 if(System_int==0)
//                 {
//                     setting_flag=FACE_MANAGER;
//                     SPK_Wdata(CN_OPERATION_OK);  //非恢复出厂状态
//                     tal_system_sleep(200);
//                     Bread_STANDBY();
//                     break;
//                 }
//                 else if(System_int==4)
//                 {
// #ifdef Palm_EN
//                     tal_system_sleep(300);
//                     Send_FaceCmd(PALM_DELALL,NULL,0);  //删除全部掌静脉
//                     FACE_Work_Mode=PALM_DEL_ALL_ACK;
//                     FACE_TIME=10000;
// #else
//                     System_int=3;
// #endif
//                     break;
//                 }
                tal_system_sleep(200);
                Bread_STANDBY();
            }
        }
//           else if(FACE_TIME<6500&&FACE_TIME>5500)
//           {
//                 FACE_TIME==5500;
//                 Send_FaceCmd(MID_DELALL,NULL,0);
//           }
        Check_FACE_TimeOut(); //超时
        break;
    default: //
//        FACE_Work_Mode=FACE_STANDBY;
        Bread_STANDBY();
        break;
    }
#else
    FACE_Work_Mode=FACE_STANDBY;
#endif
}

VOID hugo_ai_face_timer(VOID)
{
    if(FACE_TIME>0) FACE_TIME --;    
    if(FACE_ADD_TIME>0) FACE_ADD_TIME --;
    Face_RXtime += 1;
}

VOID hugo_ai_face_intimer(VOID)
{
    // if(FACE_TIME > 5000)
    {
        FACE_TIME = 10;
        FACE_Work_Mode=FACE_STANDBY;
        Face_error_ts = 0;
    }

}

VOID hugo_Face_uart_task(VOID)
{
    Face_uart_rx();
    Face_uart_task();

    // if(FACE_TIME>0) FACE_TIME--;
    // if(FACE_ADD_TIME>0) FACE_ADD_TIME--;
    // Face_RXtime ++;
}
