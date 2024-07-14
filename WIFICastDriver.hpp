#pragma once
#include <tuple>
#include <vector>
#include <queue>
#include <condition_variable>
#include "Drive_Socket.hpp"
#include "FlowController.hpp"
#include <cstring>
#include <deque>
#include <iomanip>

#define CAST32(x) reinterpret_cast<uint32_t *>(x)[0]

#define HeaderSize 61
#define FrameTypeL (HeaderSize - 3)
#define FrameLLCMark (HeaderSize - 2)
#define VideoTrans 0x68
#define DataETrans 0x69
#define FeedBackTrans 0x77
#define SocketMTU 1490 - 6 // FCS auto add by driver or kerenl
#define SocketMTUMAX 1500
#define PacketPrePacks (SocketMTU - HeaderSize)

namespace WIFIBroadCast
{
    enum BroadCastType
    {
        VideoStream,
        DataStream,
    };

    struct VideoPacketsInfo
    {
        uint8_t header[HeaderSize];
        uint8_t frameID;
        uint8_t maxVideosize[4]; // get uint32_t data by CAST32(maxVideosize)
        uint16_t width;
        uint16_t height;
        uint8_t dataEnd;
        uint8_t scrc32[4];
    };

    struct VideoPacketsSetter
    {
        VideoPacketsInfo info;
        uint32_t dataLose;
        uint32_t currentFrameSeq;
        uint32_t currentDataSize;
        uint32_t currentFrameMark;
        uint32_t currentPacketSize;
        std::deque<std::tuple<uint8_t, int>> pakcetAvaliable;
    };

    struct VideoPackets
    {
        VideoPacketsSetter vps;
        int videoRawSize;
        std::shared_ptr<uint8_t> videoDataRaw;
    };

    struct WirelessInfo
    {
        int antenSignal;
        int signalQuality;
    };

    class WIFICastDriver
    {
    public:
        std::queue<std::string> DataEBuffer;

        WIFICastDriver(std::vector<std::string> Interfaces);
        ~WIFICastDriver();

        int WIFICastInject(uint8_t *data, int size, int InterfaceID, BroadCastType type, int delayUS, uint8_t FrameQueueID, uint8_t FrameMarking);

        void WIFICastInjectMulti(uint8_t *data, int size, int delayUS) {};
        void WIFICastInjectMultiBL(uint8_t *data, int size, int delayUS) {};
        //
        void WIFIRecvSinff(std::function<void(VideoPackets *, WirelessInfo, int)> vcallback);

    private:
        struct InjectPacketLLCInfo
        {
            const uint8_t RadioHeader[16] = {
                0x00, 0x00, 0x20, 0x00, 0xae, 0x40, 0x00, 0xa0,
                0x20, 0x08, 0x00, 0xa0, 0x20, 0x08, 0x00, 0x00};
            // Auto Complete by driver or kerenl
            // TODO: must set data rate or defaultly run in 6mbps
            const uint8_t RadioInfo[16] = {
                0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            // Frame Control field and Duration
            const uint8_t Data80211Info[4] = {
                0x08, 0x02, 0x00, 0x00};
            // destination or BSS id should be zero
            const uint8_t DeviceInfo_DST[6] = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            const uint8_t DeviceInfo_BSS[6] = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            // Only SRC use for dentification when broadcast injection
            uint8_t DeviceInfo_SRC[6] = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            // Only One of Then can be use In a Frame
            const uint8_t SquenceAndLLCVideo[7] = {
                0x00, 0x00, 0x00, 0x00, VideoTrans, 0xff, 0xff};
            const uint8_t SquenceAndLLCDataE[7] = {
                0x00, 0x00, 0x00, 0x00, DataETrans, 0xff, 0xff};
        } InjectPacketLLC;

        int InterfaceCount = 0;
        uint8_t FrameCounter68 = 0;
        uint8_t FrameCounter69 = 0;
        uint8_t **PacketVideo;
        uint8_t **PacketDatae;
        std::vector<sock_filter> RecvFilter;
        std::vector<std::unique_ptr<Socket>> SocketInjectors;
        std::unique_ptr<FlowThread> RecvThread;
        std::vector<VideoPackets> VideoPacketsBuffer;

