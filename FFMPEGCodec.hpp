
#pragma once
#include <queue>
#include <tuple>
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
namespace FFMPEGTools
{
    struct AVData
    {
        int width;
        int height;
        unsigned int size;
        unsigned char *data;
        //
        AVData() : width(0), height(0), size(0), data(nullptr){};
        AVData(int width, int height, unsigned int size)
        {
            this->width = width;
            this->height = height;
            this->size = size;
            this->data = new unsigned char[this->size];
        };
        AVData &operator=(const AVData &DataCpy)
        {
            width = DataCpy.width;
            height = DataCpy.height;
            size = DataCpy.size;
            data = new unsigned char[size];
            std::copy(DataCpy.data, DataCpy.data + size, this->data);
            return *this;
        };
        AVData(const AVData &DataCpy)
        {
            width = DataCpy.width;
            height = DataCpy.height;
            size = DataCpy.size;
            data = new unsigned char[size];
            std::copy(DataCpy.data, DataCpy.data + size, this->data);
        };
        ~AVData()
        {
            if (data != nullptr)
                delete[] data;
        };
    };

    struct FFMPEGOption
    {
        int IOWidth;
        int IOHeight;
        int OBuffer;
        int OFrameRate;
        int OBitRate;
        const char *CodecProfile;

        AVCodecID OutputFormat;
        AVPixelFormat TargetFormat;
    };

    class FFMPEGCodec
    {
    public:
        FFMPEGCodec(FFMPEGOption);
        void pushFrame(unsigned char *imagedata, int imagesize, int linesize);
        int getFrame(std::queue<AVData> &dataQueue);
        ~FFMPEGCodec();

    private:
        FFMPEGOption _FFMPEOption;
        struct AVCodecDep
        {
            AVPacket Packet;
            AVFrame *frameInput;
            AVCodec *EncoderBase;
            AVCodecContext *Encoder;
            SwsContext *DataConvertor;
        } AVCD;
    };

    class FFMPEGDecodec
    {
    public:
        FFMPEGDecodec();
        int FFMPEGDecodecInsert(uint8_t *data, int size);
        AVData FFMPEGDecodecGetFrame();

    private:
        struct AVCodecDep
        {
            AVPacket Packet;
            AVPacket PacketOut;
            AVFrame *frameOutput;
            AVCodec *DecoderBase;
            AVCodecContext *Decoder;
            AVCodecParserContext *DecodeParser;
            SwsContext *DataConvertor;
        } AVCD;
    };
}

FFMPEGTools::FFMPEGCodec::FFMPEGCodec(FFMPEGOption optionI)
{
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
#endif

    _FFMPEOption = optionI;
    AVCD.EncoderBase = avcodec_find_encoder(_FFMPEOption.OutputFormat);
    AVCD.Encoder = avcodec_alloc_context3(AVCD.EncoderBase);
    AVCD.Encoder->codec_tag = 0;
    AVCD.Encoder->codec_id = _FFMPEOption.OutputFormat;
    AVCD.Encoder->codec_type = AVMEDIA_TYPE_VIDEO;
    AVCD.Encoder->width = _FFMPEOption.IOWidth;
    AVCD.Encoder->height = _FFMPEOption.IOHeight;
    AVCD.Encoder->gop_size = _FFMPEOption.OBuffer;
    AVCD.Encoder->pix_fmt = AV_PIX_FMT_YUV420P;
    AVCD.Encoder->framerate = {_FFMPEOption.OFrameRate, 1};
    AVCD.Encoder->time_base = av_inv_q({_FFMPEOption.OFrameRate, 1});
    AVCD.Encoder->bit_rate = _FFMPEOption.OBitRate;
    AVCD.Encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary *TmpCodecOptions = nullptr;
    av_dict_set(&TmpCodecOptions, "profile", _FFMPEOption.CodecProfile, 0);
    av_dict_set(&TmpCodecOptions, "preset", "ultrafast", 0);
    av_dict_set(&TmpCodecOptions, "tune", "zerolatency", 0);
    avcodec_open2(AVCD.Encoder, AVCD.EncoderBase, &TmpCodecOptions);

    if (_FFMPEOption.TargetFormat != AV_PIX_FMT_YUV420P)
        AVCD.DataConvertor = sws_getContext(_FFMPEOption.IOWidth, _FFMPEOption.IOHeight,
                                            _FFMPEOption.TargetFormat,
                                            _FFMPEOption.IOWidth, _FFMPEOption.IOHeight,
                                            AV_PIX_FMT_YUV420P,
                                            SWS_FAST_BILINEAR, NULL, NULL, NULL);

    av_init_packet(&AVCD.Packet);
    AVCD.frameInput = av_frame_alloc();
    av_image_alloc(AVCD.frameInput->data, AVCD.frameInput->linesize, _FFMPEOption.IOWidth,
                   _FFMPEOption.IOHeight, AV_PIX_FMT_YUV420P, 1);
    AVCD.frameInput->width = _FFMPEOption.IOWidth;
    AVCD.frameInput->height = _FFMPEOption.IOHeight;
    AVCD.frameInput->format = static_cast<int>(AVCD.Encoder->pix_fmt);
}

