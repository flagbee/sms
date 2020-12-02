/*******select.c*********/
/*******Using select() for I / O multiplexing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <time.h>
#include <string.h>
#include <errno.h>

#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspStream.h"
#include "smsRtspSession.h"
#include "smsRtspVideoRaw.h"
#include "sms.h"

//#define DEBUG_SMS_RTSP_VIDEO_RAW
#if defined(DEBUG_SMS_RTSP_VIDEO_RAW)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP-VRAW] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-VRAW-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

#define MAX_RTP_PACKET_SIZE	(1400)
#define MAX_FU_PAYLOAD_SIZE	(MAX_RTP_PACKET_SIZE-14)

static	unsigned int _convert_timestamp(Frame *frame, unsigned int resolution)
{
	unsigned int rval = 0;
	double	cal = 0.0;

	cal = 90000.0/(double)(frame->timeResolution);

	//FIXME.
	rval = frame->timestamp*cal;
	//DBGMSG("cal[%lf] frame->timestamp[%u] resol[%u] to [%u] rval[%u]", cal, frame->timestamp, frame->timeResolution, resolution, rval);
	
	return rval;
}

static inline unsigned char* putRawPayloadHeader(unsigned char *pBuf, Track *track, Frame *frame, RTPSession *pSession)
{
	unsigned char fieldNo=0;

	pBuf[0] = 0x00;
	pBuf[1] = 0x00;
	
	pBuf[2] = (track->mediaInfo.Raw.width * 2) >> 8;
	pBuf[3] = (track->mediaInfo.Raw.width * 2)&0xff;
	
	fieldNo = (frame->type == FRAME_TYPE_RAW_DEPTH)?0:1;
	pBuf[4] = (fieldNo?0x80:0x00)|pSession->seqNum; // line No == seqnum
	pBuf[5] = 0;
	
	pBuf[6] = 0;
	pBuf[7] = 0;

	return &pBuf[8];
}

int putVideoRawFrameFunc(Track *track, Frame *frame, int *tcpSendStatus)
{
	RTPSessionNodePtr	pNode = NULL;
	RTPSession			*pSession = NULL;
	int byteSent = 0, ACbyteSent = 0;
	char	sendbuf[MAX_RTP_PACKET_SIZE];
	int rval = 0;
	int tmpTcpStatus = 0;

	SK_ASSERT(track);
	SK_ASSERT(frame);

	/* skip uknown frames */
	if( frame->type == FRAME_TYPE_UNKNOWN ){
		return rval;
	}
	
	/* send frame to all destination */
	LOCK_MTX(&track->rtpSessionList.rtpSessionListMutex);
	do {
		pNode = NextRTPSessionNode(&track->rtpSessionList, pNode);
		if( pNode ){
			pSession = &pNode->session;

			if(pSession->state == RTP_STATE_WAIT_FOR_KEY_FRAME){
				/* If Key frame, set state to PLAY */				
				pSession->state = RTP_STATE_PLAY;
			}
		
			if( pSession->state == RTP_STATE_PLAY ){
				RTPPacketDesc		RtpDesc;
				unsigned int timestamp = 0;
				unsigned char *pRawHeader = NULL;
				unsigned char *pRawData = NULL;
				unsigned int rtpHeaderLen = 0, rtpRawHeaderLen=0, byteToSend=0;

				memset(sendbuf, 0x0, sizeof(char)*MAX_RTP_PACKET_SIZE);
				sendbuf[0] = 0x80; rtpHeaderLen++;// RTP version, extenstion, cc
				sendbuf[1] = 0x61; rtpHeaderLen++;// RTP marker, pt
				sendbuf[2] = (char)(pSession->seqNum>>8); rtpHeaderLen++;
				sendbuf[3] = (char)(pSession->seqNum&0x00FF); rtpHeaderLen++;
				timestamp = pSession->timestampBase + _convert_timestamp(frame, track->timeResolution);
				sendbuf[4] = (timestamp>>24)&0xFF; rtpHeaderLen++;
				sendbuf[5] = (timestamp>>16)&0xFF; rtpHeaderLen++;
				sendbuf[6] = (timestamp>>8)&0xFF; rtpHeaderLen++;
				sendbuf[7] = timestamp&0xFF; rtpHeaderLen++;
				sendbuf[8] = (pSession->ssrc>>24)&0xFF; rtpHeaderLen++;
				sendbuf[9] = (pSession->ssrc>>16)&0xFF; rtpHeaderLen++;
				sendbuf[10] = (pSession->ssrc>>8)&0xFF; rtpHeaderLen++;
				sendbuf[11] = pSession->ssrc&0xFF; rtpHeaderLen++;
				pRawHeader = &sendbuf[12];

				if(frame->size > MAX_FU_PAYLOAD_SIZE ){
					unsigned int sentBytes = 0, plsize=0;
					int i=0;
					unsigned char *pStart = NULL;

					pStart = &frame->pData[0];					
					for(i=0; sentBytes < frame->size; i++){

						sendbuf[2] = (char)(pSession->seqNum>>8);
						sendbuf[3] = (char)(pSession->seqNum&0x00FF);

						pRawData = putRawPayloadHeader(pRawHeader, track, frame, pSession);
						rtpRawHeaderLen = (pRawData - pRawHeader);						

						if( i == 0 ){
							plsize = track->mediaInfo.Raw.width * 2;
							memcpy(pRawData, pStart + sentBytes, plsize);
							sentBytes += plsize;
						}
						else{
							unsigned int remains = 0;

							remains = frame->size - sentBytes;
							if(remains == plsize){
								/* End of Frame*/
								sendbuf[1] |= 0x80; // RTP marker
								memcpy(pRawData, pStart + sentBytes, plsize);
								sentBytes += plsize;
							}
							else if(remains < plsize){
								ERRMSG("remains(%d) < plsize(%d)", remains, plsize);
								/* End of Frame*/
								sendbuf[1] |= 0x80; // RTP marker
								memcpy(pRawData, pStart + sentBytes, remains);
								sentBytes += remains;
								plsize = remains;
							}
							else{
								memcpy(pRawData, pStart + sentBytes, plsize);
								sentBytes += plsize;
							}
						}
						
						byteToSend = rtpRawHeaderLen + rtpHeaderLen + plsize; 
						if( MAX_RTP_PACKET_SIZE < byteToSend ){
							ERRMSG("MAX_RTP_PACKET_SIZE < byteToSend(%d)", byteToSend);
						}
						
						switch(pSession->mode){
							case RTP_UDP:
								byteSent = sendto(pSession->rtpSocket, (char*)sendbuf, byteToSend, 0, (struct sockaddr*)&pSession->rtpAddr, sizeof(struct sockaddr));
								if(byteSent != (int)byteToSend) {
									ERRMSG("sendto fail ret[%d] err[%d](%s)", byteSent, errno, strerror(errno));
								}
								else{
									ACbyteSent += byteSent;
									//DBGMSG("session[%s] ACbyteSent[%d] byteSent[%d]", pSession->strId, ACbyteSent, byteSent);
								}
								break;
							case RTP_RTSP_TCP:
								memset(&RtpDesc, 0x0, sizeof(RTPPacketDesc));
								RtpDesc.pStartAddr = sendbuf;
								RtpDesc.Len = byteToSend;
								RtpDesc.Marker = sendbuf[1];
								rval = pushTCPDataToChannel(pSession->rtspSession->ClientConn, &RtpDesc, pSession->rtpChannel, &tmpTcpStatus);
								if( rval == SMS_RET_OK ){
									byteSent = RtpDesc.Len;
									ACbyteSent += byteSent;
									//DBGMSG("session[%s] ACbyteSent[%d] byteSent[%d] ts[%u] seq[%u]", pSession->strId, ACbyteSent, byteSent, pSession->timestamp, pSession->seqNum);
									if( tcpSendStatus ){
										if( *tcpSendStatus < tmpTcpStatus ){
											*tcpSendStatus = tmpTcpStatus;
										}
									}
								}
								else{
									pSession->state = RTP_STATE_TO_BE_REMOVED;
									ERRMSG("data push fail! rval[%d]", rval);
								}
								break;
						}		
						if( RTP_STATE_TO_BE_REMOVED == pSession->state ){						
							break;
						}						
						pSession->seqNum++;
					}					
				}
			}
		}
	} while(pNode);
	UNLOCK_MTX(&track->rtpSessionList.rtpSessionListMutex);

	return ACbyteSent;
}


