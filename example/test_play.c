/*
 * test_play.c
 *
 *  Created on: Nov 7, 2012
 *      Author: arash
 */

#define _XOPEN_SOURCE	600
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <signal.h>
#include <tlog/tlog.h>
#include <pthread.h>

#include "rcpdefs.h"
#include "rcpplus.h"
#include "rcpcommand.h"
#include "rtp.h"
#include "coder.h"
#include "preset.h"
#include "caps.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavfilter/avfilter.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#define VIDEO_MODE_H263	1
#define VIDEO_MODE_H264	2

void save_frame(AVFrame *frame, int width, int height)
{
	static int fnum = 0;
	FILE *f;
	char fname[32];
	int  y;

	// Open file
	sprintf(fname, "frame%02d.ppm", fnum++);
	f = fopen(fname, "wb");
	if(f == NULL)
		return;

	// Write header
	fprintf(f, "P6\n%d %d\n255\n", width, height);

	// Write pixel data
	for (y=0; y<height; y++)
		fwrite(frame->data[0] + y*frame->linesize[0], 1, width*3, f);

	// Close file
	fclose(f);
}

pthread_t thread;
void* keep_alive_thread(void* params)
{
	rcp_session* session = (rcp_session*)params;
	while (1)
	{
		int n = keep_alive(session);
		TL_INFO("active connections = %d", n);
		if (n < 0)
			break;

		sleep(2);
	}
	return NULL;
}

unsigned int highest_bit(unsigned int a)
{
	if (!a)
		return 0;

	unsigned int r = 1;
	while (a>>=1)
		r <<= 1;

	return r;
}

