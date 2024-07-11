#include <iostream>
#include <unistd.h>
#include "fec.hpp"
#include <bitset>
#include <climits>
#include <cstring>
#include <iostream>

int main(int argc, char const *argv[])
{
    // TEST PASSED
    fec_init();

    FecPacket<2500, 5, 500> dataPool;
    FecPacket<2500, 5, 500> fecPool;
    FecPacket<2500, 5, 500> fecPoolUse;
    FecPacket<2500, 5, 500> dataPoolFake;

    std::memset(fecPool.FecDataType_t.data1d, 0x00, 2500);
    std::memset(dataPool.FecDataType_t.data1d, 0x00, 2500);
    std::memset(dataPool.FecDataType_t.data1d, 0xFF, 500);
    std::memset(dataPoolFake.FecDataType_t.data1d, 0x00, 2500);
    std::memset(dataPoolFake.FecDataType_t.data1d + 1000, 0xFF, 500);
    dataPool.FecDataType_t.data1d[15] = 0x7F;

    for (size_t i = 0; i < 2500; i++)
    {
        std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data1d[i] << " ";
    }
    std::cout << '\n';
    std::cout << '\n';
    for (size_t i = 0; i < 5; i++)
    {
        for (size_t k = 0; k < 500; k++)
        {
            std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data2d[i][k] << " ";
        }
    }
    std::cout << '\n';
    std::cout << '\n';

    fec_encode(500, dataPool.dataout, 5, fecPool.dataout, 5);

    unsigned int fecnos[] = {0, 1, 0, 0, 0};
    unsigned int earnos[] = {0, 2, 0, 0, 0};
    std::memcpy(fecPoolUse.FecDataType_t.data2d[0],
                fecPool.FecDataType_t.data2d[0],
                sizeof(fecPool.FecDataType_t.data2d[0]));
    std::memcpy(fecPoolUse.FecDataType_t.data2d[1],
                fecPool.FecDataType_t.data2d[1],
                sizeof(fecPool.FecDataType_t.data2d[2]));

    fec_decode(500, dataPoolFake.dataout, 5,
               fecPoolUse.dataout,
               fecnos, earnos,
               2);

    for (size_t i = 0; i < 2500; i++)
    {
        std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data1d[i] << " ";
    }
    std::cout << '\n';
    std::cout << '\n';
    for (size_t i = 0; i < 5; i++)
    {
        for (size_t k = 0; k < 500; k++)
        {
            std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data2d[i][k] << " ";
        }
    }
    std::cout << '\n';

    return 0;
}

// #include <unistd.h>
// #include <iostream>
// // #define NETDEBUG
// #include "FFMPEGCodec.hpp"
// #include "Drive_Socket.hpp"
// #include "WIFICastDriver.hpp"
// #include <opencv4/opencv2/opencv.hpp>

// #include "crc32.h"

// extern "C"
// {
// #include <libavutil/error.h>
// }

// uint32_t GetTimeStamp()
// {
//     struct timespec tv;
//     clock_gettime(CLOCK_MONOTONIC, &tv);
//     return ((tv.tv_sec * (uint32_t)1000000 + (tv.tv_nsec / 1000)));
// }

// int main(int argc, char const *argv[])
// {
//     uint64_t datarecive = 0;
//     uint64_t dataLose = 0;
//     double dataLoseRate = 0;

//     std::queue<std::tuple<std::shared_ptr<uint8_t>, int>> dataque;

//     char cmd[64];
//     sprintf(cmd, "iw dev %s set type monitor", argv[1]);
//     system(cmd);
//     sprintf(cmd, "iw dev %s set monitor fcsfail otherbss", argv[1]);
//     system(cmd);
//     sprintf(cmd, "iw dev %s set freq 5600 NOHT", argv[1]);
//     system(cmd);
//     sprintf(cmd, "iw dev %s set txpower fixed 3000", argv[1]);
//     system(cmd);

//     WIFIBroadCast::WIFICastDriver *test;
//     test = new WIFIBroadCast::WIFICastDriver({argv[1]});

//     // uint8_t dats[] = {0xFF, 0xFF, 0xFF, 0xFF};
//     // while (true)
//     // {
//     //    test->WIFICastInject(dats, 4, 0, WIFIBroadCast::BroadCastType::VideoStream, 0, 0);
//     // }

