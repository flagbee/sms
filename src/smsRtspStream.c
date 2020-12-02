/*******select.c*********/
/*******Using select() for I / O multiplexing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <time.h>

#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspStream.h"
#include "smsRtspH264.h"
#include "smsRtspVideoRaw.h"
#include "smsRtspAAC.h"
#include "smsRtspPCM.h"
#include "sms.h"

//#define DEBUG_SMS_RTSP_STREAM
#if defined(DEBUG_SMS_RTSP_STREAM)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP-STREAM] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-STREAM-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

/**** Static Functions ******************************************************************/
static void InitTrackList(TrackList *lptr)
{
	SK_ASSERT(lptr);

	memset(lptr, 0x0, sizeof(TrackList));

	lptr->count = 0;
	lptr->head = NULL;
}

static void InsertTrack(TrackList *lptr, Track *track)
{
	TrackNodePtr new_nptr = NULL;

	SK_ASSERT(track);
	SK_ASSERT(lptr);

	new_nptr = (TrackNode*) malloc(sizeof(TrackNode));
	memset(new_nptr, 0x0, sizeof(TrackNode));
	memcpy(&new_nptr->track, track, sizeof(Track));

	/* Insert Stream */
	if (lptr->count == 0) {
		new_nptr->next = lptr->head;
		lptr->head = new_nptr;
	}
	else {
		TrackNodePtr tmp = lptr->head;
		int i = 0;
		for (i = 1; i < lptr->count; i++) {
			tmp = tmp->next;
		}
		new_nptr->next = tmp->next;
		tmp->next = new_nptr;
	}
	lptr->count++;
}

static int DeleteTrack(TrackList *lptr, int id)
{
	int i = 0, rval = SMS_RET_ERR_INTERNAL;

	TrackNodePtr tmp = NULL, prev = NULL;

	SK_ASSERT(lptr);

	tmp = lptr->head;
	prev = lptr->head;

	for (i = 0; i < lptr->count; i++) {
		if (tmp->track.Id == id) {
			if (i == 0) {
				lptr->head = tmp->next;
			}
			else {
				prev->next = tmp->next;
			}

			/* free Connection */
			free(tmp);

			lptr->count--;
			rval = SMS_RET_OK;
			break;
		}
		else {
			prev = tmp;
			tmp = tmp->next;
		}
	}

	if (rval != SMS_RET_OK) {
		ERRMSG("track id[%d] not found!", id);
	}

	return rval;
}

static Track* SearchTrackById(TrackList *lptr, int trackId)
{
	TrackNodePtr tmp = lptr->head;
	Track* rTrack = NULL;

	while (tmp != NULL) {
		if (tmp->track.Id == trackId) {
			break;
		}
		tmp = tmp->next;
	}

	if (tmp == NULL) {
		ERRMSG("track id[%d] not found!", trackId);
	}
	else {
		rTrack = &tmp->track;
	}

	return rTrack;
}

static void PrintTrackList(TrackList *lptr)
{
	TrackNodePtr tmp = lptr->head;

	DBGMSG("List value: ");
	while (tmp != NULL) {
		DBGMSG("[%s]", tmp->track.name);
		tmp = tmp->next;
	}
	DBGMSG("Total: %d tracks", lptr->count);
}

static void InitStreamList(StreamList *lptr)
{
	memset(lptr, 0x0, sizeof(StreamList));

	lptr->count = 0;
	lptr->head = NULL;
}

static void InsertStream(StreamList *lptr, Stream *stream)
{
	StreamNodePtr new_nptr = NULL;

	new_nptr = (StreamNode*) malloc(sizeof(StreamNode));
	memset(new_nptr, 0x0, sizeof(StreamNode));
	memcpy(&new_nptr->stream, stream, sizeof(Stream));

	if (lptr->count == 0) {
		new_nptr->next = lptr->head;
		lptr->head = new_nptr;
	}
	else {
		StreamNodePtr tmp = lptr->head;
		int i = 0;
		for (i = 1; i < lptr->count; i++) {
			tmp = tmp->next;
		}
		new_nptr->next = tmp->next;
		tmp->next = new_nptr;
	}
	lptr->count++;
}