        std::function<void(VideoPackets *, WirelessInfo, int)> videoCallBack;
    };
}

//===============================================//
WIFIBroadCast::WIFICastDriver::WIFICastDriver(std::vector<std::string> Interfaces)
{
    InterfaceCount = Interfaces.size();
    PacketVideo = new uint8_t *[Interfaces.size()];
    PacketDatae = new uint8_t *[Interfaces.size()];

    for (size_t i = 0; i < Interfaces.size(); i++)
    {
        std::unique_ptr<Socket> Injector;
        Injector.reset(new Socket());
        Injector->CreateIllegal(Interfaces[i].c_str());
        PacketVideo[i] = new uint8_t[HeaderSize];
        PacketDatae[i] = new uint8_t[HeaderSize];
        memcpy(InjectPacketLLC.DeviceInfo_SRC, Injector->InterfaceMACGet(), 6);

        const uint32_t selfMacFliter = InjectPacketLLC.DeviceInfo_SRC[2] << 24 | InjectPacketLLC.DeviceInfo_SRC[3] << 16 | InjectPacketLLC.DeviceInfo_SRC[4] << 8 | InjectPacketLLC.DeviceInfo_SRC[5];
        RecvFilter = {
            {(BPF_LD | BPF_W | BPF_ABS), 0, 0, 50},
            {(BPF_JMP | BPF_JEQ | BPF_K), 3, 0, selfMacFliter},
            {(BPF_LD | BPF_H | BPF_ABS), 0, 0, 59},
            {(BPF_JMP | BPF_JEQ | BPF_K), 0, 1, 0xffff},
            {(BPF_RET | BPF_K), 0, 0, 0x00040000},
            {(BPF_RET | BPF_K), 0, 0, 0x00000000},
        };

        int offset = 0;
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.RadioHeader, sizeof(InjectPacketLLC.RadioHeader));
        offset += sizeof(InjectPacketLLC.RadioHeader);
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.RadioInfo, sizeof(InjectPacketLLC.RadioInfo));
        offset += sizeof(InjectPacketLLC.RadioInfo);
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.Data80211Info, sizeof(InjectPacketLLC.Data80211Info));
        offset += sizeof(InjectPacketLLC.Data80211Info);
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.DeviceInfo_DST, sizeof(InjectPacketLLC.DeviceInfo_DST));
        offset += sizeof(InjectPacketLLC.DeviceInfo_DST);
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.DeviceInfo_BSS, sizeof(InjectPacketLLC.DeviceInfo_BSS));
        offset += sizeof(InjectPacketLLC.DeviceInfo_BSS);
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.DeviceInfo_SRC, sizeof(InjectPacketLLC.DeviceInfo_SRC));

        offset += sizeof(InjectPacketLLC.DeviceInfo_SRC);
        memcpy(PacketDatae[i], PacketVideo[i], offset);
        memcpy(PacketDatae[i] + offset, InjectPacketLLC.SquenceAndLLCDataE, sizeof(InjectPacketLLC.SquenceAndLLCDataE));
        memcpy(PacketVideo[i] + offset, InjectPacketLLC.SquenceAndLLCVideo, sizeof(InjectPacketLLC.SquenceAndLLCVideo));

        Injector->SocketFilterApply(&RecvFilter[0], RecvFilter.size());
        SocketInjectors.push_back(std::move(Injector));
    }
}

WIFIBroadCast::WIFICastDriver::~WIFICastDriver()
{
    if (RecvThread != nullptr)
    {
        RecvThread->detach(); // I can't handel this , need help, Recv is block and can't shutoff
    }
    for (size_t i = 0; i < SocketInjectors.size(); i++)
    {
        SocketInjectors[i].reset();
    }
    for (int i = 0; i < InterfaceCount; ++i)
    {
        delete[] PacketVideo[i];
        delete[] PacketDatae[i];
    }
    delete[] PacketVideo;
    delete[] PacketDatae;
}

