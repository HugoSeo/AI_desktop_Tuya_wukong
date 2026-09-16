#ifndef _ESNR_H_
#define _ESNR_H_

typedef enum {
	WORK_MODE_NR,
	WORK_MODE_TEL,
	WORK_MODE_NR_OUTDOOR
}ESNRWorkMode;

typedef struct _ESNRConfig {
    char*       sram_addr;
    int         sram_size;
    int         work_mode;
    const void* res_aes_addr;
    int         res_aes_size;
	int         agc_target_level;
	int         enable_aec;
	int         enable_vad;
	int         enable_2mic;
}ESNRConfig;

#ifdef __cplusplus
extern "C" {
#endif

	int esnr_init(void **hdl, void *reserve);

	int esnr_uninit(void *hdl);

	int esnr_process(void *hdl, const void *in, void *out, int* vadout);

	int esnr_set_param(void *hdl, const int id, const void *value, int size);

	int esnr_get_param(void *hdl, const int id, void *value, int size);

	int esnr_get_version(void *hdl, const char **version);

	void engine_get_res(const unsigned char **addr, unsigned int* size);

#ifdef __cplusplus
}
#endif

#endif //_ESNR_H_
