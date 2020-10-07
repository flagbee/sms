#ifndef _SMSRTSP_VIDEO_RAW_H
#define _SMSRTSP_VIDEO_RAW_H

#ifdef __cplusplus
extern "C" {
#endif


#include "smsDefine.h"

int putVideoRawFrameFunc(Track *track, Frame *frame, int *tcpSendStatus);
char* getVideoRawSDPFunc(Track *track);
int setVideoRawSDPFunc(Track *track, MediaInfo *pInfo);

#ifdef __cplusplus
}
#endif
#endif
