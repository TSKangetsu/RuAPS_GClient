#include <unistd.h>
#include <iostream>
// #define NETDEBUG
#include "FFMPEGCodec.hpp"
#include "Drive_Socket.hpp"
#include "WIFICastDriver.hpp"
#include <opencv4/opencv2/opencv.hpp>

#include <libavutil/error.h>

int main(int argc, char const *argv[])
{
    std::queue<std::tuple<uint8_t *, int>> dataque;

    WIFIBroadCast::WIFICastDriver *test;
    test = new WIFIBroadCast::WIFICastDriver({"wlan1"});
    test->WIFIRecvSinff();

    int parseerror = 0;
    FFMPEGTools::FFMPEGDecodec decoder;

    cv::namedWindow("test", cv::WINDOW_NORMAL);
    cv::setWindowProperty("test", cv::WND_PROP_FULLSCREEN, cv::WINDOW_FULLSCREEN);

    FlowThread res(
        [&]
        {
            if (test->WIFIRecvVideoSeq() > 0)
            {
                auto data = test->WIFIRecvVideoDMA(0);

                test->WIFIRecvWaitFrame();
                if (std::get<int *>(data)[DATA_SIZENOW] > 0)
                {
                    int datauSize = std::get<int *>(data)[DATA_SIZENOW];
                    uint8_t *datau = new uint8_t[datauSize];
                    std::copy(std::get<unsigned char *>(data), std::get<unsigned char *>(data) + datauSize, datau);

                    //
                    dataque.push(std::make_tuple(datau, std::get<int *>(data)[DATA_SIZENOW]));
                    // {
                    //     for (size_t i = 0; i < datauSize; i++)
                    //     {
                    //         if (datau[i] == 0 && datau[i + 1] == 0 &&
                    //             datau[i + 2] == 0 && datau[i + 3] == 1)
                    //         {
                    //             // std::cout << "header: " << std::hex << "0x" << (int)datau[i + 4] << std::dec << "\n";
                    //             // std::cout << "size:   " << datauSize << "\n";
                    //             //
                    //             // if (datau[i + 4] == 0x68)
                    //             // {
                    //             //     decoder.FFMPEGDecodecInsert((datau + i), datauSize - i);
                    //             //     datauSize = i;
                    //             // }
                    //         }
                    //     }
                    // }
                    //
                    // char errmsg[2000];
                    // int err = decoder.FFMPEGDecodecInsert(datau, datauSize);
                    // if (err < 0)
                    // {
                    //     std::cout << av_make_error_string(errmsg, 2000, err) << "\n\n";
                    //     parseerror++;
                    // }
                    // FFMPEGTools::AVData dataf = decoder.FFMPEGDecodecGetFrame();
                    // if (dataf.width != -1)
                    // {
                    //     cv::Mat matData(dataf.height, dataf.width, CV_8UC3, dataf.data);

                    //     if (!matData.empty())
                    //     {
                    //         cv::flip(matData, matData, -1);
                    //         cv::resize(matData, matData, cv::Size(1024, 768));

                    //         cv::imshow("test", matData);
                    //         cv::waitKey(5);
                    //     }
                    // }
                }

                // std::get<int *>(data)[DATA_SIZENOW] = 0;
            }
        });

    // FlowThread resq(
    //     [&]
    //     {
    //         if (dataque.size() > 0)
    //         {
    //             for (size_t i = 0; i < std::get<int>(dataque.front()); i++)
    //             {
    //                 std::cout << std::get<uint8_t *>(dataque.front())[i];
    //             }
    //             std::cout.flush();
    //             dataque.pop();
    //         }
    //     },
    //     30.f);

    FlowThread ree(
        [&]
        {
            if (test->WIFIRecvVideoSeq() > 0)
            {
                auto data = test->WIFIRecvVideoDMA(0);
                std::cout << "dataLose:" << std::get<int *>(data)[DATA_LOSE] << '\n';
                std::cout << "h264Lose:" << parseerror << '\n';
            }
        },
        1.f);

    // int IframePer = 0;
    int lastLose = 0;
    FlowThread rest(
        [&]
        {
            if (dataque.size() > 0)
            {
                for (; !dataque.empty(); dataque.pop())
                {
                    char errmsg[2000];
                    int err = decoder.FFMPEGDecodecInsert(std::get<uint8_t *>(dataque.front()), std::get<int>(dataque.front()));

                    if (err < 0)
                    {
                        parseerror++;
                        // std::cout << av_make_error_string(errmsg, 2000, err) << "\n\n";
                    }

                    {
                        auto data = test->WIFIRecvVideoDMA(0);
                        for (size_t i = 0; i < std::get<int>(dataque.front()); i++)
                        {
                            if (std::get<uint8_t *>(dataque.front())[i] == 0 && std::get<uint8_t *>(dataque.front())[i + 1] == 0 &&
                                std::get<uint8_t *>(dataque.front())[i + 2] == 0 && std::get<uint8_t *>(dataque.front())[i + 3] == 1)
                            {
                                // std::cout << "offset: " << std::setw(7) << std::setfill(' ') << i << " -> ";
                                // std::cout << "header: " << std::hex << "0x" << (int)std::get<uint8_t *>(dataque.front())[i + 4] << std::dec << " <--> ";
                                // if ((int)std::get<uint8_t *>(dataque.front())[i + 4] == 0x65)
                                // {
                                //     if (std::get<int *>(data)[DATA_LOSE] > lastLose)
                                //     {
                                //         std::cout << "oops! \n";
                                //     }
                                //     lastLose = std::get<int *>(data)[DATA_LOSE];
                                // }

                                // if ((int)std::get<uint8_t *>(dataque.front())[i + 4] == 0x41)
                                //     IframePer++;
                                // if (IframePer > 32)
                                // {
                                //     std::cout << "===========================================================================================================================================I Missing!\n";
                                //     IframePer = 0;
                                // }
                                // if ((int)std::get<uint8_t *>(dataque.front())[i + 4] == 0x65)
                                //     IframePer = 0;
                            }
                        }
                        // std::cout << " <==========> size-got: " << std::get<int>(dataque.front()) << " \n";
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
                //
                // dataque.pop();
            }
        });

    res.FlowWait();
    return 0;
}
