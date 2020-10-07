#ifndef _SMSRTSP_PCM_H
#define _SMSRTSP_PCM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "smsDefine.h"

int putPCMFrameFunc(Track *track, Frame *frame, int *tcpSendStatus);
char* getPCMSDPFunc(Track *track);
int setPCMSDPFunc(Track *track, MediaInfo *pInfo);

#ifdef __cplusplus
}
#endif

#endif
