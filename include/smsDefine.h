#ifndef _SMSDEFINE_H
#define _SMSDEFINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 


#define MAX_RTSP_SERVER_CNT (1)

#define MAX_RTSP_SESSION_LEN	(32)
#define COMMON_STR_LEN	(128)
#define MAX_RTSP_CMD_LEN	(16)
#define MAX_RTSP_URL_PRE_SUFFIX_LEN	(16)
#define MAX_RTSP_URL_SUFFIX_LEN		(16)
#define MAX_RTSP_CSEQ_LEN		(16)
#define MAX_RTSP_SESSION_ID_LEN		(64)
#define RTSP_CONNECTION_BUFFER_LEN	(1024)
#define RTSP_SESSION_TIMEOUT_SECS	(65)

#define IPADDR_STR_LEN		(16)
#define RTSP_SESSIONID_LEN	(9)
#define INIT_RTP_SERVER_PORT	(50000)

#define MAX_TCP_SEND_BUF_SIZE	(200*1024*1024)

#define H264_START_CODE_LEN	(4)
#define H264_SPS_LEN		(24)
#define H264_PPS_LEN		(3)
#define H264_BS_SPS_IDX		(H264_START_CODE_LEN)
#define H264_BS_DATA_IDX	(H264_START_CODE_LEN + H264_SPS_LEN + H264_START_CODE_LEN + H264_PPS_LEN)

typedef enum	_FrameType{
	FRAME_TYPE_UNKNOWN = 0,
	FRAME_TYPE_H264_SPS,
	FRAME_TYPE_H264_PPS,
	FRAME_TYPE_H264_IDR,
	FRAME_TYPE_H264_P,
	FRAME_TYPE_H264_IDR_SPS_PPS,	// IDR frame including SPS and PPS
	FRAME_TYPE_AUDIO_AAC,
	FRAME_TYPE_RAW_DEPTH,
	FRAME_TYPE_RAW_AMPLITUDE,
	FRAME_TYPE_RAW_DEPTH_AND_AMPLITUDE,
	FRAME_TYPE_RAW_YUV,
} FrameType;

struct _Frame{
	int				type;
	unsigned int	size;
	unsigned int	seqnum;
	unsigned int	timestamp;
	unsigned int	timeResolution;
	unsigned char	*pData;
};

typedef struct _Frame 	Frame;

enum {
	SMS_RET_ERROR = 0,
	SMS_RET_OK		= 1,

	SMS_RET_ERR_INTERNAL = 101,
	SMS_RET_ERR_INVALID_REQUEST,
	SMS_RET_ERR_INCOMPLIETE_REQUEST,

	SMS_ERR_STREAM_NOT_FOUND,
	SMS_ERR_TRACK_NOT_FOUND,
	SMS_ERR_SESSION_NOT_FOUND,

	SMS_TCP_SEND_BUFFER_FULL,

	SMS_RET_ERR_UNKNONW = 199,
};

enum {
	SMS_TCP_STATUS_BUFFER_UNKNOWN = 0,
	SMS_TCP_STATUS_BUFFER_OK,
	SMS_TCP_STATUS_BUFFER_UNSTABLE,
	SMS_TCP_STATUS_BUFFER_WARNING,
	SMS_TCP_STATUS_BUFFER_FULL
};


typedef struct _Stream			Stream;
typedef struct _Track			Track;
typedef struct _RTSPSession		RTSPSession;
typedef struct _RTPSession		RTPSession;
typedef struct _RtspServer		RtspServer;
typedef struct _ClientConnection	ClientConnection;
typedef struct	 _RTSPConfiguration	RTSPConfiguration;
typedef struct _RTSPSessionList	RTSPSessionList;


typedef int (*clientHandlerFunc)(struct _RtspServer* server, struct _ClientConnection* conn);
typedef int (*SessionHandlerFunc)(struct _RTSPSession *session);

typedef int (*handlePLAYFunc)(RtspServer *server, ClientConnection *clientConn, const char *strId, const char *StreamName, const char *TrackName, char *rtpInfo);
typedef int (*handlePAUSEFunc)(RTSPSession *session);
typedef int (*handleGETPARAMFunc)(RTSPSession *session);
typedef int (*handleSETPARAMFunc)(RTSPSession *session);
typedef int (*handleSETUPFunc)(RtspServer *server, ClientConnection *clientConn, RTSPSession*session, RTSPConfiguration *config);
typedef int (*handleTEARDOWNFunc)(RTSPSession *session);
typedef int (*renewTerminationTimeFunc)(RTSPSession *session);

typedef char* (*getSDPFunc)(Stream *stream);
typedef char* (*getTrackSDPFunc)(Track *track);
typedef int (*getFrameFunc)(Track *track, Frame *frame, int *tcpSendStatus);

#ifdef __cplusplus
}
#endif


#endif
