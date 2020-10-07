#ifndef _SMSRTSP_H264_H
#define _SMSRTSP_H264_H

#ifdef __cplusplus
extern "C" {
#endif


#include "smsDefine.h"

int putH264FrameFunc(Track *track, Frame *frame, int *tcpSendStatus);
char* getH264SDPFunc(Track *track);
int setH264SDPFunc(Track *track, MediaInfo *pInfo);

#ifdef __cplusplus
}
#endif
#endif
