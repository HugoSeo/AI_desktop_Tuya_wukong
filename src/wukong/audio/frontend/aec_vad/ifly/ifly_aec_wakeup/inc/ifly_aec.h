#ifndef _IFLY_AEC_H_
#define _IFLY_AEC_H_

#ifdef __cplusplus
extern "C" {
#endif

int ifly_aec_init(void);
int ifly_aec_uninit(void);
int ifly_aec_process(const short *in, short *out, int num_samples);
void ifly_aec_set_mic_number(int number);
int ifly_aec_get_mic_number(void);
const char* ifly_aec_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // _IFLY_AEC_H_
