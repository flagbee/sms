#ifndef _SMS_H
#define _SMS_H

#include "smsDefine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 
Function 
	smsRTSPServerInit
Description 
	RTSP server initialization
Arguments
	- RTSP server port
	- Server name (rtsp://<ipaddress>:<port>/<server name>/<stream name>/<track name>)
Return value
	- success : server ID
	- failure : -1
*/
int smsRTSPServerInit(int port, const char *serverName);


/* 
Function 
	smsAddRTSPStream
Description 
	Add RTSP Stream to server
Arguments
	- RTSP server ID
	- Stream name (rtsp://<ipaddress>:<port>/<server name>/<stream name>/<track name>)
Return value
	- success : stream ID
	- failure : -1
*/
int smsAddRTSPStream(int serverid, const char *streamName);

/* 
Function 
	smsAddRTSPTrack
Description 
	Add RTSP Track to stream	
Arguments
	- RTSP stream ID
	- Track name (rtsp://<ipaddress>:<port>/<server name>/<stream name>/<track name>)
Return value
	- success : track ID
	- failure : -1
*/
int smsAddRTSPTrack(int streamId, const char *trackName, int trackType);

int smsRemoveRTSPTrack(int streamId, int trackId);

/* 
Function 
	smsStartRTSPServer
Description 
	Start RTSP Server	
Arguments
	- Server ID
Return value
	- success : SMS_RET_OK
	- failure : errors
*/
int smsStartRTSPServer(int serverid);


/*
Function
	smsStopRTSPServer
Description
	Stop RTSP Server
Arguments
	- Server ID
Return value
	- success : SMS_RET_OK
	- failure : errors
*/
int smsStopRTSPServer(int serverid);


/* 
Function 
	smsPutFrame
Description 
	Send frame data to Streaming server
Arguments
	- track ID
	- Frame data
Return value
	- success : SMS_RET_OK
	- failure : errors
*/
int smsPutFrame(int trackId, Frame *frame);


int smsSetMediaInfo(int trackId, MediaInfo *pInfo);

#ifdef __cplusplus
}
#endif

#endif
