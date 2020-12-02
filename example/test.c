/*******select.c*********/
/*******Using select() for I / O multiplexing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "smsDefine.h"
#include "smsRtsp.h"
#include "smsRtspStream.h"
#include "sms.h"


//#define DEBUG_SMS
#if defined(DEBUG_SMS)
#undef DBGMSG
#define DBGMSG(msg, args...) printf("[SMS] %s(%d) : "msg"\n", __FUNCTION__, __LINE__, ## args)
#else
#undef DBGMSG
#define DBGMSG(...)
#endif
#define ERRMSG(msg, args...) printf("%c[1;31m",27); printf("%c[1;33m[SMS-ERR] %s(%d) : "msg"%c[0m\n", 27, __FUNCTION__, __LINE__, ## args, 27)

pthread_t	gRtspThreadId;
int			gPort = 0;

//test
pthread_t	gTestThreadId;
pthread_t	gTestThreadId2;

static int gVid = 0;
static int gAid = 0;

//test

#if 0 //[[ baek_0160303_BEGIN -- del
						/* send frame */
						if( isFrameReady ){
							if( i != 0 ){
								/* Add data to frame buffer */
								addDataToFrame(&frame, buf, i);								
								smsPutFrame(gVideoTrackId, &frame);	
							}
						}
						else{
							if( copyLen != 0 ){
								
							}
							copyLen = i;
							addDataToFrame(&frame, buf[start], copyLen);
						}


								frame.size						
#endif //]] baek_0160303_END -- del

#define	MAX_FRAME_SIZE	(1024*256)

unsigned int gTotalSize = 0;
void sendFrame(Frame *frame, int id)
{
	int i=0;

	DBGMSG("type[%d] timestamp[%u] size[%u]", frame->type, frame->timestamp, frame->size);
#if 0 //[[ baek_0160303_BEGIN -- del
	for(i=0; i<frame->size; i++){
		if( i%8 == 0 ){
			printf(" ");
			if( i%16 == 0 ){
				printf("\r\n");
			}
		}
		printf("[%02X]", frame->pData[i]);
	}
	printf("\r\n");
#endif //]] baek_0160303_END -- del

	gTotalSize += frame->size;

	if( frame->type == FRAME_TYPE_H264_IDR || frame->type == FRAME_TYPE_H264_P ){
		smsPutFrame(id, frame);
		frame->timestamp += frame->timeResolution/30;
		usleep(33333);
	}	
	else if( frame->type == FRAME_TYPE_AUDIO_AAC ){
		smsPutFrame(id, frame);
		frame->timestamp += 1024;
		usleep(22500);
	}
	else{
		smsPutFrame(id, frame);
	}
	
}

void appendDataToFrame(Frame *frame, unsigned char *buf, unsigned int len)
{
	SK_ASSERT(frame);
	SK_ASSERT(frame->pData);

	memcpy(frame->pData+frame->size, buf, len);
	frame->size += len;		
}

void destoryFrameBuffer(Frame *frame)
{
	SK_ASSERT(frame);

	if( frame->pData ){
		free(frame->pData);
		frame->pData = NULL;
	}
	frame->	size = 0;	
}

void newH264FrameBuffer(Frame *frame, unsigned char *buf, unsigned int len)
{
	SK_ASSERT(frame);

	frame->pData = (unsigned char *)malloc(sizeof(char)*MAX_FRAME_SIZE);
	if( frame->pData ){
		memset(frame->pData, 0x0, sizeof(char)*MAX_FRAME_SIZE);
		memcpy(frame->pData, buf, len);
		frame->size = len;
		
		/* check nal type */
		if( (buf[4]&0x1f) == 0x01 ){
			/* P-Frame */
			frame->type = FRAME_TYPE_H264_P;
		}
		else if( (buf[4]&0x1f) == 0x05 ){
			/* I-Frame */
			frame->type = FRAME_TYPE_H264_IDR;
		}
		else if( (buf[4]&0x1f) == 0x08 ){
			/* PPS */
			frame->type = FRAME_TYPE_H264_PPS;									
		}
		else if( (buf[4]&0x1f) == 0x07 ){
			/* SPS */
			frame->type = FRAME_TYPE_H264_SPS;
		}		
	}
}