//     int parseerror = 0;
//     FFMPEGTools::FFMPEGDecodec decoder;

//     cv::namedWindow("test", cv::WINDOW_NORMAL);
//     cv::setWindowProperty("test", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);

//     test->WIFIRecvSinff(
//         [&](auto data, auto wirelssinfo)
//         {
//             int start = GetTimeStamp();
//             datarecive++;
//             int datauSize = data->videoRawSize;
//             std::shared_ptr<uint8_t> datau;
//             datau.reset(new uint8_t[data->videoRawSize]);
//             std::copy(data->videoDataRaw.get(), data->videoDataRaw.get() + (datauSize - 4), datau.get());
//             for (size_t i = 0; i < data->videoRawSize; i++)
//             {
//                 if (datau.get()[i] == 0 && datau.get()[i + 1] == 0 &&
//                     datau.get()[i + 2] == 0 && datau.get()[i + 3] == 1)
//                 {
//                     std::cout << "offset: " << std::setw(7) << std::setfill(' ') << i << " -> ";
//                     std::cout << "header: " << std::hex << "0x" << (int)datau.get()[i + 4] << std::dec << " <--> ";
//                 }
//             }
//             std::cout << "\n";
//             // FIXME: copy without ffsync id
//             int crcGet = ((int)data->videoDataRaw.get()[datauSize - 4]) |
//                          ((int)data->videoDataRaw.get()[datauSize - 3] << 8) |
//                          ((int)data->videoDataRaw.get()[datauSize - 2] << 16) |
//                          ((int)data->videoDataRaw.get()[datauSize - 1] << 24);

//             // TODO: check CRC32
//             uint32_t table[256];
//             crc32::generate_table(table);
//             uint32_t crc = crc32::update(table, 0, data->videoDataRaw.get(), datauSize - 4);
//             if (crc == crcGet)
//             {
//                 std::cout << "check crc: " << std::hex << crc << "  " << crcGet << std::dec << "   " << datauSize << " \n";
//                 dataque.push(std::make_tuple(datau, data->videoRawSize));
//             }
//             else
//             {
//                 dataLose++;
//                 std::cout << "check crc: " << std::hex << crc << "  " << crcGet << std::dec << "   " << datauSize << " ";
//                 std::cout << "\033[31m data crc error \033[0m\n";
//             }

//             if (datarecive == 300)
//             {
//                 datarecive = datarecive / 10;
//                 dataLose = dataLose / 10;
//             }

//             std::cout << "\033[32mdataLoseRate: " << (int)(((double)dataLose / (double)datarecive) * 100.f)
//                       << " datasignal:" << wirelssinfo.antenSignal << " signalQ:" << wirelssinfo.signalQuality << "\033[0m\n ";
//             int end = GetTimeStamp();

//             std::cout << "check time using:" << std::dec << end - start << "\n";
//         });

//     int IframePer = 0;
//     int lastLose = 0;
//     FlowThread rest(
//         [&]
//         {
//             if (dataque.size() > 0)
//             {
//                 for (; !dataque.empty(); dataque.pop())
//                 {
//                     char errmsg[2000];
//                     int err = decoder.FFMPEGDecodecInsert(std::get<std::shared_ptr<uint8_t>>(dataque.front()).get(), std::get<int>(dataque.front()));

//                     if (err < 0)
//                     {
//                         char otp[AV_ERROR_MAX_STRING_SIZE] = {0};
//                         // printf("%s \n", av_make_error_string(otp, AV_ERROR_MAX_STRING_SIZE, err));
//                         // parseerror++;
//                     }
//                 }
//                 // TODO: check header and found header to comfirm
//                 {
//                     while (true)
//                     {
//                         FFMPEGTools::AVData data = decoder.FFMPEGDecodecGetFrame();
//                         //
//                         if (data.width != -1)
//                         {
//                             cv::Mat matData(data.height, data.width, CV_8UC3, data.data);
//                             cv::flip(matData, matData, -1);
//                             cv::resize(matData, matData, cv::Size(1024, 768));
//                             cv::imshow("test", matData);
//                             cv::waitKey(1);
//                         }
//                         else
//                             break;
//                     }
//                 }
//             }
//         });

//     sleep(-1);
//     // res.FlowWait();
//     return 0;
// }