int WIFIBroadCast::WIFICastDriver::WIFICastInject(uint8_t *data, int len, int InterfaceID, BroadCastType type, int delayUS, uint8_t FrameQueueID, uint8_t FrameMarking)
{
    float PacketSize = (((float)len / (float)(PacketPrePacks)) == ((int)(len / (PacketPrePacks))))
                           ? ((int)(len / (PacketPrePacks)))
                           : ((int)(len / (PacketPrePacks))) + 1;
    // FIXME: force frame counter as 0
    FrameCounter68 = 0;
    FrameCounter69 = 0;

    for (size_t i = 0; i < PacketSize; i++)
    {
        uint8_t tmpData[SocketMTU + 2] = {0x00};
        //
        if (type == BroadCastType::VideoStream)
            std::copy(PacketVideo[InterfaceID], PacketVideo[InterfaceID] + HeaderSize, tmpData);
        if (type == BroadCastType::DataStream)
            std::copy(PacketDatae[InterfaceID], PacketDatae[InterfaceID] + HeaderSize, tmpData);

        if (!(((float)len / (float)(PacketPrePacks)) == ((int)(len / (PacketPrePacks)))) && i == (PacketSize - 1))
        {
            int size = ((PacketPrePacks) - (PacketSize * (PacketPrePacks)-len));
            //
            if (type == BroadCastType::VideoStream)
                tmpData[((size + HeaderSize + 1))] = FrameQueueID << 5 | 0x1f;
            if (type == BroadCastType::DataStream)
                tmpData[((size + HeaderSize + 1))] = FrameQueueID << 5 | 0x1f;
            // TODO: adding extra id frame locator
            tmpData[((size + HeaderSize))] = FrameMarking;
            //
            tmpData[FrameTypeL - 1] = (size + HeaderSize + 2);
            tmpData[FrameTypeL - 2] = (size + HeaderSize + 2) >> 8;
            //
            int dataStart = (i * (PacketPrePacks));
            int dataEnd = (i * (PacketPrePacks)) + size;
            std::copy(data + dataStart, data + dataEnd, tmpData + HeaderSize);
            SocketInjectors[InterfaceID]->Inject(tmpData, (size + HeaderSize + 2));
        }
        else
        {
            // TODO: adding frame count up to 5bit, left 8 / 2 channel
            if (type == BroadCastType::VideoStream)
                tmpData[(SocketMTU + 1)] = FrameQueueID << 5 | FrameCounter68;
            if (type == BroadCastType::DataStream)
                tmpData[(SocketMTU + 1)] = FrameQueueID << 5 | FrameCounter69;
            // TODO: adding extra id frame locator
            tmpData[(SocketMTU)] = FrameMarking;
            //
            tmpData[(FrameTypeL - 1)] = (uint8_t)(SocketMTU + 2);
            tmpData[(FrameTypeL - 2)] = (uint8_t)((SocketMTU + 2) >> 8);
            std::copy((data + (i * (PacketPrePacks))), (data + (i * (PacketPrePacks))) + (PacketPrePacks), tmpData + HeaderSize);
            SocketInjectors[InterfaceID]->Inject(tmpData, SocketMTU + 2);
        }
        if (delayUS)
            usleep(delayUS);

        if (type == BroadCastType::VideoStream && PacketSize - 1 != i)
        {
            FrameCounter68++;
            if (FrameCounter68 >= 0x1f)
                FrameCounter68 = 0x0;
        }
        else if (type == BroadCastType::DataStream && PacketSize - 1 != i)
        {
            FrameCounter69++;
            if (FrameCounter69 >= 0x1f)
                FrameCounter69 = 0x0;
        }
    }

    return PacketSize;
}