static int DeleteStream(StreamList *lptr, int id)
{
	int i = 0, rval = SMS_RET_ERR_INTERNAL;

	StreamNodePtr tmp = lptr->head, prev = lptr->head;

	for (i = 0; i < lptr->count; i++) {
		if (tmp->stream.Id == id) {
			if (i == 0) {
				lptr->head = tmp->next;
			}
			else {
				prev->next = tmp->next;
			}

			/* free Connection */
			free(tmp);

			lptr->count--;
			rval = SMS_RET_OK;
			break;
		}
		else {
			prev = tmp;
			tmp = tmp->next;
		}
	}

	if (rval != SMS_RET_OK) {
		ERRMSG("stream id[%d] not found!", id);
	}

	return rval;
}

static Stream* SearchStreamById(RtspServer *server, int id)
{
	StreamList *lptr = NULL;
	StreamNodePtr tmp = NULL;
	Stream* rStream = NULL;

	SK_ASSERT(server);

	lptr = &server->streamList;
	tmp = lptr->head;

	while (tmp != NULL) {
		if (tmp->stream.Id == id) {
			break;
		}
		tmp = tmp->next;
	}

	if (tmp == NULL) {
		ERRMSG("stream id[%d] not found!", id);
	}
	else {
		rStream = &tmp->stream;
	}

	return rStream;
}

static void PrintStreamList(StreamList *lptr)
{
	StreamNodePtr tmp = lptr->head;

	DBGMSG("List value: ");
	while (tmp != NULL) {
		DBGMSG("[%s]", tmp->stream.name);
		PrintTrackList(&tmp->stream.trackList);
		tmp = tmp->next;
	}
	DBGMSG("Total: %d Streams", lptr->count);
}

/* RTP Session List */
static void InitRTPSessionList(RTPSessionList *lptr)
{
	SK_ASSERT(lptr);

	memset(lptr, 0x0, sizeof(RTPSessionList));

	pthread_mutex_init(&lptr->rtpSessionListMutex, NULL);
	lptr->count = 0;
	lptr->head = NULL;
}

static void PrintRTPSessionList(RTPSessionList *lptr)
{
	RTPSessionNodePtr tmp = lptr->head;

	DBGMSG("List value: ");
	while (tmp != NULL) {
		DBGMSG("[%s]", tmp->session.strId);
		tmp = tmp->next;
	}
	DBGMSG("Total: %d rtpSessions", lptr->count);
}

static void InsertRTPSession(RTPSessionList *lptr, RTPSession *rtpSession)
{
	RTPSessionNodePtr new_nptr = NULL;
	RTPSessionNodePtr tmp = NULL, prev = NULL;
	int i = 0, count = 0;

	SK_ASSERT(rtpSession);
	SK_ASSERT(lptr);

	new_nptr = (RTPSessionNode*) malloc(sizeof(RTPSessionNode));
	if ( new_nptr == NULL ) {
		ERRMSG("malloc failed");
	}
	else {
		memset(new_nptr, 0x0, sizeof(RTPSessionNode));
		memcpy(&new_nptr->session, rtpSession, sizeof(RTPSession));

		/* Flush RTP Session List */
		LOCK_MTX(&lptr->rtpSessionListMutex);
		tmp = lptr->head;
		prev = lptr->head;
		count = lptr->count;
		
		for (i = 0; i < count; i++) {
			prev = tmp;
			tmp = tmp->next;
			
		}

		/* Insert Stream */
		if (lptr->count == 0) {
			new_nptr->next = lptr->head;
			lptr->head = new_nptr;
		}
		else {
			RTPSessionNodePtr tmp = lptr->head;
			int i = 0;
			for (i = 1; i < lptr->count; i++) {
				tmp = tmp->next;
			}
			new_nptr->next = tmp->next;
			tmp->next = new_nptr;
		}
		lptr->count++;
		UNLOCK_MTX(&lptr->rtpSessionListMutex);
	}
}

