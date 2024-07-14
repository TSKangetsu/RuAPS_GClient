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
                        std::copy(data->videoDataRaw.get(), data->videoDataRaw.get() + data->videoRawSize - 4, errorFrameTmp->videoDataRaw.get());
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
                    FecPacket<FEC_DATA_MAX, FEC_PACKET_MAX, PacketPrePacks> fecPool;
                    FecPacket<FEC_DATA_MAX, FEC_PACKET_MAX, PacketPrePacks> dataPool;
                    unsigned int FecNos[FEC_PACKET_MAX] = {0};
                    unsigned int ErrNos[FEC_PACKET_MAX] = {0};

                    std::memset(fecPool.FecDataType_t.data1d, 0x00, FEC_DATA_MAX);
                    std::memset(dataPool.FecDataType_t.data1d, 0x00, FEC_DATA_MAX);

                    int index = 0;
                    int blocksizeIn = 0;
                    int datalast = -1;
                    int errorSize = 0;
                    // std::cout << "\033[34mRaw data ava check:";

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
                                ErrNos[index] = datalast + i + 1;
                                index++;
                            }
                        }
                        datalast = blockset;
                        errorSize = index;
                        // debug
                        // std::cout << std::hex << (int)blockset << " ";
                    }

                    // ========================================================================================//
                    // std::cout << "\033[0m\n";
                    // find final packet and fill errnos
                    int lastpackmiss = ((errorFrameTmp->vps.currentPacketSize - 1) - datalast);
                    // std::cout << "last pack: " << lastpackmiss << "\n";
                    if (lastpackmiss > 0)
                    {
                        for (size_t i = 0; i < lastpackmiss; i++)
                        {
                            ErrNos[index] = datalast + 1 + i;
                            index++;
                        }
                    }

                    std::cout << "\033[34mERROR BLOCK check:";
                    for (size_t i = 0; i < index; i++)
                    {
                        std::cout << std::hex << (int)ErrNos[i] << " ";
                    }
                    std::cout << "\033[0m\n";
                    // ========================================================================================//
                    index = 0;
                    // std::cout << "\033[34mFEC data ava check:";
                    for (; !data->vps.pakcetAvaliable.empty(); data->vps.pakcetAvaliable.pop_front())
                    {
                        // std::cout << std::hex << (int)std::get<0>(data->vps.pakcetAvaliable.front()) << " ";
                        FecNos[index] = std::get<0>(data->vps.pakcetAvaliable.front());
                        index++;
                    }
                    // std::cout << std::dec << "\033[0m\n";

                    std::cout << "\033[34mFEC BLOCK check:";
                    for (size_t i = 0; i < index; i++)
                    {
                        std::cout << FecNos[i] << " ";
                    }

                    std::cout << "\033[0m\n";
                    std::copy((data->videoDataRaw.get()),
                              (data->videoDataRaw.get() + data->videoRawSize),
                              fecPool.FecDataType_t.data1d);

                    // for (size_t i = 0; i < 12; i++)
                    // {
                    //     for (size_t k = 0; k < PacketPrePacks; k++)
                    //     {
                    //         std::cout << std::hex << (int)dataPool.FecDataType_t.data2d[i][k] << std::dec << " ";
                    //     }
                    //     std::cout << "===================\n";
                    // }
                    // std::cout << "\n";
                    // std::cout << "\n";

                    // for (size_t i = 0; i < 12; i++)
                    // {
                    //     for (size_t k = 0; k < PacketPrePacks; k++)
                    //     {
                    //         std::cout << std::hex << (int)fecPool.FecDataType_t.data2d[i][k] << std::dec << " ";
                    //     }
                    //     std::cout << "===================\n";
                    // }
                    // std::cout << "\n";
                    // std::cout << "\n";

                    fec_decode(PacketPrePacks,
                               dataPool.dataout,
                               errorFrameTmp->vps.currentPacketSize,
                               fecPool.dataout,
                               FecNos, ErrNos,
                               errorSize);

                    // for (size_t i = 0; i < PacketPrePacks; i++)
                    // {
                    //     std::cout << std::hex << (int)dataPool.FecDataType_t.data2d[ErrNos[0]][i] << std::dec << " ";
                    // }
                    // std::cout << "\n";

                    // for (size_t k = 0; k < 16384; k++)
                    // {
                    //     std::cout << std::hex << (int)dataPool.FecDataType_t.data1d[k] << std::dec << " ";
                    // }
                    // std::cout << "===================\n";

                    uint32_t table[256];
                    crc32::generate_table(table);
                    uint32_t crc = crc32::update(table, 0, dataPool.FecDataType_t.data1d, 16384);
                    std::cout << "check crc fix: " << std::hex << crc << std::hex << "\n";

                    // std::shared_ptr<uint8_t> datau;
                    // datau.reset(new uint8_t[data->videoRawSize]);
                    // std::copy(dataPool.FecDataType_t.data1d, dataPool.FecDataType_t.data1d + , datau.get());
                    // dataque.push(std::make_tuple(datau, errorFrameTmp->videoRawSize));
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
                        // printf("%s \n", av_make_error_string(otp, AV_ERROR_MAX_STRING_SIZE, err));
                        // parseerror++;
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
