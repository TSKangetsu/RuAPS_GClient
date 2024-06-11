#include <unistd.h>
#include <iostream>
// #define NETDEBUG
#include "FFMPEGCodec.hpp"
#include "Drive_Socket.hpp"
#include "WIFICastDriver.hpp"
#include <opencv4/opencv2/opencv.hpp>

#include "crc32.h"

extern "C"
{
#include <libavutil/error.h>
}

int main(int argc, char const *argv[])
{
    std::queue<std::tuple<std::shared_ptr<uint8_t>, int>> dataque;

    char cmd[64];
    sprintf(cmd, "iw dev %s set type monitor", argv[1]);
    system(cmd);
    sprintf(cmd, "iw dev %s set monitor fcsfail", argv[1]);
    system(cmd);
    sprintf(cmd, "iw dev %s set freq 5600", argv[1]);
    system(cmd);
    sprintf(cmd, "iw dev %s set txpower fixed 3000", argv[1]);
    system(cmd);

    WIFIBroadCast::WIFICastDriver *test;
    test = new WIFIBroadCast::WIFICastDriver({argv[1]});

    int parseerror = 0;
    FFMPEGTools::FFMPEGDecodec decoder;

    cv::namedWindow("test", cv::WINDOW_NORMAL);
    cv::setWindowProperty("test", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);

    test->WIFIRecvSinff(
        [&](auto data)
        {
            int datauSize = data->videoRawSize;
            std::shared_ptr<uint8_t> datau;
            datau.reset(new uint8_t[data->videoRawSize]);
            std::copy(data->videoDataRaw.get(), data->videoDataRaw.get() + (datauSize - 4), datau.get());
            // for (size_t i = 0; i < data->videoRawSize; i++)
            // {
            //     if (datau[i] == 0 && datau[i + 1] == 0 &&
            //         datau[i + 2] == 0 && datau[i + 3] == 1)
            //     {
            //         std::cout << "offset: " << std::setw(7) << std::setfill(' ') << i << " -> ";
            //         std::cout << "header: " << std::hex << "0x" << (int)datau[i + 4] << std::dec << " <--> ";
            //     }
            // }
            // std::copy(data->videoDataRaw.get(), data->videoDataRaw.get() + (datauSize - 4), datau);
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
            }
            else
            {
                std::cout << "check crc: " << std::hex << crc << "  " << crcGet << std::dec << "   " << datauSize << " ";
                std::cout << "data crc error\n";
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
                    // std::cout << "head: " << (int)std::get<uint8_t *>(dataque.front())[std::get<int>(dataque.front())] << '\n';
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
