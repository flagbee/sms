#ifndef _SMSRTSP_STREAM_H
#define _SMSRTSP_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <netinet/in.h>

#include "smsDefine.h"
#include "smsRtsp.h"

/* RTP Session List */
enum _RTPSessionState{
	RTP_STATE_UNKNOWN = 0,
	RTP_STATE_IDLE,
	RTP_STATE_WAIT_FOR_KEY_FRAME,
	RTP_STATE_PLAY,
	RTP_STATE_PAUSE,
	RTP_STATE_TO_BE_REMOVED,
};
typedef enum _RTPSessionState	RTPSessionState;

enum _StreamingMode{
	RTP_UDP = 0,
	RTP_RTSP_TCP,
	RTP_TCP,
	RAW_UDP,
};
typedef enum _StreamingMode	StreamingMode;

struct _RTPSession{
	char	strId[RTSP_SESSIONID_LEN];
	StreamingMode	mode;	
	RTPSessionState	state;
	int		rtpSocket;
	int		rtcpSocket;	
	int		rtpChannel;
	int		rtcpChannel;
	struct sockaddr_in	rtpAddr;
	struct sockaddr_in	rtcpAddr;
	unsigned short seqNum;
	unsigned short prevSeqNum;
	unsigned int ssrc;	
	unsigned int timeResolution;	
	unsigned int timestampBase;
	unsigned int timestampLast;
	Track*	track;
	RTSPSession*	rtspSession;
};

typedef struct _RTPSessionNode {
	RTPSession	session;
	struct _RTPSessionNode	*next;
} RTPSessionNode;

typedef RTPSessionNode* RTPSessionNodePtr;

typedef struct _RTPSessionList{
	int 			count;
	RTPSessionNode *head;
	pthread_mutex_t	rtpSessionListMutex;
} RTPSessionList;

/* Track List */
enum	_TrackType {
	TRK_TYPE_H264 = 0,
	TRK_TYPE_AAC,
	TRK_TYPE_PCM_L16,
	TRK_TYPE_VIDEO_RAW,
};
typedef enum _TrackType		TrackType;


typedef struct _MediaInfoH264 {
	unsigned int	spsLen;
	unsigned char	sps[128];
} MediaInfoH264;

typedef struct _MediaInfoRaw {
	unsigned int	width;
	unsigned int	height;
} MediaInfoRaw;

typedef union _MediaInfo{
	MediaInfoH264	H264;
	MediaInfoRaw	Raw;
} MediaInfo;

struct _Track {
	char			name[MAX_RTSP_URL_SUFFIX_LEN];
	int				Id;
	TrackType		type;
	unsigned int	refCount;
	RTPSessionList	rtpSessionList;
	getFrameFunc	putFrame;
	getTrackSDPFunc		getSDP;
	char*			sdp;
	unsigned int	timeResolution;
	unsigned int	lastPTS;
	MediaInfo		mediaInfo;
};

typedef struct _TrackNode{
	Track	track;
	struct _TrackNode	*next;
} TrackNode;

typedef TrackNode* TrackNodePtr;

typedef struct _TrackList{
	int 			count;
	TrackNodePtr	head;
} TrackList;

/* Sream List */
struct _Stream {
	char			name[MAX_RTSP_URL_SUFFIX_LEN];
	int				Id;
	unsigned int	refCount;
	getSDPFunc		getSDP;
	char*			sdp;
	struct timeval creationTime;	
	TrackList		trackList;
	int				LatestTrackId;
};

typedef struct _StreamNode{
	Stream	stream;
	struct _StreamNode	*next;
} StreamNode;

typedef StreamNode* StreamNodePtr;

typedef struct _StreamList{
	int 			count;
	StreamNodePtr	head;
} StreamList;


/* Media List */
typedef struct _Media {
	int	trackId;
	int	rtpSessionId;
} Media;

typedef struct _MediaNode{
	Media	media;
	struct _MediaNode	*next;
} MediaNode;

typedef MediaNode* MediaNodePtr;

typedef struct _MediaList{
	int 			count;
	MediaNodePtr	head;
} MediaList;

/* RTSP Session List */
enum _RTSPSessionState{
	RTSP_STATE_UNKNOWN = 0,
	RTSP_STATE_IDLE,
	RTSP_STATE_PLAY,
	RTSP_STATE_PAUSE,	
};
typedef enum _RTSPSessionState	RTSPSessionState;

struct _RTSPSession{
	char	strId[RTSP_SESSIONID_LEN];
	handlePLAYFunc		handlePLAY;
	handlePAUSEFunc		handlePAUSE;
	handleGETPARAMFunc	handleGETPARAM;
	handleSETPARAMFunc	handleSETPARAM;
	handleSETUPFunc		handleSETUP;	
	handleTEARDOWNFunc	handleTEARDOWN;
	renewTerminationTimeFunc	handleRenewTermTime;
	RTSPSessionState	state;
	ClientConnection	*ClientConn;
	MediaList			mediaList;
	long				terminationUptime;
};

typedef struct _RTSPSessionNode {
	RTSPSession		session;
	struct _RTSPSessionNode	 *next;
} RTSPSessionNode;

typedef RTSPSessionNode* RTSPSessionNodePtr;

struct _RTSPSessionList{
	int 			count;
	RTSPSessionNodePtr	head;
};

/* RTSP Server */
struct _RtspServer {	
	char	name[MAX_RTSP_URL_PRE_SUFFIX_LEN];
	int	initdone;
	int	ServerPort;
	int	ServerSocket;	
	StreamList 		streamList;
	RTSPSessionList rtspSessionList;	
	ClientList		clientList;
	int			rtspRunning;
	pthread_t	rtspThreadId;
	pthread_mutex_t	rtspSeverMutex;
	
	int			rtcpHandlerRunning;
	pthread_t	rtcpHandlerThread;
	pthread_mutex_t	rtcpHandlerMutex;
	int			rtcpFdMax;
	fd_set		rtcpFds;
};


/* external */
StreamNodePtr NextStreamNode(StreamList *lptr, StreamNodePtr pNode);
TrackNodePtr NextTrackNode(TrackList *lptr, TrackNodePtr pNode);
RTPSessionNodePtr NextRTPSessionNode(RTPSessionList *lptr, RTPSessionNodePtr pNode);
int DestoryRTPSession(RtspServer *server, const char *strId);
int CreateStream(RtspServer *server, const char *name);
int AddTrack(RtspServer *server, int streamId, const char *url, TrackType type);
int RemoveTrack(RtspServer *server, int streamId, int trackId);
void PrintStreams(RtspServer *server);
int StartRTPStreaming(RTPSessionList *lptr, const char *strId, unsigned int *ssrc, unsigned int *timestampBase, unsigned short *seqNum);
int AddRTPSession(RtspServer *server, int trackId, RTPSession *rtpSession);
int PutFrame(RtspServer *server, int trackId, Frame *frame, int *tcpSendStatus);
int RenewMediaInfo(RtspServer *server, int trackId, MediaInfo *pInfo);

#ifdef __cplusplus
}
#endif

#endif