void FFMPEGTools::FFMPEGCodec::pushFrame(unsigned char *imagedata, int imagesize, int linesize)
{
    if (_FFMPEOption.TargetFormat != AV_PIX_FMT_YUV420P)
        sws_scale(AVCD.DataConvertor, &imagedata, &linesize, 0,
                  _FFMPEOption.IOHeight, AVCD.frameInput->data, AVCD.frameInput->linesize);
    else
    {
        int frame_size = _FFMPEOption.IOWidth * _FFMPEOption.IOHeight;
        AVCD.frameInput->data[0] = imagedata;
        AVCD.frameInput->data[1] = imagedata + frame_size;
        AVCD.frameInput->data[2] = imagedata + frame_size * 5 / 4;
    }

    avcodec_send_frame(AVCD.Encoder, AVCD.frameInput);
}

int FFMPEGTools::FFMPEGCodec::getFrame(std::queue<AVData> &dataQueue)
{
    int codec_signal = 0;
    while (codec_signal != AVERROR(EAGAIN) && codec_signal != AVERROR_EOF)
    {
        codec_signal = avcodec_receive_packet(AVCD.Encoder, &AVCD.Packet);
        //
        if (codec_signal != AVERROR(EAGAIN) && codec_signal != AVERROR_EOF)
        {
            if ((AVCD.Packet.data[4] & 0x1f) == 5)
            {
                AVData dataSPS(_FFMPEOption.IOWidth, _FFMPEOption.IOHeight, AVCD.Encoder->extradata_size);
                //
                std::copy(AVCD.Encoder->extradata, AVCD.Encoder->extradata + AVCD.Encoder->extradata_size, dataSPS.data);
                //
                dataQueue.push(dataSPS);
            }

            AVData data(_FFMPEOption.IOWidth, _FFMPEOption.IOHeight, AVCD.Packet.size);
            //
            std::copy(AVCD.Packet.data, AVCD.Packet.data + AVCD.Packet.size, data.data);
            //
            dataQueue.push(data);
        }
    }
    return codec_signal;
};

FFMPEGTools::FFMPEGCodec::~FFMPEGCodec()
{
    avcodec_close(AVCD.Encoder);
    if (_FFMPEOption.TargetFormat != AV_PIX_FMT_YUV420P)
        sws_freeContext(AVCD.DataConvertor);
    av_packet_unref(&AVCD.Packet);
    av_frame_free(&AVCD.frameInput);
}

FFMPEGTools::FFMPEGDecodec::FFMPEGDecodec()
{
    av_log_set_level(AV_LOG_SKIP_REPEATED);
    AVCD.DecoderBase = avcodec_find_decoder(AV_CODEC_ID_H264);
    AVCD.Decoder = avcodec_alloc_context3(AVCD.DecoderBase);
    AVCD.DecodeParser = av_parser_init(AV_CODEC_ID_H264);
    if (AVCD.DecoderBase->capabilities & AV_CODEC_CAP_TRUNCATED)
        AVCD.DecodeParser->flags |= AV_CODEC_FLAG_TRUNCATED;
    AVCD.DecodeParser->flags |= AV_CODEC_FLAG_INTERLACED_DCT;
    //
    avcodec_open2(AVCD.Decoder, AVCD.DecoderBase, NULL);
    //
    av_init_packet(&AVCD.Packet);
    av_init_packet(&AVCD.PacketOut);
    AVCD.frameOutput = av_frame_alloc();
};

int FFMPEGTools::FFMPEGDecodec::FFMPEGDecodecInsert(uint8_t *data, int size)
{
    uint8_t *dataParsed = NULL;
    int sizeParsed = 0;
    // int len = av_parser_parse2(AVCD.DecodeParser, AVCD.Decoder, &dataParsed, &sizeParsed, data, size, 0, 0, AV_NOPTS_VALUE);

    // std::cout << " \n <-------------------------------------------------------------------------------------------> size parsed:" << sizeParsed << "\n";
    // if (sizeParsed > 0)
    // {
    // AVCD.Packet.data = dataParsed;
    // AVCD.Packet.size = sizeParsed;
    AVCD.Packet.data = data;
    AVCD.Packet.size = size;

    // AVPacket flushPacket;
    // av_init_packet(&flushPacket);
    // avcodec_send_packet(AVCD.Decoder, &flushPacket);

    return avcodec_send_packet(AVCD.Decoder, &AVCD.Packet);
    // }
    // else
    // {
    //     return -1;
    // }
};

FFMPEGTools::AVData FFMPEGTools::FFMPEGDecodec::FFMPEGDecodecGetFrame()
{
    AVData dataGen;
    int err = avcodec_receive_frame(AVCD.Decoder, AVCD.frameOutput);

    if (err == 0)
    {
        int linesize[1];
        linesize[0] = AVCD.frameOutput->width * 3;
        //
        int width = AVCD.frameOutput->width;
        int height = AVCD.frameOutput->height;
        dataGen = AVData(width, height, width * height * 3);
        //
        SwsContext *conversion = sws_getContext(width, height, (AVPixelFormat)AVCD.frameOutput->format, width, height, AVPixelFormat::AV_PIX_FMT_BGR24, SWS_FAST_BILINEAR, NULL, NULL, NULL);
        sws_scale(conversion, AVCD.frameOutput->data, AVCD.frameOutput->linesize, 0, height, &dataGen.data, linesize);
        sws_freeContext(conversion);
    }
    else
    {
        dataGen.width = -1;
        dataGen.height = -1;
    }
    //
    return dataGen;
};