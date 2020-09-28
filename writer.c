

#include "restream.h"
#include "guide.h"
#include "playlist.h"
#include "infile.h"
#include "reader.h"
#include "writer.h"


int writer_packet_flush(ctx_restream *restrm) {
    int retcd, indx;
    AVPacket enc_pkt;

    snprintf(restrm->function_name,1024,"%s","writer_packet_flush");
    restrm->watchdog_playlist = av_gettime_relative();

    //fprintf(stderr, "%s: Flushing encoder\n"
    //    ,restrm->guide_info->guide_displayname);

    for (indx=0; indx < restrm->stream_count; indx++ ){
        if (restrm->stream_ctx[indx].enc_ctx != NULL){
            retcd = avcodec_send_frame(restrm->stream_ctx[indx].enc_ctx, NULL);
            if (retcd < 0 ){
                return -1;
            }

            while (retcd != AVERROR_EOF){
                enc_pkt.data = NULL;
                enc_pkt.size = 0;
                av_init_packet(&enc_pkt);
                retcd = avcodec_receive_packet(restrm->stream_ctx[indx].enc_ctx, &enc_pkt);
                if (retcd != AVERROR_EOF){
                    if (retcd < 0){
                        av_packet_unref(&enc_pkt);
                        return -1;
                    }
                    enc_pkt.stream_index = indx;

                    retcd = av_interleaved_write_frame(restrm->ofmt_ctx, &enc_pkt);
                    if (retcd < 0) {
                        av_packet_unref(&enc_pkt);
                        return -1;
                    }

                }
                av_packet_unref(&enc_pkt);
            }
        }
    }

    return retcd;

}