int main(int argc, char* argv[])
{
	tlog_init(TLOG_MODE_STDERR, TLOG_INFO, NULL);

	if (argc < 2)
	{
		TL_INFO("%s ip [line] [coder]\n", argv[0]);
		return 0;
	}

	int line = 1;
	if (argc >= 3)
		line = atoi(argv[2]);

	rcp_connect(argv[1]);

	start_message_manager();

	client_register(RCP_USER_LEVEL_SERVICE, "", RCP_REGISTRATION_TYPE_NORMAL, RCP_ENCRYPTION_MODE_PLAIN);

	rcp_session session;
	memset(&session, 0, sizeof(rcp_session));
	unsigned short udp_port = stream_connect_udp(&session);

	int coder=-1, coding, resolution;

	cap_list caps;
	get_capability_list(&caps);
	log_capabilities(TLOG_INFO, &caps);

	int video_section = 0;
	while (caps.sections[video_section].section_type != CAP_SECTION_TYPE_VIDEO)
		video_section++;

	for (int i=0; i<caps.sections[video_section].n_elems; i++)
	{
		cap_element* el = &caps.sections[video_section].elements[i];
		if (el->element_type == CAP_ELEMENT_VIDEO_ENCODER && el->input_number == line)
		{
			coder = el->identifier;
			coding = (el->compression & VIDEO_COMP_H264)?RCP_VIDEO_CODING_H264:RCP_VIDEO_CODING_H263;
			break;
		}
	}
	if (argc >= 4)
			coder = atoi(argv[3]);
	if (coder == -1)
	{
		TL_ERROR("no coder found for selected line");
		goto end1;
	}

	int preset_id = get_coder_preset(coder);

	rcp_mpeg4_preset preset;
	get_preset(preset_id, &preset, 1);
	log_preset(TLOG_INFO, &preset, 1);

	resolution = 0x1EF; // we accept all resolutions

	rcp_media_descriptor desc = {
			RCP_MEP_UDP, 1, 1, 0, udp_port, coder, line, coding, resolution
	};

	if (client_connect(&session, RCP_CONNECTION_METHOD_GET, RCP_MEDIA_TYPE_VIDEO, 0, &desc) != 0)
		return -1;

	//coder = desc.coder;
	//coding = desc.coding;

	int width, height;

	int mode = -1;
	get_coder_video_operation_mode(coder, &mode);
	TL_INFO("mode = %d coder = %d", mode, coder);

	int modes[2];
	const char* res_name;
    int format;
    get_video_input_format(1, &format);
	if (mode == RCP_CODER_MODE_H264 && get_h264_encoder_video_operation_mode(modes) == 0)
	{
		if (get_resolution_from_h264_operation_mode(modes[0], &width, &height, &res_name) != 0)
		{
			TL_INFO("%d", preset.resolution);
			get_resolution_from_preset(&preset, format, &width, &height, &res_name);
		}
	}
	else
	{
		resolution = highest_bit(desc.resolution);
		get_resolution_from_preset(&preset, format, &width, &height, &res_name);
	}

	TL_INFO("width=%d height=%d %s", width, height, res_name);

	avcodec_register_all();
	av_register_all();
	avfilter_register_all();

	AVCodec* codec_in;

	rtp_merge_desc mdesc;

	if (coding == RCP_VIDEO_CODING_H264)
	{
		rtp_init(RTP_PAYLOAD_TYPE_H264, 1, &mdesc);
		codec_in = avcodec_find_decoder(AV_CODEC_ID_H264);
	}
	else
	{
		rtp_init(RTP_PAYLOAD_TYPE_H263, 1, &mdesc);
		codec_in = avcodec_find_decoder(AV_CODEC_ID_H263);
	}

	if (codec_in == NULL)
	{
		TL_ERROR("cannot find decoder");
		return -1;
	}

	AVCodecContext *dec_ctx = avcodec_alloc_context3(codec_in);
	if (dec_ctx == NULL)
	{
		TL_ERROR("cannot allocate codec context");
		return -1;
	}

	if (avcodec_open2(dec_ctx, codec_in, NULL) < 0)
	{
		TL_ERROR("cannot open codec");
		return -1;
	}

	dec_ctx->width = width;
	dec_ctx->height = height;
	dec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

	AVFrame* raw_frame = avcodec_alloc_frame();
	if (raw_frame == NULL)
	{
		TL_ERROR("cannot allocate frame");
		return -1;
	}

	AVPacket in_pkt;
	av_init_packet(&in_pkt);


	//***********************************************

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
	{
		TL_ERROR("could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	SDL_Surface *screen;

	screen = SDL_SetVideoMode(width, height, 0, 0);
	if (!screen)
	{
		TL_ERROR("could not initialize video mode");
		return -1;
	}

	SDL_Overlay *bmp;
	bmp = SDL_CreateYUVOverlay(width, height, SDL_YV12_OVERLAY, screen);

	pthread_create(&thread, NULL, keep_alive_thread, &session);

	char sps_pps[5000];
	int sps_pps_len;
	request_sps_pps(&session, coder, sps_pps, &sps_pps_len);
	if (sps_pps_len)
	{
		unsigned char tmp[100];
		tmp[0] = 0;
		tmp[1] = 0;
		tmp[2] = 0;
		tmp[3] = 1;

		unsigned char sps[] = {0x67};
		unsigned char* pos = memmem(sps_pps, sps_pps_len, sps, 1);
		tlog_hex(TLOG_INFO, "sps", pos, 16);
		memcpy(tmp+4, pos, 16);
		in_pkt.data = tmp;
		in_pkt.size = 20;
		int have_frame = 0;
		avcodec_decode_video2(dec_ctx, raw_frame, &have_frame, &in_pkt);

		unsigned char pps[] = {0x68};
		pos = memmem(sps_pps, sps_pps_len, pps, 1);
		tlog_hex(TLOG_INFO, "pps", pos, 4);
		memcpy(tmp+4, pos, 4);
		in_pkt.data = tmp;
		in_pkt.size = 8;
		avcodec_decode_video2(dec_ctx, raw_frame, &have_frame, &in_pkt);
	}

	dec_ctx->extradata = (unsigned char*)sps_pps;
	dec_ctx->extradata_size = sps_pps_len;

	SDL_Rect rect;
	rect.x = 0;
	rect.y = 0;
	rect.w = dec_ctx->width;
	rect.h = dec_ctx->height;

	AVPicture pict;
	pict.data[0] = bmp->pixels[0];
	pict.data[1] = bmp->pixels[2];
	pict.data[2] = bmp->pixels[1];

	pict.linesize[0] = bmp->pitches[0];
	pict.linesize[1] = bmp->pitches[2];
	pict.linesize[2] = bmp->pitches[1];

	while (1)
	{
		if (rtp_recv(session.stream_socket, &mdesc) == 0)
        {
			if (rtp_pop_frame(&mdesc) == 0)
			{
				int have_frame = 0;
				in_pkt.data = (unsigned char*)mdesc.data;
				in_pkt.size = mdesc.frame_lenght;
				int ret = avcodec_decode_video2(dec_ctx, raw_frame, &have_frame, &in_pkt);
				if (ret && have_frame)
				{
					//TL_INFO("d0 %d d1 %d d2 %d d3 %d", raw_frame->linesize[0], raw_frame->linesize[1], raw_frame->linesize[2], raw_frame->linesize[3]);

					{
						SDL_LockYUVOverlay(bmp);

						av_picture_copy(&pict, (AVPicture*)raw_frame, AV_PIX_FMT_YUV420P, raw_frame->width, raw_frame->height);

						SDL_UnlockYUVOverlay(bmp);

						SDL_DisplayYUVOverlay(bmp, &rect);
					}


				}
				SDL_Event event;
				SDL_PollEvent(&event);
				if (event.type == SDL_QUIT)
				{
					SDL_Quit();
					goto end;
				}
			}
        }
	}

end:
	pthread_cancel(thread);
	client_disconnect(&session);
end1:
	client_unregister();
	stop_message_manager();

	return 0;
}