char* getVideoRawSDPFunc(Track *track)
{
	char *rSdp = NULL;

	SK_ASSERT(track);

	rSdp = track->sdp;

	return rSdp;
}

static const char sdpPrefixFmt[] =
    "m=%s %u RTP/AVP %d\r\n"
    "c=IN IP4 %s\r\n"
    "b=AS:%u\r\n"
    "a=rtpmap:%d %s/%d%s\r\n"
    "%s"
    "a=range:clock=%s-\r\n"
    "%s"
    "a=control:%s\r\n";
static const char sdpRtpPayloadFormatName[] = "raw";
static const char sdpMediaType[] = "video";
static const char sdpIpAddress[] = "192.168.10.102";
static const char sdpEncodingParamsPart[] = "";    
static const char sdpRtcpmuxLine[] = "";
static const char sdpRangeLine[] = "";
static const char sdpAuxSDPLineTemp[] = "a=fmtp:97 sampling=RGB+; width=%d; height=%d; depth=5\r\n";

int setVideoRawSDPFunc(Track *track, MediaInfo *pInfo)
{
	unsigned int sdpLength = 0;	
	char sdpRtpPayloadType = 97;
	int	sdpRtpTimestampFrequency = 90000;
	char sdpAuxSDPLine[COMMON_STR_LEN*2];
	char *pSpsBase64 = NULL;

	SK_ASSERT(track);

	DBGMSG("pInfo width(%d) height(%d)", pInfo->Raw.width, pInfo->Raw.height);

	if( memcmp(&track->mediaInfo, pInfo, sizeof(MediaInfo)) != 0 )
	{
		memset(sdpAuxSDPLine, 0x0, sizeof(char)*COMMON_STR_LEN*2);
		snprintf(sdpAuxSDPLine, COMMON_STR_LEN*2-1, sdpAuxSDPLineTemp, pInfo->Raw.width, pInfo->Raw.height);
		DBGMSG("sdpAuxSDPLine[%s]", sdpAuxSDPLine);

		sdpLength += strlen(sdpPrefixFmt);
		sdpLength += strlen(sdpMediaType) + 5 /* max short len */ + 3 /* max char len */;
		sdpLength += strlen(sdpIpAddress);
		sdpLength += 20; /* max int len */
		sdpLength += 3 /* max char len */ + strlen(sdpRtpPayloadFormatName) + 20 /* max int len */+ strlen(sdpEncodingParamsPart);
		sdpLength += strlen(sdpRtcpmuxLine);
		sdpLength += strlen(sdpRangeLine);
		sdpLength += strlen(sdpAuxSDPLine);
//		sdpLength += strlen("track000");
		sdpLength += strlen(track->name);
        sdpLength += 1024;

		if( track->sdp == NULL ){
			track->sdp = (char *)malloc(sdpLength);
		}

		if(track->sdp){
			memset(track->sdp, 0x0, sdpLength);
			snprintf(track->sdp, sdpLength-1, sdpPrefixFmt,
				sdpMediaType, // m= <media>
				0, // m= <port>
				sdpRtpPayloadType, // m= <fmt list>
				sdpIpAddress, // c= address
				0, // b=AS:<bandwidth>
				sdpRtpPayloadType, sdpRtpPayloadFormatName, sdpRtpTimestampFrequency, sdpEncodingParamsPart,// a=rtpmap:... (if present)
				sdpRtcpmuxLine, // a=rtcp-mux:... (if present)
				sdpRangeLine, // a=range:... (if present)
				sdpAuxSDPLine, // optional extra SDP line
				track->name); // a=control:<track name>
		}
		else{
			ERRMSG("malloc fail len[%d]", sdpLength);
		}

		memcpy(&track->mediaInfo, pInfo, sizeof(MediaInfo));
	}

	return 1;
}