static int DeleteRTPSession(RtspServer *server, RTPSessionList *lptr, const char* strId)
{
	int i = 0, rval = SMS_RET_ERR_INTERNAL, NodeCnt = 0;

	RTPSessionNodePtr tmp = NULL, prev = NULL;

	SK_ASSERT(lptr);	

	tmp = lptr->head;
	prev = lptr->head;
	NodeCnt = lptr->count;

	for (i = 0; i < NodeCnt; i++) {
		if (strcmp(tmp->session.strId, strId) == 0) {
			if (i == 0) {
				lptr->head = tmp->next;
			}
			else {
				prev->next = tmp->next;
			}

			if (tmp->session.mode == RTP_UDP) {
				close(tmp->session.rtpSocket);
				LOCK_MTX(&server->rtcpHandlerMutex);
				close(tmp->session.rtcpSocket);
				FD_CLR(tmp->session.rtcpSocket, &server->rtcpFds);
				UNLOCK_MTX(&server->rtcpHandlerMutex);
			}

			/* free Connection */
			free(tmp);

			lptr->count--;
			rval = SMS_RET_OK;
			break;
		}
		else {
			prev = tmp;
			tmp = tmp->next;
		}
	}


	if (rval != SMS_RET_OK) {
		ERRMSG("rtpSession id[%s] not found!", strId);
	}

	return rval;
}

static RTPSession* SearchRTPSession(RTPSessionList *lptr, const char *strId)
{
	RTPSessionNodePtr tmp = lptr->head;
	RTPSession* rRTPSession = NULL;

	while (tmp != NULL) {
		if (strcmp(tmp->session.strId, strId) == 0) {
			break;
		}
		tmp = tmp->next;
	}

	if (tmp == NULL) {
		ERRMSG("rtpSession id[%s] not found!", strId);
	}
	else {
		rRTPSession = &tmp->session;
	}

	return rRTPSession;
}

int SetStateRTPSession(RTPSessionList *lptr, const char *strId, RTPSessionState state)
{
	RTPSession *pRtpSession = NULL;
	int rval = SMS_RET_ERR_INTERNAL;

	SK_ASSERT(lptr);
	SK_ASSERT(strId);

	/* Find RTP Session */
	LOCK_MTX(&lptr->rtpSessionListMutex);
	pRtpSession = SearchRTPSession(lptr, strId);
	if (pRtpSession) {
		pRtpSession->state = state;
		rval = SMS_RET_OK;
	}
	UNLOCK_MTX(&lptr->rtpSessionListMutex);

	return rval;
}

int StartRTPStreaming(RTPSessionList *lptr, const char *strId, unsigned int *ssrc, unsigned int *timestampBase, unsigned short *seqNum)
{
	RTPSession *pRtpSession = NULL;
	int rval = SMS_RET_ERR_INTERNAL;

	SK_ASSERT(lptr);
	SK_ASSERT(strId);
	SK_ASSERT(ssrc);
	SK_ASSERT(timestampBase);
	SK_ASSERT(seqNum);

	/* Find RTP Session */
	LOCK_MTX(&lptr->rtpSessionListMutex);
	pRtpSession = SearchRTPSession(lptr, strId);
	if (pRtpSession) {
		pRtpSession->state = RTP_STATE_WAIT_FOR_KEY_FRAME;
		*ssrc = pRtpSession->ssrc;
		*timestampBase = pRtpSession->timestampBase + pRtpSession->track->lastPTS;
		//baek.debug
		//DBGMSG("pRtpSession->timestampBase[%X] lastPTS[%X] timestampBase[%X]", pRtpSession->timestampBase, pRtpSession->track->lastPTS, *timestampBase);
		*seqNum = pRtpSession->seqNum;
		rval = SMS_RET_OK;
	}
	else {
		ERRMSG("RTP Session[%s] Not Found!", strId);
		rval = SMS_ERR_SESSION_NOT_FOUND;
	}
	UNLOCK_MTX(&lptr->rtpSessionListMutex);

	return rval;
}