void WIFIBroadCast::WIFICastDriver::WIFIRecvSinff(std::function<void(VideoPackets *, WirelessInfo, int)> vcallback)
{
    videoCallBack = vcallback;

    RecvThread.reset(new FlowThread(
        [&]()
        {
            std::shared_ptr<uint8_t> dataTmps;
            dataTmps.reset(new uint8_t[SocketMTUMAX]);
            uint8_t *dataTmp = dataTmps.get();
            // WirelessInfo wirelessinfo;
            SocketInjectors[0]->Sniff(dataTmp, SocketMTUMAX);
            // FIXME: must locate every frame one by one
            // From data HeaderSize:
            int size = dataTmp[FrameTypeL - 1];
            size |= dataTmp[FrameTypeL - 2] << 8;
            // FIXME: must deal with data team together
            int FramestreamID = (dataTmp[size - 1] >> 5);
            uint8_t Framesequeue = (dataTmp[size - 1] - (FramestreamID << 5));
            int FrameMarking = dataTmp[size - 2] >> 5;
            int framepacketsize = (dataTmp[size - 2] - (FrameMarking << 5));

            // std::cout << FramestreamID << " " << Framesequeue << " " << FrameMarking << " " << framepacketsize << "\n";

            int LocateID = -1;
            bool PacketNotReg = true;
            // check pack is regsitered and match what we want
            VideoPackets *videoTarget;
            for (size_t i = 0; i < VideoPacketsBuffer.size(); i++)
                if (VideoPacketsBuffer[i].vps.info.frameID == FramestreamID)
                {
                    LocateID = i;
                    PacketNotReg = false;
                    videoTarget = &VideoPacketsBuffer[LocateID];
                    break;
                }
            //
            if (dataTmp[FrameTypeL] == VideoTrans && !PacketNotReg && videoTarget)
            {
                if (videoTarget->vps.currentDataSize <=
                    CAST32(videoTarget->vps.info.maxVideosize))
                {
                errorrecover:
                    if (FrameMarking != videoTarget->vps.currentFrameMark)
                    {
                        // it means frame losing or frame switched, check frame 0x1f
                        if (videoTarget->vps.currentDataSize != 0)
                            goto framefinshed;
                        videoTarget->vps.currentFrameMark = FrameMarking;
                    }
                    // JUST COPY IT, I dont't care
                    std::copy((dataTmp + HeaderSize),
                              (dataTmp + size),
                              videoTarget->videoDataRaw.get() + videoTarget->vps.currentDataSize);
                    videoTarget->vps.currentDataSize += (size - HeaderSize - 2);

                    if (Framesequeue == 0x1f)
                    {
                    framefinshed:
                        // Marking change means tranfer complete
                        if (FrameMarking == videoTarget->vps.currentFrameMark)
                            videoTarget->vps.pakcetAvaliable.push_back(
                                std::tuple<uint8_t, int>{(videoTarget->vps.currentPacketSize - 1), (size - HeaderSize - 2)});

                        videoTarget->videoRawSize = videoTarget->vps.currentDataSize;
                        videoCallBack(videoTarget, {.antenSignal = (int8_t)dataTmp[22], .signalQuality = (dataTmp[24] | (dataTmp[25] << 8))}, FramestreamID);
                        // TODO: add a signal to notify data is ready
                        // FIXME: Direct to wait next frame?
                        videoTarget->vps.pakcetAvaliable.clear();
                        videoTarget->vps.currentDataSize = 0;
                        std::memset(videoTarget->videoDataRaw.get(), 0, videoTarget->videoRawSize);
                        // no 0x1f ending losing packet, just goto deal with anther packet now, because data is dealed
                        if (FrameMarking != videoTarget->vps.currentFrameMark)
                            goto errorrecover;
                    }
                    else
                    {
                        // continue packet
                        videoTarget->vps.currentPacketSize = framepacketsize;
                        videoTarget->vps.pakcetAvaliable.push_back(
                            std::tuple<uint8_t, int>{Framesequeue, (size - HeaderSize - 2)});
                    }
                }
                else
                {
                    goto framereset;
                }
            }
            else if (dataTmp[FrameTypeL] == DataETrans)
            {
                // TODO: Get max video size , width and height from DataEFrame. also reattch ebpf with mac bind.
                if (PacketNotReg == true)
                {
                    if (dataTmp[size - 1] == 0xff)
                    {

                        VideoPacketsInfo packetInfo;
                        std::copy(dataTmp, dataTmp + sizeof(VideoPacketsInfo), (uint8_t *)&packetInfo);
                        //
                        if ((int)packetInfo.frameID >= VideoPacketsBuffer.size())
                        {
                            std::cout << "DataFrameInfo: "
                                      << (int)packetInfo.frameID << " "
                                      << (int)packetInfo.width << " "
                                      << (int)packetInfo.height << " "
                                      << CAST32(packetInfo.maxVideosize) << "\n";
                            // TODO: prepare data area for video data input
                            VideoPackets videopacket;
                            videopacket.vps.info = packetInfo;
                            videopacket.vps.currentDataSize = 0;
                            videopacket.vps.currentFrameMark = 0;
                            videopacket.vps.currentFrameSeq = 0;
                            videopacket.vps.currentPacketSize = 0;
                            // FIXME: should set as a framebuffer?
                            videopacket.videoDataRaw.reset(new uint8_t[CAST32(packetInfo.maxVideosize)]);
                            //
                            VideoPacketsBuffer.push_back(videopacket);
                        }
                    }

                    // TODO: Send a feedBack frame for caculating latency
                    // uint8_t FeedBack[] = {
                    //     FeedBackTrans,
                    //     (uint8_t)FramestreamID,
                    // };
                    // WIFICastInject(FeedBack, sizeof(FeedBack), 0, BroadCastType::DataStream, 0, 0x0);
                }
                else if (Framesequeue == 0xf)
                {
                    // recv a feedBack frame for caculating latency
                    // std::string dataTransfer(dataTmp + HeaderSize, dataTmp + size - 1);
                    // DataEBuffer.push(dataTransfer);
                }
                //
            }
            else
            {
                // Not the Target Frame, throw.
            }

            return;

        framereset:
            videoTarget->vps.dataLose++;
            videoTarget->vps.currentDataSize = 0;
            return;
        }));
}