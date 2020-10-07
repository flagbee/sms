#ifndef _SMSRTSP_AAC_H
#define _SMSRTSP_AAC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "smsDefine.h"

int putAACFrameFunc(Track *track, Frame *frame, int *tcpSendStatus);
char* getAACSDPFunc(Track *track);
int setAACSDPFunc(Track *track, MediaInfo *pInfo);

#ifdef __cplusplus
}
#endif
#endif