void PrintStreams(RtspServer *server)
{
	SK_ASSERT(server);

	PrintStreamList(&server->streamList);
}

static const char sdpPrefixFmt[] = "v=0\r\n"
	"o=- %ld%06ld %d IN IP4 %s\r\n"
	"s=%s\r\n"
	"i=%s\r\n"
	"t=0 0\r\n"
	"a=tool:%s%s\r\n"
	"a=type:broadcast\r\n"
	"a=control:*\r\n"
	"%s"
	"%s"
	"a=x-qt-text-nam:%s\r\n"
	"a=x-qt-text-inf:%s\r\n"
	"%s";
static const char sdpDescription[] = "RTSP session";
static const char sdpInfo[] = "RTSP session infomation";
static const char sdpName[] = "SMS";
static const char sdpSourceFilterLine[] = "";
static const char sdpRangeLine[] = "a=range:npt=0-\r\n";
static const char sdpVersion[] = "v000";
static const char sdpMisc[] = "";
static const char sdpIpAddress[] = "192.168.10.102";

char *StreamGetSDPFunc(Stream *stream)
{
	char *rSdp = NULL;
	int sdpLength = 0, ipAddressStrSize = 0;

	SK_ASSERT(stream);

	ipAddressStrSize = strlen(sdpIpAddress);
	/* If sdp is not exist, create new sdp */
	sdpLength += strlen(sdpPrefixFmt);
	sdpLength += 20 + 6 + 20 + ipAddressStrSize;
	sdpLength += strlen(sdpDescription); //  1
	sdpLength += strlen(sdpInfo);			// 1
	sdpLength += strlen(sdpName) + strlen(sdpVersion);
	sdpLength += strlen(sdpSourceFilterLine);
	sdpLength += strlen(sdpRangeLine);
	sdpLength += strlen(sdpDescription); //  2
	sdpLength += strlen(sdpInfo);	// 2
	sdpLength += strlen(sdpMisc);
	sdpLength += 1000; 			// in case the length of the "subsession->sdpLines()" calls below change

	sdpLength += 1024;

	if (stream->sdp == NULL) {
		stream->sdp = (char *) malloc(sdpLength);
	}

	if (stream->sdp) {
		TrackNodePtr pNode = NULL;
		char* mediaSDP = NULL;

		memset(stream->sdp, 0x0, sdpLength);
		snprintf(stream->sdp, sdpLength - 1, sdpPrefixFmt, stream->creationTime.tv_sec, stream->creationTime.tv_usec, // o= <session id>
			1, // o= <version> // (needs to change if params are modified)
			sdpIpAddress, // o= <address>
			sdpDescription, // s= <description>
			sdpInfo, // i= <info>
			sdpName, sdpVersion, // a=tool:
			sdpSourceFilterLine, // a=source-filter: incl (if a SSM session)
			sdpRangeLine, // a=range: line
			sdpDescription, // a=x-qt-text-nam: line
			sdpInfo, // a=x-qt-text-inf: line
			sdpMisc); // miscellaneous session SDP lines (if any)

		mediaSDP = stream->sdp;
		do {
			pNode = NextTrackNode(&stream->trackList, pNode);
			if (pNode) {
				char* sdpLines = NULL;
				unsigned int mediaSDPLength = 0;

				DBGMSG(">>>>>>>> 1 track[%s]", pNode->track.name);
				mediaSDPLength = strlen(mediaSDP);
				if (mediaSDPLength < sdpLength) {
					mediaSDP += mediaSDPLength;
					sdpLength -= mediaSDPLength;
					if (sdpLength <= 1) {
						break; // the SDP has somehow become too long
					}

					sdpLines = pNode->track.getSDP(&pNode->track);
					if (sdpLines != NULL) {
						snprintf(mediaSDP, sdpLength - 1, "%s", sdpLines);
					}
				}
				else {
					ERRMSG("mediaSDPLength(%d) < sdpLength(%d)", mediaSDPLength, sdpLength);
				}
			}
		} while (pNode);
	}
	else {
		ERRMSG("malloc fail! len[%d]", sdpLength);
	}

	DBGMSG("sdp[%s]", stream->sdp);

	rSdp = stream->sdp;

	return rSdp;
}

