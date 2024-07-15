#include <unistd.h>
#include <iostream>
#include "FFMPEGCodec.hpp"
#include "Drive_Socket.hpp"
#include "WIFICastDriver.hpp"
#include <opencv4/opencv2/opencv.hpp>

#include "crc32.h"
#include "fec.hpp"
#define FEC_PACKET_MAX 32
#define FEC_DATA_MAX (FEC_PACKET_MAX * PacketPrePacks)

extern "C"
{
#include <libavutil/error.h>
}

uint32_t GetTimeStamp()
{
    struct timespec tv;
    clock_gettime(CLOCK_MONOTONIC, &tv);
    return ((tv.tv_sec * (uint32_t)1000000 + (tv.tv_nsec / 1000)));
}

int main(int argc, char const *argv[])
{
    uint64_t datarecive = 0;
    uint64_t dataLose = 0;
    double dataLoseRate = 0;

    bool dataisLose = false;
    std::deque<unsigned int> *packAVA;
    std::queue<std::tuple<std::shared_ptr<uint8_t>, int>> dataque;

    char cmd[64];
    sprintf(cmd, "iw dev %s set type monitor", argv[1]);
    system(cmd);
    sprintf(cmd, "iw dev %s set monitor fcsfail otherbss", argv[1]);
    system(cmd);
    sprintf(cmd, "iw dev %s set freq 5600 NOHT", argv[1]);
    system(cmd);
    sprintf(cmd, "iw dev %s set txpower fixed 3000", argv[1]);
    system(cmd);

    fec_init();

    WIFIBroadCast::WIFICastDriver *test;
    test = new WIFIBroadCast::WIFICastDriver({argv[1]});

    int parseerror = 0;

    WIFIBroadCast::VideoPackets *errorFrameTmp = new WIFIBroadCast::VideoPackets;
    errorFrameTmp->videoDataRaw.reset(new uint8_t[FEC_DATA_MAX]);

    FFMPEGTools::FFMPEGDecodec decoder;
    cv::namedWindow("test", cv::WINDOW_NORMAL);
    cv::setWindowProperty("test", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);

    test->WIFIRecvSinff(
        [&](WIFIBroadCast::VideoPackets *data, auto wirelssinfo, auto streamID)
        {
            datarecive++;
            int start = GetTimeStamp();
            if (streamID == 0) // RAW data here
            {
                int datauSize = data->videoRawSize;
                std::shared_ptr<uint8_t> datau;
                datau.reset(new uint8_t[data->videoRawSize]);
                std::copy(data->videoDataRaw.get(), data->videoDataRaw.get() + (datauSize - 4), datau.get());
                //=============================================================================//
                std::cout << '\n';
                std::cout << "\033[33mframeMark:" << data->vps.currentFrameMark << " frame packet count: " << data->vps.currentPacketSize << "\033[0m\n";
                std::cout << "try to find data: ";
                for (size_t i = 0; i < data->videoRawSize; i++)
                {
                    if (datau.get()[i] == 0 && datau.get()[i + 1] == 0 &&
                        datau.get()[i + 2] == 0 && datau.get()[i + 3] == 1)
                    {
                        std::cout << "offset: " << std::setw(7) << std::setfill(' ') << i << " -> ";
                        std::cout << "header: " << std::hex << "0x" << (int)datau.get()[i + 4] << std::dec << " <--> ";
                    }
                }
                std::cout << "\n";
                // FIXME: copy without ffsync id
                int crcGet = ((int)data->videoDataRaw.get()[datauSize - 4]) |
                             ((int)data->videoDataRaw.get()[datauSize - 3] << 8) |
                             ((int)data->videoDataRaw.get()[datauSize - 2] << 16) |
                             ((int)data->videoDataRaw.get()[datauSize - 1] << 24);

                // TODO: check CRC32
                uint32_t table[256];
                crc32::generate_table(table);
                uint32_t crc = crc32::update(table, 0, data->videoDataRaw.get(), datauSize - 4);
                if (crc == crcGet)
                {
                    std::cout << "check crc: " << std::hex << crc << "  " << crcGet << std::dec << "   " << datauSize << " \n";
                    dataque.push(std::make_tuple(datau, data->videoRawSize));
                    dataisLose = false;
                }
                else
                {
                    dataLose++;
                    std::cout << "check crc: " << std::hex << crc << "  " << crcGet << std::dec << "   " << datauSize << " ";
                    std::cout << "\033[31m data crc error \033[0m\n";
                    // TODO: adding FEC wait flag next
                    dataisLose = true;
                    // SOME DATA MUST COPY BY SELF
                    {
                        errorFrameTmp->vps = data->vps;
                        std::memset(errorFrameTmp->videoDataRaw.get(), 0x00, data->vps.currentPacketSize * PacketPrePacks);
                        std::copy(data->videoDataRaw.get(), data->videoDataRaw.get() + data->videoRawSize, errorFrameTmp->videoDataRaw.get());
                        errorFrameTmp->videoRawSize = data->videoRawSize;
                    }
                }

                if (datarecive == 300)
                {
                    datarecive = datarecive / 10;
                    dataLose = dataLose / 10;
                }
                int end = GetTimeStamp();
                std::cout << "check time using:" << std::dec << end - start << "\n";

                std::cout << "\033[32mdataLoseRate: " << (int)(((double)dataLose / (double)datarecive) * 100.f)
                          << " datasignal:" << wirelssinfo.antenSignal << " signalQ:" << wirelssinfo.signalQuality << "\033[0m\n";
            }
            else if (streamID == 1 && dataisLose)
            {
                // TODO: adding FEC decode fix data
                // fec data is append after frame, just check framemarker
                if (errorFrameTmp->vps.currentFrameMark == data->vps.currentFrameMark)
                {
                    // ========================================================================================//
                    unsigned int ErrNos[FEC_PACKET_MAX] = {0};
                    unsigned int FecNos[FEC_PACKET_MAX] = {0};

                    FecPacket<FEC_DATA_MAX, FEC_PACKET_MAX, PacketPrePacks> fecPool;
                    FecPacket<FEC_DATA_MAX, FEC_PACKET_MAX, PacketPrePacks> dataPool;
                    std::memset(fecPool.FecDataType_t.data1d, 0x00, FEC_DATA_MAX);
                    std::memset(dataPool.FecDataType_t.data1d, 0x00, FEC_DATA_MAX);

                    int datalast = -1;
                    int errorCount = 0;
                    int blocksizeIn = 0;
                    for (; !errorFrameTmp->vps.pakcetAvaliable.empty(); errorFrameTmp->vps.pakcetAvaliable.pop_front())
                    {
                        int blockset = std::get<0>(errorFrameTmp->vps.pakcetAvaliable.front());
                        int blocksize = std::get<1>(errorFrameTmp->vps.pakcetAvaliable.front());
                        // copy data to block next
                        std::copy((errorFrameTmp->videoDataRaw.get() + blocksizeIn),
                                  (errorFrameTmp->videoDataRaw.get() + blocksizeIn + blocksize),
                                  dataPool.FecDataType_t.data2d[blockset]);
                        blocksizeIn += blocksize;

                        // find err frame id
                        if (datalast + 1 != blockset)
                        {
                            for (size_t i = 0; i < (blockset - datalast - 1); i++)
                            {
                                ErrNos[errorCount] = datalast + i + 1;
                                errorCount++;
                            }
                        }
                        datalast = blockset;
                    }

                    // ========================================================================================//
                    // find final packet and fill errnos
                    int lastpackmiss = ((errorFrameTmp->vps.currentPacketSize - 1) - datalast);
                    if (lastpackmiss > 0)
                    {
                        for (size_t i = 0; i < lastpackmiss; i++)
                        {
                            ErrNos[errorCount] = datalast + 1 + i;
                            errorCount++;
                        }
                    }
                    // FIXME: COPY dequeue to array, because pass deque raw data fec_decode not accept it.
                    for (size_t i = 0; i < data->vps.pakcetAvaliable.size(); i++)
                    {
                        FecNos[i] = std::get<0>(data->vps.pakcetAvaliable.at(i));
                    }
                    // ========================================================================================//
                    std::copy((data->videoDataRaw.get()),
                              (data->videoDataRaw.get() + data->videoRawSize),
                              fecPool.FecDataType_t.data1d);
                    fec_decode(PacketPrePacks,
                               dataPool.dataout,
                               errorFrameTmp->vps.currentPacketSize,
                               fecPool.dataout,
                               FecNos, ErrNos,
                               errorCount);
                    // ========================================================================================//
                    // FIXME: should check crc?
                    int crcGet = 0;
                    uint32_t crc = 0;
                    if (lastpackmiss <= 0)
                    {
                        // back search
                        for (size_t i = errorFrameTmp->videoRawSize + (errorCount * PacketPrePacks); i > 0; i--)
                        {
                            if (dataPool.FecDataType_t.data1d[i] != 0)
                            {
                                crcGet = ((int)dataPool.FecDataType_t.data1d[i - 3]) |
                                         ((int)dataPool.FecDataType_t.data1d[i - 2] << 8) |
                                         ((int)dataPool.FecDataType_t.data1d[i - 1] << 16) |
                                         ((int)dataPool.FecDataType_t.data1d[i] << 24);
                                break;
                            }
                        }
                        uint32_t table[256];
                        crc32::generate_table(table);
                        crc = crc32::update(table, 0, dataPool.FecDataType_t.data1d, errorFrameTmp->videoRawSize + (errorCount * PacketPrePacks) - 4);
                    }
                    // ERROR DEBUG
                    {
                        std::cout << "\033[34mFEC BLOCK check:";
                        for (size_t i = 0; i < data->vps.pakcetAvaliable.size(); i++)
                        {
                            std::cout << FecNos[i] << " ";
                        }
                        std::cout << std::hex << "\nERR BLOCK CHECK :";
                        for (size_t i = 0; i < errorCount; i++)
                        {
                            std::cout << (int64_t)ErrNos[i] << " ";
                        }
                        std::cout << "\033[0m\n"
                                  << std::dec;
                        std::cout << "check crc fix: " << std::hex << crc << " " << crcGet << std::dec << " size: " << errorFrameTmp->videoRawSize + (errorCount * PacketPrePacks) - 4;
                        if (crc != crcGet)
                            std::cout << "\033[31m data crc fix error \033[0m";
                        else
                            std::cout << "\033[32m data crc fix WELL \033[0m";
                        std::cout << '\n';
                    }
                    // Pushing fixed data
                    {
                        std::shared_ptr<uint8_t> datau;
                        int datasize = PacketPrePacks * errorFrameTmp->vps.currentPacketSize; // just using the max data packetsize, decode will handle it
                        datau.reset(new uint8_t[datasize]);
                        std::copy(dataPool.FecDataType_t.data1d, dataPool.FecDataType_t.data1d + datasize, datau.get());
                        dataque.push(std::make_tuple(datau, datasize));
                    }
                }
            }
        });

    int IframePer = 0;
    int lastLose = 0;
    FlowThread rest(
        [&]
        {
            if (dataque.size() > 0)
            {
                for (; !dataque.empty(); dataque.pop())
                {
                    char errmsg[2000];
                    int err = decoder.FFMPEGDecodecInsert(std::get<std::shared_ptr<uint8_t>>(dataque.front()).get(), std::get<int>(dataque.front()));

                    if (err < 0)
                    {
                        char otp[AV_ERROR_MAX_STRING_SIZE] = {0};
                        std::cout << "\033[36mFFMPEG INFO:" << av_make_error_string(otp, AV_ERROR_MAX_STRING_SIZE, err) << "\033[0m\n";
                    }
                }
                // TODO: check header and found header to comfirm
                {
                    while (true)
                    {
                        FFMPEGTools::AVData data = decoder.FFMPEGDecodecGetFrame();
                        //
                        if (data.width != -1)
                        {
                            cv::Mat matData(data.height, data.width, CV_8UC3, data.data);
                            cv::flip(matData, matData, -1);
                            cv::resize(matData, matData, cv::Size(1024, 768));
                            cv::imshow("test", matData);
                            cv::waitKey(1);
                        }
                        else
                            break;
                    }
                }
            }
        });

    sleep(-1);
    // res.FlowWait();
    return 0;
}
