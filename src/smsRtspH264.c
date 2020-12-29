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
#include "smsRtspH264.h"
#include "sms.h"

//#define DEBUG_SMS_RTSP_H264
#if defined(DEBUG_SMS_RTSP_H264)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS-RTSP-H264] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-RTSP-H264-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

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

int putH264FrameFunc(Track *track, Frame *frame, int *tcpSendStatus)
{
	RTPSessionNodePtr	pNode = NULL;
	RTPSession			*pSession = NULL;
	int byteSent = 0, ACbyteSent = 0;
	char	sendbuf[MAX_RTP_PACKET_SIZE];
	int rval = 0;
	int tmpTcpStatus = 0;

	SK_ASSERT(track);
	SK_ASSERT(frame);


#if 0 //[[ baek_0160303_BEGIN -- test code
	FILE	*fp = NULL;

	fp = fopen("/tmp/nfs/dump.h264", "a+");
	fwrite(frame->pData, 1, frame->size, fp);
	fclose(fp);	
#endif //]] baek_0160303_END -- test code

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
				static unsigned short prevSeqNum=0;
				
				if( frame->type == FRAME_TYPE_H264_IDR || frame->type == FRAME_TYPE_H264_P ){
				}
				else if( frame->type == FRAME_TYPE_UNKNOWN ){
					break;
				}

				memset(sendbuf, 0x0, sizeof(char)*MAX_RTP_PACKET_SIZE);
				sendbuf[0] = 0x80; // RTP version, extenstion, cc
				sendbuf[1] = 0x60; // RTP marker, pt
				sendbuf[2] = (char)(pSession->seqNum>>8);
				sendbuf[3] = (char)(pSession->seqNum&0x00FF);
				timestamp = pSession->timestampBase + _convert_timestamp(frame, track->timeResolution);
				sendbuf[4] = (timestamp>>24)&0xFF;
				sendbuf[5] = (timestamp>>16)&0xFF;
				sendbuf[6] = (timestamp>>8)&0xFF;
				sendbuf[7] = timestamp&0xFF;
				sendbuf[8] = (pSession->ssrc>>24)&0xFF;
				sendbuf[9] = (pSession->ssrc>>16)&0xFF;
				sendbuf[10] = (pSession->ssrc>>8)&0xFF;
				sendbuf[11] = pSession->ssrc&0xFF;

				DBGMSG("frame->size[%u] pts[%u]", frame->size, timestamp);

				if( frame->size <= MAX_FU_PAYLOAD_SIZE && frame->size > 6 ){
					unsigned char *pStart = NULL;					
					
					/* Jump start code */
					pStart = &frame->pData[5];

					/* Single nal */
					sendbuf[12] = 0x80|(frame->pData[4]&0x1f); //F, NRI, Type				
					memcpy(&sendbuf[13], pStart, frame->size-5);
					
					switch(pSession->mode){
						case RTP_UDP:
							if( pSession->seqNum - pSession->prevSeqNum != 1 ){
								ERRMSG("pSession->seqNum(%d) pSession->prevSeqNum(%d)", pSession->seqNum, pSession->prevSeqNum);
							}
							pSession->prevSeqNum = pSession->seqNum;									
							byteSent = sendto(pSession->rtpSocket, (char*)sendbuf, frame->size-5+13, 0, (struct sockaddr*)&pSession->rtpAddr, sizeof(struct sockaddr));
							if(byteSent != (int)frame->size-5+13) {
								ERRMSG("sendto fail ret[%d] err[%d](%s)", byteSent, errno, strerror(errno));
							}
							else{
								ACbyteSent += byteSent;
								//DBGMSG("session[%s] ACbyteSent[%d] byteSent[%d] ts[%u] seq[%u]", pSession->strId, ACbyteSent, byteSent, pSession->timestamp, pSession->seqNum);
							}
							break;
						case RTP_RTSP_TCP:
							memset(&RtpDesc, 0x0, sizeof(RTPPacketDesc));
							RtpDesc.pStartAddr = sendbuf;
							RtpDesc.Len = frame->size-5+13;
							RtpDesc.Marker = sendbuf[1];
							rval = pushTCPDataToChannel(pSession->rtspSession->ClientConn, &RtpDesc, pSession->rtpChannel, &tmpTcpStatus);
							if( rval == SMS_RET_OK ){
								byteSent = RtpDesc.Len;
								ACbyteSent += byteSent;
//								DBGMSG("session[%s] ACbyteSent[%d] byteSent[%d] ts[%u] seq[%u]", pSession->strId, ACbyteSent, byteSent, pSession->timestampLast, pSession->seqNum);usleep(500000);
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
				else if(frame->size > MAX_FU_PAYLOAD_SIZE && frame->size > 6 ){
					unsigned int sentBytes = 0, plsize=0;
					int i=0;
					unsigned char *pStart = NULL;
					
					/* Jump start code */
					pStart = &frame->pData[5];
					
					/* FU nal */
					if( frame->type == FRAME_TYPE_H264_IDR){
						sendbuf[12] = 0x7C; //F, NRI, Type						
					}
					else{
						sendbuf[12] = 0x5C; //F, NRI, Type
					}

					for(i=0; sentBytes < frame->size-5; i++){
						sendbuf[2] = (char)(pSession->seqNum>>8);
						sendbuf[3] = (char)(pSession->seqNum&0x00FF);
						if( i == 0 ){
							/* Start of FUs*/
							sendbuf[13] = 0x80|(frame->pData[4]&0x1f); //S, E, R, Type
							memcpy(&sendbuf[14], pStart+sentBytes, MAX_FU_PAYLOAD_SIZE);
							sentBytes += MAX_FU_PAYLOAD_SIZE;
							plsize = MAX_FU_PAYLOAD_SIZE;							
						}
						else{
							unsigned int remains = 0;

							remains = frame->size-sentBytes-5;
							if(remains < MAX_FU_PAYLOAD_SIZE){
								/* End of FUs*/
								sendbuf[1] |= 0x80; // RTP marker
								sendbuf[13] = 0x40|(frame->pData[4]&0x1f); //S, E, R, Type
								memcpy(&sendbuf[14], pStart+sentBytes, remains);
								sentBytes += remains;
								plsize = remains;
							}
							else{
								if(remains == MAX_FU_PAYLOAD_SIZE){
									sendbuf[13] = 0x40|(frame->pData[4]&0x1f); //S, E, R, Type
								}
								else{
									sendbuf[13] = 0x00|(frame->pData[4]&0x1f); //S, E, R, Type	
								}								
								memcpy(&sendbuf[14], pStart+sentBytes, MAX_FU_PAYLOAD_SIZE);
								sentBytes += MAX_FU_PAYLOAD_SIZE;								
								plsize = MAX_FU_PAYLOAD_SIZE;
							}
						}
						switch(pSession->mode){
							case RTP_UDP:
								if( pSession->seqNum - pSession->prevSeqNum != 1 ){
									ERRMSG("pSession->seqNum(%d) pSession->prevSeqNum(%d)", pSession->seqNum, pSession->prevSeqNum);
								}
								pSession->prevSeqNum = pSession->seqNum;
								
								byteSent = sendto(pSession->rtpSocket, (char*)sendbuf, plsize+14, 0, (struct sockaddr*)&pSession->rtpAddr, sizeof(struct sockaddr));
								if(byteSent != (int)plsize+14) {
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
								RtpDesc.Len = plsize+14;
								RtpDesc.Marker = sendbuf[1];
								rval = pushTCPDataToChannel(pSession->rtspSession->ClientConn, &RtpDesc, pSession->rtpChannel, &tmpTcpStatus);
								if( rval == SMS_RET_OK ){
									byteSent = RtpDesc.Len;
									ACbyteSent += byteSent;
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

				/* find start code */
			}
		}
	} while(pNode);
	UNLOCK_MTX(&track->rtpSessionList.rtpSessionListMutex);

	return ACbyteSent;
}


char* getH264SDPFunc(Track *track)
{
	char *rSdp = NULL;

	SK_ASSERT(track);

	rSdp = track->sdp;

	return rSdp;
}

static const char b64_table[] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/'
};


static char * b64_encode(const unsigned char *src, size_t len) {
  int i = 0;
  int j = 0;
  char *enc = NULL;
  size_t size = 0;
  unsigned char buf[4];
  unsigned char tmp[3];

  // alloc
  enc = (char *) malloc(1);
  if (NULL == enc) { return NULL; }

  // parse until end of source
  while (len--) {
    // read up to 3 bytes at a time into `tmp'
    tmp[i++] = *(src++);

    // if 3 bytes read then encode into `buf'
    if (3 == i) {
      buf[0] = (tmp[0] & 0xfc) >> 2;
      buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
      buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
      buf[3] = tmp[2] & 0x3f;

      // allocate 4 new byts for `enc` and
      // then translate each encoded buffer
      // part by index from the base 64 index table
      // into `enc' unsigned char array
      enc = (char *) realloc(enc, size + 4);
      for (i = 0; i < 4; ++i) {
        enc[size++] = b64_table[buf[i]];
      }

      // reset index
      i = 0;
    }
  }

  // remainder
  if (i > 0) {
    // fill `tmp' with `\0' at most 3 times
    for (j = i; j < 3; ++j) {
      tmp[j] = '\0';
    }

    // perform same codec as above
    buf[0] = (tmp[0] & 0xfc) >> 2;
    buf[1] = ((tmp[0] & 0x03) << 4) + ((tmp[1] & 0xf0) >> 4);
    buf[2] = ((tmp[1] & 0x0f) << 2) + ((tmp[2] & 0xc0) >> 6);
    buf[3] = tmp[2] & 0x3f;

    // perform same write to `enc` with new allocation
    for (j = 0; (j < i + 1); ++j) {
      enc = (char *) realloc(enc, size + 1);
      enc[size++] = b64_table[buf[j]];
    }

    // while there is still a remainder
    // append `=' to `enc'
    while ((i++ < 3)) {
      enc = (char *) realloc(enc, size + 1);
      enc[size++] = '=';
    }
  }

  // Make sure we have enough space to add '\0' character at end.
  enc = (char *) realloc(enc, size + 1);
  enc[size] = '\0';

  return enc;
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
static const char sdpRtpPayloadFormatName[] = "H264";
static const char sdpMediaType[] = "video";
static const char sdpIpAddress[] = "192.168.10.102";
static const char sdpEncodingParamsPart[] = "";    
static const char sdpRtcpmuxLine[] = "";
static const char sdpRangeLine[] = "";
static const char sdpAuxSDPLineTemp[] = "a=fmtp:96 packetization-mode=1; profile-level-id=4D002A; sprop-parameter-sets=%s,KO48gA==\r\n";

int setH264SDPFunc(Track *track, MediaInfo *pInfo)
{
	unsigned int sdpLength = 0;	
	char sdpRtpPayloadType = 96;
	int	sdpRtpTimestampFrequency = 90000;
	char sdpAuxSDPLine[COMMON_STR_LEN*2];
	char *pSpsBase64 = NULL;

	SK_ASSERT(track);
	if( memcmp(&track->mediaInfo, pInfo, sizeof(MediaInfo)) != 0 )
	{
		pSpsBase64 = b64_encode(pInfo->H264.sps, pInfo->H264.spsLen);
		if( pSpsBase64 )
		{
			memset(sdpAuxSDPLine, 0x0, sizeof(char)*COMMON_STR_LEN*2);
			DBGMSG("pSpsBase64[%s] len(%zu)", pSpsBase64, strlen(pSpsBase64));
			snprintf(sdpAuxSDPLine, COMMON_STR_LEN*2-1, sdpAuxSDPLineTemp, pSpsBase64);
			DBGMSG("sdpAuxSDPLine[%s]", sdpAuxSDPLine);
			free(pSpsBase64);
		}
		else
		{
			ERRMSG("b64_encode failed");
			return 0;
		}

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