void testH264FileParserFunc(void *data)
{
	Frame	frame;
	unsigned char 	buf[1024];
	FILE	*fp = NULL;
	unsigned int readBytes=0, remainSize=0;
	int isFrameReady = 0;

	sleep(5);

	/* Init Frame */
	memset(&frame, 0x0, sizeof(Frame));
	frame.timestamp = 0;
	frame.timeResolution = 30000;

	fp = fopen("sample/live_stream_dump.264", "rb");
	if(fp){
		memset(buf, 0x0, sizeof(char)*1024);
		do {
			readBytes = fread(buf+remainSize, 1, 1024-remainSize, fp);
			if( readBytes > 0 ){
				int i=0, start=0, used=0;

#if 1 //[[ baek_0160303_BEGIN -- del
				for(i=0; i<remainSize+readBytes-3; i++){
					if( buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1 ){
						if( isFrameReady ){
							/* Frame data is already exist */
							
							/* append "start~i" data to frame buf */
							appendDataToFrame(&frame, &buf[start], i-start);
							used += i-start;

							/* send frame */
							sendFrame(&frame, gVid);

							/* destory frame buffer */
							destoryFrameBuffer(&frame);
							isFrameReady = 0;							
						}
						
						if( i < remainSize+readBytes-4 ){
							newH264FrameBuffer(&frame, &buf[i], 5);
							used += 5;
							start = i + 5;
							i = start;
							isFrameReady = 1;
						}
					}
				}				
				
				if( used == 0 ){					
					if( isFrameReady ){
						appendDataToFrame(&frame, &buf[start], i-start);
						used += i-start;
					}
				}
				else{
					if( isFrameReady && i > used && i < remainSize+readBytes){
						appendDataToFrame(&frame, &buf[used], i-used);
						used += i-used;
					}					
				}
				remainSize = remainSize + readBytes - used;
				DBGMSG("remainSize[%u] readBytes[%u] used[%u]", remainSize, readBytes, used);
#else
				DBGMSG("readBytes[%u]", readBytes);
				for(i=0; i<remainSize+readBytes-4; i++){
					if( i%8 == 0 ){
						printf(" ");
						if( i%16 == 0 ){
							printf("\r\n");
						}
					}
					printf("[%02X]", buf[i]);
				}
				printf("\r\n");
#endif //]] baek_0160303_END -- del
				memmove(buf, buf+i, remainSize);				
			}
			else if( readBytes == 0 ){
				if( isFrameReady ){

					appendDataToFrame(&frame, buf, remainSize);
					
					/* send frame */
					sendFrame(&frame, gVid);							
					
					/* destory frame buffer */
					destoryFrameBuffer(&frame);
					isFrameReady = 0;

					fseek(fp, 0, SEEK_SET);
				}
				ERRMSG("fread done total[%u]", gTotalSize);				
			}
			else{
				ERRMSG("fread fail");
			}			
		} while( readBytes != -1 );
		fclose(fp);
	}
	else{
		ERRMSG("fopen fail");
	}

	while(1){
//		smsPutFrame(gVideoTrackId, &frame);
		sleep(1);
	}
	
}

void newAACFrameBuffer(Frame *frame, unsigned char *buf, unsigned int len)
{
	SK_ASSERT(frame);

	frame->pData = (unsigned char *)malloc(sizeof(char)*MAX_FRAME_SIZE);
	if( frame->pData ){
		memset(frame->pData, 0x0, sizeof(char)*MAX_FRAME_SIZE);
		memcpy(frame->pData, buf, len);
		frame->size = len;
		frame->type = FRAME_TYPE_AUDIO_AAC;
	}
}

