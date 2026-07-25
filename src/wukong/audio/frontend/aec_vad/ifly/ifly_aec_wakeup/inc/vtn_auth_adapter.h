#ifndef VTN_AUTH_ADAPTER_H_INCLUDED
#define VTN_AUTH_ADAPTER_H_INCLUDED

// 初始化认证模块
int vtn_net_auth_init(char * appid, char * sn, const char * token);

// 执行认证校验
char* vtn_net_auth_verify(void);

// 获取当前认证状态
int vtn_net_auth_get_status(void);

#endif // VTN_AUTH_ADAPTER_H_INCLUDED