static Track* SearchTrackFromServerById(RtspServer *server, int trackId)
{
	StreamList *lptr = NULL;
	StreamNodePtr tmp = NULL;
	Track *rTrack = NULL;

	SK_ASSERT(server);

	lptr = &server->streamList;
	tmp = lptr->head;

	while (tmp != NULL) {
		rTrack = SearchTrackById(&tmp->stream.trackList, trackId);
		if (rTrack) {
			break;
		}
		tmp = tmp->next;
	}

	if (tmp == NULL) {
		ERRMSG("track id[%d] not found!", trackId);
	}

	return rTrack;
}

/**** APIs ******************************************************************/
/* 
 Function 
 CreateStream 
 Description 
 Create new stream
 Arguments
 - RTSPServer pointer
 - Stream url(stream name)
 Return value
 - success : stream id
 - failure : -1	
 */
int CreateStream(RtspServer *server, const char *name)
{
	int rval = 0;
	Stream newStream;

	SK_ASSERT(server);
	SK_ASSERT(name);

	LOCK_MTX(&server->rtspSeverMutex);

	memset(&newStream, 0x0, sizeof(Stream));

	snprintf(newStream.name, MAX_RTSP_URL_SUFFIX_LEN - 1, "%s", name);
	newStream.getSDP = StreamGetSDPFunc;
	//FIXME.20160126. stream ID
	newStream.Id = 100;
	gettimeofday(&newStream.creationTime, NULL);
	InsertStream(&server->streamList, &newStream);
	newStream.LatestTrackId = 0;

	rval = newStream.Id;

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

/* 
 Function 
 CreateTrack
 Description 
 Create new track and add to stream
 Arguments
 - Stream pointer
 - track name
 - track type
 Return value
 - success : track id
 - failure : -1
 */
int CreateTrack(Stream *stream, const char *url, TrackType type)
{
	int rval = 0;
	Track newTrack;

	SK_ASSERT(stream);
	SK_ASSERT(url);

	memset(&newTrack, 0x0, sizeof(Track));

	snprintf(newTrack.name, MAX_RTSP_URL_SUFFIX_LEN - 1, "%s", url);

	newTrack.type = type;
	switch (type) {
	case TRK_TYPE_H264:
		//FIXME.20160126. track ID
//		newTrack.Id = 201;
		stream->LatestTrackId++;
		newTrack.Id = stream->LatestTrackId;
		newTrack.timeResolution = 90000;
		newTrack.putFrame = putH264FrameFunc;
		newTrack.getSDP = getH264SDPFunc;
		break;
	case TRK_TYPE_VIDEO_RAW:
//		newTrack.Id = 202;
		stream->LatestTrackId++;
		newTrack.Id = stream->LatestTrackId;
		newTrack.timeResolution = 90000;
		newTrack.putFrame = putVideoRawFrameFunc;
		newTrack.getSDP = getVideoRawSDPFunc;
		break;
	case TRK_TYPE_AAC:
		//FIXME.20160126. track ID			
//		newTrack.Id = 203;
		stream->LatestTrackId++;
		newTrack.Id = stream->LatestTrackId;
		newTrack.timeResolution = 48000;
		newTrack.putFrame = putAACFrameFunc;
		newTrack.getSDP = getAACSDPFunc;
		break;
	case TRK_TYPE_PCM_L16:
		//FIXME.20160126. track ID
//		newTrack.Id = 204;
		stream->LatestTrackId++;
		newTrack.Id = stream->LatestTrackId;
		newTrack.timeResolution = 8000;
		newTrack.putFrame = putPCMFrameFunc;
		newTrack.getSDP = getPCMSDPFunc;
		break;
	default:
		break;
	}
	InitRTPSessionList(&newTrack.rtpSessionList);
	InsertTrack(&stream->trackList, &newTrack);

	rval = newTrack.Id;

	return rval;
}

/* 
 Function 
 AddTrack
 Description 
 Wrapper function of CreateTrack
 Arguments
 - Stream ID
 - track name
 Return value
 - success : track id
 - failure : -1
 */

int AddTrack(RtspServer *server, int streamId, const char *url, TrackType type)
{
	int rval = 0;
	Stream* pStream = NULL;

	SK_ASSERT(server);
	SK_ASSERT(url);

	LOCK_MTX(&server->rtspSeverMutex);

	pStream = SearchStreamById(server, streamId);

	rval = CreateTrack(pStream, url, type);

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

int RemoveTrack(RtspServer *server, int streamId, int trackId)
{
	int rval = 0;
	Stream* pStream = NULL;

	SK_ASSERT(server);

	LOCK_MTX(&server->rtspSeverMutex);
	
	pStream = SearchStreamById(server, streamId);
	if( pStream ){
		rval = DeleteTrack(&pStream->trackList, trackId);
		if( rval != SMS_RET_OK ){
			ERRMSG("DeleteTrack failed");
		}
	}
	else{
		ERRMSG("SearchStreamById failed");
	}
	
	UNLOCK_MTX(&server->rtspSeverMutex);
	
	return rval;
}

/* 
 Function 
 DestoryAllTrack
 Description 
 Destory All Tracks of stream
 Arguments
 - server pointer
 - Stream ID
 Return value
 - success : 1
 - failure : -1
 */
int DestoryAllTrack(RtspServer *server, int streamId)
{
	int rval = 0;

	LOCK_MTX(&server->rtspSeverMutex);

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

/* 
 Function 
 DestoryAllStream
 Description 
 Destory All Stream of server
 Arguments
 - server pointer
 Return value
 - success : 1
 - failure : -1
 */
int DestoryAllStream(RtspServer *server)
{
	int rval = 0;

	LOCK_MTX(&server->rtspSeverMutex);

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

/* 
 Function 
 AddRTPSession
 Description 
 Add a RTP session to Track
 Arguments
 - track ID
 - RTPSession pointer(not include session ID)
 Return value
 - success : RTP Session ID
 - failure : -1
 */
int AddRTPSession(RtspServer *server, int trackId, RTPSession *rtpSession)
{
	int rval = 0;
	Track *pTrack = NULL;

	SK_ASSERT(server);
	SK_ASSERT(rtpSession);

	LOCK_MTX(&server->rtspSeverMutex);

	pTrack = SearchTrackFromServerById(server, trackId);
	if (pTrack) {
		rtpSession->track = pTrack;
		InsertRTPSession(&pTrack->rtpSessionList, rtpSession);
		rval = 1;
	}

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

/* 
 Function 
 DestoryRTPSession
 Description 
 Destory RTP session
 Arguments
 - track ID
 - RTPSession pointer(not include session ID)
 Return value
 - success : RTP Session ID
 - failure : -1
 */
int DestoryRTPSession(RtspServer *server, const char *strId)
{
	int rval = 0, foundSession = 0;
	StreamNodePtr pStreamNode = NULL;
	TrackNodePtr pTrackNode = NULL;

	SK_ASSERT(server);
	SK_ASSERT(strId);

	LOCK_MTX(&server->rtspSeverMutex);

	do {
		pStreamNode = NextStreamNode(&server->streamList, pStreamNode);
		if (pStreamNode) {
			do {
				pTrackNode = NextTrackNode(&pStreamNode->stream.trackList, pTrackNode);
				if (pTrackNode) {
					Track *pTrack = &pTrackNode->track;
					LOCK_MTX(&pTrack->rtpSessionList.rtpSessionListMutex);
					if (0 < pTrack->rtpSessionList.count) {
						rval = DeleteRTPSession(server, &pTrack->rtpSessionList, strId);
						if (rval == SMS_RET_OK) {
							DBGMSG("DEL Session[%s] of Track[%s]", strId, pTrack->name);
							foundSession++;
						}
					}
					UNLOCK_MTX(&pTrack->rtpSessionList.rtpSessionListMutex);
				}
			} while (pTrackNode);
		}
	} while (pStreamNode);

	if (foundSession == 0) {
		rval = SMS_ERR_SESSION_NOT_FOUND;
	}
	else {
		rval = SMS_RET_OK;
	}

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

/* 
 Function 
 NextRTPSessionNode
 Description 
 retrieve Next RTP Session Node 
 Arguments
 - RTP Session List
 - RTP Session Node pointer. NULL means first node.
 Return value
 - success : 1
 - failure : -1
 */
RTPSessionNodePtr NextRTPSessionNode(RTPSessionList *lptr, RTPSessionNodePtr pNode)
{
	RTPSessionNodePtr tmp = NULL;

	SK_ASSERT(lptr);

	if (pNode) {
		//tmp = pNode;
		tmp = pNode->next;
	}
	else {
		tmp = lptr->head;
	}

	return tmp;
}

int PutFrame(RtspServer *server, int trackId, Frame *frame, int *tcpSendStatus)
{
	int rval = 0;
	Track *pTrack = NULL;

	SK_ASSERT(server);
	SK_ASSERT(frame);

	LOCK_MTX(&server->rtspSeverMutex);

	pTrack = SearchTrackFromServerById(server, trackId);
	if (pTrack) {
		rval = pTrack->putFrame(pTrack, frame, tcpSendStatus);
		pTrack->lastPTS = frame->timestamp;
	}
	else {
		ERRMSG("track[%d] not found", trackId);
	}

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}

/* 
 Function 
 NextTrackNode
 Description 
 retrieve Next Track Node 
 Arguments
 - Track List
 - Track pointer. NULL means first node.
 Return value
 - success : 1
 - failure : -1
 */
TrackNodePtr NextTrackNode(TrackList *lptr, TrackNodePtr pNode)
{
	TrackNodePtr tmp = NULL;

	SK_ASSERT(lptr);

	if (pNode) {
		tmp = pNode->next;
	}
	else {
		tmp = lptr->head;
	}

	return tmp;
}

/* 
 Function 
 NextStreamkNode
 Description 
 retrieve Next Stream Node 
 Arguments
 - Stream List
 - Stream pointer. NULL means first node.
 Return value
 - success : 1
 - failure : -1
 */
StreamNodePtr NextStreamNode(StreamList *lptr, StreamNodePtr pNode)
{
	StreamNodePtr tmp = NULL;

	SK_ASSERT(lptr);

	if (pNode) {
		tmp = pNode->next;
	}
	else {
		tmp = lptr->head;
	}

	return tmp;
}

int RenewMediaInfo(RtspServer *server, int trackId, MediaInfo *pInfo)
{
	int rval = 0;
	Track *pTrack = NULL;

	SK_ASSERT(server);
	SK_ASSERT(pInfo);

	LOCK_MTX(&server->rtspSeverMutex);

	pTrack = SearchTrackFromServerById(server, trackId);
	if (pTrack) {
		switch (pTrack->type) {
		case TRK_TYPE_H264:
			setH264SDPFunc(pTrack, pInfo);
			break;
		case TRK_TYPE_VIDEO_RAW:
			setVideoRawSDPFunc(pTrack, pInfo);
			break;
		case TRK_TYPE_AAC:
			setAACSDPFunc(pTrack, pInfo);
			break;
		case TRK_TYPE_PCM_L16:
			setPCMSDPFunc(pTrack, pInfo);
			break;
		}
	}
	else {
		ERRMSG("track[%d] not found", trackId);
	}

	UNLOCK_MTX(&server->rtspSeverMutex);

	return rval;
}