void testAACFileParserFunc(void *data)
{
	Frame	frame;
	unsigned char 	buf[1024];
	FILE	*fp = NULL;
	unsigned int readBytes=0, remainSize=0;
	int isFrameReady = 0;

	sleep(5);

	/* Init Frame */
	memset(&frame, 0x0, sizeof(Frame));
	frame.timestamp = 0;
	frame.timeResolution = 44100;

	fp = fopen("sample/test.aac", "rb");
	if(fp){
		memset(buf, 0x0, sizeof(char)*1024);
		do {
			readBytes = fread(buf+remainSize, 1, 1024-remainSize, fp);
			if( readBytes > 0 ){
				int i=0, start=0, used=0;

#if 1 //[[ baek_0160303_BEGIN -- del
				for(i=0; i<remainSize+readBytes-1; i++){
					if( buf[i] == 0xFF && buf[i+1] == 0xF1 ){
						if( isFrameReady ){
							/* Frame data is already exist */
							
							/* append "start~i" data to frame buf */
							appendDataToFrame(&frame, &buf[start], i-start);
							used += i-start;

							/* send frame */
							sendFrame(&frame, gAid);							

							/* destory frame buffer */
							destoryFrameBuffer(&frame);
							isFrameReady = 0;							
						}
						
						if( i < remainSize+readBytes-2 ){
							newAACFrameBuffer(&frame, &buf[i], 3);
							used += 3;
							start = i + 3;
							i = start;
							isFrameReady = 1;
						}
					}
				}				
				
				if( used == 0 ){					
					if( isFrameReady ){
						appendDataToFrame(&frame, &buf[start], i-start);
						used += i-start;
					}
				}
				else{
					if( isFrameReady && i > used && i < remainSize+readBytes){
						appendDataToFrame(&frame, &buf[used], i-used);
						used += i-used;
					}					
				}
				remainSize = remainSize + readBytes - used;
				DBGMSG("remainSize[%u] readBytes[%u] used[%u]", remainSize, readBytes, used);
#else
				DBGMSG("readBytes[%u]", readBytes);
				for(i=0; i<remainSize+readBytes-4; i++){
					if( i%8 == 0 ){
						printf(" ");
						if( i%16 == 0 ){
							printf("\r\n");
						}
					}
					printf("[%02X]", buf[i]);
				}
				printf("\r\n");
#endif //]] baek_0160303_END -- del
				memmove(buf, buf+i, remainSize);				
			}
			else if( readBytes == 0 ){
				if( isFrameReady ){

					appendDataToFrame(&frame, buf, remainSize);
					
					/* send frame */
					sendFrame(&frame, gAid);							
					
					/* destory frame buffer */
					destoryFrameBuffer(&frame);
					isFrameReady = 0;

					fseek(fp, 0, SEEK_SET);
				}
				ERRMSG("fread done total[%u]", gTotalSize);				
			}
			else{
				ERRMSG("fread fail");
			}			
		} while( readBytes != -1 );
		fclose(fp);
	}
	else{
		ERRMSG("fopen fail");
	}

	while(1){
		sleep(1);
	}
	
}

void testFunc(void)
{
	Frame	dummyframe;
	char 	tmpstr[128] = "This is test messageGGGGGGGGGGGGGGGGGGG";

	memset(&dummyframe, 0x0, sizeof(Frame));

	dummyframe.type = 0;
	dummyframe.size = strlen(tmpstr);
	dummyframe.pData = (unsigned char *)malloc(sizeof(char)*dummyframe.size);
	memcpy(dummyframe.pData, tmpstr, sizeof(char)*dummyframe.size);
	dummyframe.timeResolution = 30000;

	while(1){
		smsPutFrame(gVideoTrackId, &dummyframe);	
		dummyframe.timestamp += 30000;		
		sleep(1);
	}
}

int startVideoStreaming(int videoTrackId)
{
	int rval = 0;
	int id = 0;

	id = videoTrackId;

	if( pthread_create(&gTestThreadId, NULL, (void *)testH264FileParserFunc, (void *)&id) != 0){
		ERRMSG("%s pthread_create error", __FUNCTION__);
	}		
	pthread_detach(gTestThreadId);

	return rval;
}


int startAudioStreaming(int audioTrackId)
{
	int rval = 0;
	int id = 0;

	id = audioTrackId;

	if( pthread_create(&gTestThreadId2, NULL, (void *)testAACFileParserFunc, (void *)&id) != 0){
		ERRMSG("%s pthread_create error", __FUNCTION__);
	}		
	pthread_detach(gTestThreadId2);	

	return rval;
}


int main(int argc, char *argv[])
{
	int serverId = 0, streamId = 0, videoTrackId = 0, audioTrackId = 0;
	
	serverId = smsRTSPServerInit(5555, "live");
	if( serverId == -1 ){
		ERRMSG("serverId == -1");
	}

	streamId = smsAddRTSPStream(serverId, "live/primary");
	if( streamId == -1 ){
		ERRMSG("streamId == -1");
	}

	videoTrackId = smsAddRTSPTrack(streamId, "track201", TRK_TYPE_H264);
	if( videoTrackId == -1 ){
		ERRMSG("videoTrackId == -1");
	}

	audioTrackId = smsAddRTSPTrack(streamId, "track202", TRK_TYPE_AAC);
	if( audioTrackId == -1 ){
		ERRMSG("audioTrackId == -1");
	}

	smsStartRTSPServer(serverId);

	gVid = videoTrackId;
	gAid = audioTrackId;

	startVideoStreaming(gVid);
	startAudioStreaming(gAid);	

	while(1){
		sleep(1);
	}

	return 0;
}