int writer_init_video(ctx_restream *restrm, int indx){

    AVCodecContext  *enc_ctx, *dec_ctx;
    AVCodec         *encoder;
    AVStream        *out_stream;
    int retcd;

    snprintf(restrm->function_name,1024,"%s","writer_init_video");
    restrm->watchdog_playlist = av_gettime_relative();

    dec_ctx = restrm->stream_ctx[indx].dec_ctx;

    out_stream = avformat_new_stream(restrm->ofmt_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "%s: Failed allocating output stream\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder) {
        fprintf(stderr, "%s: Could not find video encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        fprintf(stderr, "%s: Could not allocate encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    (void)dec_ctx;
    /*
    retcd = avcodec_parameters_from_context(out_stream->codecpar, dec_ctx);
    if (retcd < 0) {
        fprintf(stderr, "%s: Could not copy parms from decoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    retcd = avcodec_parameters_to_context(enc_ctx, out_stream->codecpar);
    if (retcd < 0) {
        fprintf(stderr, "%s: Could not copy parms to encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }
    */

    enc_ctx->width = 640;
    enc_ctx->height = 360;
    enc_ctx->time_base = (AVRational){1, 90000};
    enc_ctx->profile = FF_PROFILE_H264_MAIN;
    enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    retcd = avcodec_open2(enc_ctx, encoder, NULL);
    if (retcd < 0) {
        fprintf(stderr, "%s: Could not open encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    if (restrm->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    out_stream->time_base = enc_ctx->time_base;

    retcd = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    if (retcd < 0) {
        fprintf(stderr, "%s: Could not copy parms from decoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    restrm->stream_ctx[indx].enc_ctx = enc_ctx;

    if (restrm->stream_ctx[indx].sws_ctx != NULL){
        sws_freeContext(restrm->stream_ctx[indx].sws_ctx);
    }

    restrm->stream_ctx[indx].sws_ctx = sws_getContext(
         restrm->stream_ctx[indx].dec_ctx->width
        ,restrm->stream_ctx[indx].dec_ctx->height
        ,restrm->stream_ctx[indx].dec_ctx->pix_fmt
        ,640 ,360, AV_PIX_FMT_YUV420P
        ,SWS_BICUBIC,NULL,NULL,NULL);
    if (restrm->stream_ctx[indx].sws_ctx == NULL) {
        return -1;
    }

    return 0;
}

int writer_init_audio(ctx_restream *restrm, int indx){

    AVStream *out_stream;
    AVCodecContext *dec_ctx, *enc_ctx;
    AVCodec *encoder;
    int retcd;

    snprintf(restrm->function_name,1024,"%s","writer_init_audio");
    restrm->watchdog_playlist = av_gettime_relative();

    out_stream = avformat_new_stream(restrm->ofmt_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "%s: Failed allocating output stream\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    dec_ctx = restrm->stream_ctx[indx].dec_ctx;

    encoder = avcodec_find_encoder(dec_ctx->codec_id);
    if (!encoder) {
        fprintf(stderr, "%s: Could not find video encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    enc_ctx = avcodec_alloc_context3(encoder);
    if (!enc_ctx) {
        fprintf(stderr, "%s: Could not allocate encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    enc_ctx->sample_rate = dec_ctx->sample_rate;
    enc_ctx->channel_layout = dec_ctx->channel_layout;
    enc_ctx->channels = dec_ctx->channels;
    enc_ctx->sample_fmt = encoder->sample_fmts[0];
    enc_ctx->time_base = (AVRational){1, 90000};

    /* Third parameter can be used to pass settings to encoder */
    retcd = avcodec_open2(enc_ctx, encoder, NULL);
    if (retcd < 0) {
        fprintf(stderr, "%s: Could not open encoder\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    retcd = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    if (retcd < 0) {
        fprintf(stderr, "%s: Failed to copy encoder parameters to stream\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    if (restrm->ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    out_stream->time_base = enc_ctx->time_base;
    restrm->stream_ctx[indx].enc_ctx = enc_ctx;

    return 0;
}

int writer_init_open(ctx_restream *restrm){

    int retcd;

    snprintf(restrm->function_name,1024,"%s","writer_init_open");
    restrm->watchdog_playlist = av_gettime_relative();

    retcd = avio_open(&restrm->ofmt_ctx->pb, restrm->out_filename, AVIO_FLAG_WRITE);
    if (retcd < 0) {
        fprintf(stderr, "%s: AVIO Open 1 Failed '%s'"
            ,restrm->guide_info->guide_displayname, restrm->out_filename);
        return -1;
    }

    retcd = avformat_write_header(restrm->ofmt_ctx, NULL);
    if (retcd < 0)  {
        if (restrm->ofmt_ctx){
            if (restrm->ofmt_ctx->pb != NULL){
                fprintf(stderr, "%s: close1 \n",restrm->guide_info->guide_displayname);
                avio_closep(&restrm->ofmt_ctx->pb);
            }
            avformat_free_context(restrm->ofmt_ctx);
        }
        restrm->ofmt_ctx = NULL;
        fprintf(stderr, "%s: Error occurred when opening output file\n"
            ,restrm->guide_info->guide_displayname);
        restrm->pipe_state = PIPE_NEEDS_RESET;
        return -1;
    }

    return 0;

}

int writer_init(ctx_restream *restrm){

    int retcd, indx;
    AVOutputFormat *out_fmt;

    snprintf(restrm->function_name,1024,"%s","writer_init");
    restrm->watchdog_playlist = av_gettime_relative();

    if (restrm->ifmt_ctx == NULL){
        fprintf(stderr, "%s: no input file provided\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    out_fmt = av_guess_format("mpegts", NULL, NULL);
    out_fmt->flags |= ~AVFMT_GLOBALHEADER;
    if (!out_fmt) {
        fprintf(stderr, "%s: av_guess_format failed\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    avformat_alloc_output_context2(&restrm->ofmt_ctx, out_fmt, NULL, restrm->out_filename);
    if (!restrm->ofmt_ctx) {
        fprintf(stderr, "%s: Could not create output context\n"
            ,restrm->guide_info->guide_displayname);
        return -1;
    }

    restrm->ofmt_ctx->interrupt_callback.callback = restrm_interrupt;
    restrm->ofmt_ctx->interrupt_callback.opaque = restrm;

    restrm->ofmt = restrm->ofmt_ctx->oformat;
    for (indx = 0; indx < restrm->ifmt_ctx->nb_streams; indx++) {
        if (restrm->stream_ctx[indx].dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO){
            retcd = writer_init_video(restrm, indx);
            if (retcd < 0) return -1;  /* Message already reported */

        } else if (restrm->stream_ctx[indx].dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO){
            retcd = writer_init_audio(restrm, indx);
            if (retcd < 0) return -1;  /* Message already reported */
        }
        snprintf(restrm->function_name,1024,"%s","writer_init");
    }

    av_dump_format(restrm->ofmt_ctx, 0, restrm->out_filename, 1);

    return 0;
}

void writer_close_encoder(ctx_restream *restrm){
    int indx;

    snprintf(restrm->function_name,1024,"%s","writer_close_encoder");
    restrm->watchdog_playlist = av_gettime_relative();

    indx = 0;
    while (indx < restrm->stream_count){
        if (restrm->stream_ctx[indx].enc_ctx != NULL){
            avcodec_free_context(&restrm->stream_ctx[indx].enc_ctx);
            restrm->stream_ctx[indx].enc_ctx = NULL;
        }
        if (restrm->stream_ctx[indx].sws_ctx != NULL){
            sws_freeContext(restrm->stream_ctx[indx].sws_ctx);
            restrm->stream_ctx[indx].sws_ctx = NULL;
        }
        indx++;
    }
    avformat_free_context(restrm->ofmt_ctx);
    restrm->ofmt_ctx = NULL;
    //fprintf(stderr, "%s: Output encoder closed \n"
    //        ,restrm->guide_info->guide_displayname);

}

void writer_close(ctx_restream *restrm){

    snprintf(restrm->function_name,1024,"%s","writer_close");
    restrm->watchdog_playlist = av_gettime_relative();

    reader_start(restrm);

    if (restrm->ofmt_ctx){
        if (restrm->ofmt_ctx->pb != NULL){
            writer_packet_flush(restrm);
            av_write_trailer(restrm->ofmt_ctx); /* Frees some memory*/
            avio_closep(&restrm->ofmt_ctx->pb);
        }
        writer_close_encoder(restrm);
    }
    restrm->ofmt_ctx = NULL;

    reader_close(restrm);

    restrm->pipe_state = PIPE_IS_CLOSED;
    //fprintf(stderr, "%s: Output closed\n"
    //    ,restrm->guide_info->guide_displayname);

}

void writer_rescale_frame(ctx_restream *restrm){

    restrm->frame_in->pts = av_rescale_q_rnd(restrm->frame_in->pkt_dts
        , restrm->ifmt_ctx->streams[restrm->stream_index]->time_base
        , restrm->ofmt_ctx->streams[restrm->stream_index]->time_base
        , AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    restrm->frame_in->pkt_dts = av_rescale_q_rnd(restrm->frame_in->pkt_dts
        , restrm->ifmt_ctx->streams[restrm->stream_index]->time_base
        , restrm->ofmt_ctx->streams[restrm->stream_index]->time_base
        , AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    restrm->frame_in->pkt_duration = av_rescale_q_rnd(restrm->frame_in->pkt_duration
        , restrm->ifmt_ctx->streams[restrm->stream_index]->time_base
        , restrm->ofmt_ctx->streams[restrm->stream_index]->time_base
        , AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);

}

int writer_packet_sendpkt(ctx_restream *restrm){

    int retcd, frame_size;
    uint8_t *buffer_out;

    snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt");
    restrm->watchdog_playlist = av_gettime_relative();

    writer_rescale_frame(restrm);

    if (restrm->stream_ctx[restrm->stream_index].dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO){

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 01");
        retcd = av_frame_copy_props(restrm->frame_out, restrm->frame_in);

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 02");
        frame_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 640, 360, 32);

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 03");
        buffer_out=(uint8_t *)malloc(frame_size * sizeof(uint8_t));

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 04");
        retcd = av_image_fill_arrays(
                restrm->frame_out->data
                ,restrm->frame_out->linesize
                ,buffer_out
                ,AV_PIX_FMT_YUV420P
                ,640
                ,360
                ,32
            );

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 05");
        retcd = sws_scale(
            restrm->stream_ctx[restrm->stream_index].sws_ctx
            ,(const uint8_t* const *)restrm->frame_in->data
            ,restrm->frame_in->linesize
            ,0
            ,restrm->stream_ctx[restrm->stream_index].dec_ctx->height
            ,restrm->frame_out->data
            ,restrm->frame_out->linesize);
        if (retcd < 0) {
            free(buffer_out);
            return -1;
        }

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 06");
        retcd = avcodec_send_frame(
            restrm->stream_ctx[restrm->stream_index].enc_ctx, restrm->frame_out);
        if (retcd < 0 ){
            free(buffer_out);
            return -1;
        }

        free(buffer_out);
    } else if (restrm->stream_ctx[restrm->stream_index].dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO){

        snprintf(restrm->function_name,1024,"%s","writer_packet_sendpkt 07");
        retcd = avcodec_send_frame(
            restrm->stream_ctx[restrm->stream_index].enc_ctx, restrm->frame_in);
        if (retcd < 0 ){
            return -1;
        }
    }

    return 0;

}

int writer_packet_encode(ctx_restream *restrm){

    int retcd;

    snprintf(restrm->function_name,1024,"%s","writer_packet_encode 01");
    restrm->watchdog_playlist = av_gettime_relative();

    retcd = writer_packet_sendpkt(restrm);
    if (retcd < 0) return -1;

    while (TRUE) {
        restrm->watchdog_playlist = av_gettime_relative();

        restrm->enc_pkt.data = NULL;
        restrm->enc_pkt.size = 0;
        av_init_packet(&restrm->enc_pkt);

        snprintf(restrm->function_name,1024,"%s","writer_packet_encode 02");
        retcd = avcodec_receive_packet(restrm->stream_ctx[restrm->stream_index].enc_ctx
            , &restrm->enc_pkt);
        if (retcd == AVERROR(EAGAIN)){
            /* Normal return.  The encoder is not ready to give us a packet yet*/
            av_packet_unref(&restrm->enc_pkt);
            return 0;
        }
        if (retcd < 0 ){
            av_packet_unref(&restrm->enc_pkt);
            return -1;
        }

        snprintf(restrm->function_name,1024,"%s","writer_packet_encode 03");
        if (restrm->enc_pkt.dts > restrm->enc_pkt.pts ){
            av_packet_unref(&restrm->enc_pkt);
            return 0;
        }

        restrm->enc_pkt.stream_index = restrm->stream_index;
        if (restrm->ofmt_ctx->streams[restrm->stream_index]->priv_pts == NULL) {
            retcd = -1;
        } else {
            retcd = av_interleaved_write_frame(restrm->ofmt_ctx, &restrm->enc_pkt);
        }

        if (retcd < 0 ){
            av_packet_unref(&restrm->enc_pkt);
            restrm->pipe_state = PIPE_NEEDS_RESET;
            return -1;
        }

        av_packet_unref(&restrm->enc_pkt);

    }
    return 0;
}

void writer_packet(ctx_restream *restrm){

    int retcd;

    if (finish) return;

    snprintf(restrm->function_name,1024,"%s","writer_packet");
    restrm->watchdog_playlist = av_gettime_relative();

    restrm->stream_index = restrm->pkt.stream_index;

    retcd = avcodec_send_packet(
        restrm->stream_ctx[restrm->stream_index].dec_ctx
        , &restrm->pkt);

    restrm->frame_in = av_frame_alloc();
    restrm->frame_out = av_frame_alloc();

    retcd = avcodec_receive_frame(
        restrm->stream_ctx[restrm->stream_index].dec_ctx
        , restrm->frame_in);
    if (retcd == AVERROR(EAGAIN) || retcd == AVERROR_EOF) {
        /* The frame is not ready to be consumed.  Return normal*/
        av_frame_free(&restrm->frame_in);
        av_frame_free(&restrm->frame_out);
        return;
    }
    if (retcd < 0){
        /* We have a real error */
        av_frame_free(&restrm->frame_in);
        av_frame_free(&restrm->frame_out);
        return;
    }

    retcd = writer_packet_encode(restrm);
    /* Any errors have already been reported */
    av_frame_free(&restrm->frame_in);
    av_frame_free(&restrm->frame_out);

    return;

}