#ifndef _SMSRTSP_H
#define _SMSRTSP_H


#ifdef __cplusplus
extern "C" {
#endif


#include "smsDefine.h"

#if 1
#define SK_ASSERT(x) {							\
	if (!(x)) {							\
		printf("Assertion failed!(%s:%d)\r\n", __FILE__, __LINE__); \
		for(;;); \
	}					\
}
#else
#define SK_ASSERT(x)
#endif

int appMutexLock(pthread_mutex_t *mutex);
int appMutexUnlock(pthread_mutex_t *mutex);
#define LOCK_MTX(x)	appMutexLock(x)
#define UNLOCK_MTX(x)	appMutexUnlock(x)

typedef enum _RTSPCmdNum{
	RTSP_CMD_OPTIONS = 0,
	RTSP_CMD_DESCRIBE,
	RTSP_CMD_SETUP,
	RTSP_CMD_GET_PARAMETER,
	RTSP_CMD_SET_PARAMETER,	
	RTSP_CMD_TEARDOWN,
	RTSP_CMD_PLAY,	
	RTSP_CMD_PAUSE,
	RTSP_CMD_MAX
} RTSPCmdNum;

typedef struct _RtspCmd{
	char	cmdstr[MAX_RTSP_CMD_LEN];
	RTSPCmdNum	cmdnum;
} RtspCmd;

enum	_tcpMessageType {
	MSG_TYPE_RTSP_REQUEST = 0,
	MSG_TYPE_INTERLEAVED
};

typedef enum	_rtcpMessageType {
	RTCP_MSG_TYPE_UNKNOWN = 0,
	RTCP_MSG_TYPE_SR,
	RTCP_MSG_TYPE_RR,
	RTCP_MSG_TYPE_SDES,
	RTCP_MSG_TYPE_BYE,
	RTCP_MSG_TYPE_APP
} rtcpMessageType;

struct _ClientConnection {
	int					clientSocket;
	struct sockaddr_in	clientAddr;
	clientHandlerFunc	handler;	
	char			clientBuf[RTSP_CONNECTION_BUFFER_LEN];
	unsigned int	clientBufLen;
	char			*pMsg;
	unsigned int	msgLen;
	char			*pRemain;
	unsigned int	remainLen;
	char			msgType;	// 0:RTSP request, 1:Interleaved message
	unsigned int	CSeq;
	char			rtspCmd;
	unsigned int	parseIndex;
	char			urlSuffix[MAX_RTSP_URL_SUFFIX_LEN];
	char			urlPreSuffix[MAX_RTSP_URL_PRE_SUFFIX_LEN];
	char			sessionIdStr[MAX_RTSP_SESSION_ID_LEN];
	char			sendBuf[RTSP_CONNECTION_BUFFER_LEN];
	unsigned int	sendBufLen;
	
	/* RTCP message */
	rtcpMessageType	rtcpType;
	unsigned int	rtcpMsgLen;
	char			*pRtcpMsg;	// point to pMsg
	
	/* TCP buffer */
	char			*sendTCPBuf;
	char			tcpSenderTerminate;
	unsigned int	head;
	unsigned int	tail;
	unsigned int	marker;
	unsigned int	loopCnt;
	int				tcpSendStatus;	
	pthread_t		tcpSenderId;
	pthread_mutex_t	tcpSenderMutex;
};

typedef struct _ClientNode{
	ClientConnection	conn;
	struct _ClientNode	*next;
} ClientNode;

typedef ClientNode* ClientNodePtr;

typedef struct _ClientList{
	int 			count;
	ClientNodePtr	head;
} ClientList;

typedef struct _RTPPacketHeader{
	unsigned int	version:2;
	unsigned int	padding:1;
	unsigned int	extension:1;
	unsigned int	cc:4;
	unsigned int	marker:1;
	unsigned int	payloadType:7;
	unsigned int	seqnum:16;
	unsigned int	timestamp;
	unsigned int	ssrc;
} RTPPacketHeader;

typedef struct _RTPPacketDesc{
	char	*pStartAddr;
	unsigned int	Len;
	unsigned int	Marker;
} RTPPacketDesc;

void PrintClientList(ClientList *lptr);
int RtspInit(int port);

extern	int gStreamId;
extern	int gVideoTrackId;
extern	int gAudioTrackId;


/* APIs */
char* rtspURLPrefix(int clientSocket, char *buf);
char* rtspStreamURL(Stream *stream, int clientSocket, char* buf);
char* rtspTrackURL(Stream *stream, Track *track, int clientSocket, char* buf);
int DestoryClientConnection(ClientList *CList, int fd);
Stream* LookupStreamByName(const char *name);;
Track* LookupTrackByName(const char *name);
Track* GetFirstTrackFromStream(Stream *pStream);

#ifdef __cplusplus
}
#endif

#endif
