
#pragma once
#include <tuple>
#include <vector>
#include <queue>
#include <condition_variable>
#include "Drive_Socket.hpp"
#include "FlowController.hpp"
#define FrameTypeL 58
#define VideoTrans 0x68
#define DataETrans 0x69
#define FeedBackTrans 0x77
#define SocketMTU 1490 - 5 //FCS auto add by driver or kerenl
#define HeaderSize 61
#define SocketMTUMAX 1500
#define LOSEMAXPRE 100

#define DATA_STREAMID 0
#define DATA_TMPSIZE 1
#define DATA_WIDTH 2
#define DATA_HEIGHT 3
#define DATA_FRAMESEQ 4
#define DATA_SIZENOW 5
#define DATA_SIZEMAX 6
#define DATA_LOSE 7
#define DATA_LOSEPRE 8

namespace WIFIBroadCast
{
    enum BroadCastType
    {
        VideoStream,
        DataStream,
    };

    class WIFICastDriver
    {
    public:
        std::queue<std::string> DataEBuffer;

        WIFICastDriver(std::vector<std::string> Interfaces);
        ~WIFICastDriver();

        void WIFICastInject(uint8_t *data, int size, int InterfaceID, BroadCastType type, int delayUS, uint8_t FrameQueueID);

        void WIFICastInjectMulti(uint8_t *data, int size, int delayUS){};
        void WIFICastInjectMultiBL(uint8_t *data, int size, int delayUS){};
        //
        void WIFIRecvSinff();
        int WIFIRecvVideoSeq() { return VideoFullPackets.size(); }
        std::tuple<unsigned char *, int *, bool> WIFIRecvVideoDMA(int ID) { return VideoFullPackets[ID]; };

        void WIFIRecvWaitFrame();

    private:
        struct InjectPacketLLCInfo
        {
            const uint8_t RadioHeader[16] = {
                0x00, 0x00, 0x20, 0x00, 0xae, 0x40, 0x00, 0xa0,
                0x20, 0x08, 0x00, 0xa0, 0x20, 0x08, 0x00, 0x00};
            // Auto Complete by driver or kerenl
            const uint8_t RadioInfo[16] = {
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
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

        bool IsFrameGet = false;
        std::mutex copylock;
        std::condition_variable notifyWait;

        int InterfaceCount = 0;
        uint8_t FrameCounter68 = 0;
        uint8_t FrameCounter69 = 0;
        uint8_t **PacketVideo;
        uint8_t **PacketDatae;
        std::vector<sock_filter> RecvFilter;
        std::vector<std::unique_ptr<Socket>> SocketInjectors;

        std::unique_ptr<FlowThread> RecvThread;
        std::vector<std::tuple<unsigned char *, int *, bool>> VideoFullPackets;
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

void WIFIBroadCast::WIFICastDriver::WIFICastInject(uint8_t *data, int len, int InterfaceID, BroadCastType type, int delayUS, uint8_t FrameQueueID)
{
    float PacketSize = (((float)len / (float)(SocketMTU - HeaderSize)) == ((int)(len / (SocketMTU - HeaderSize))))
                           ? ((int)(len / (SocketMTU - HeaderSize)))
                           : ((int)(len / (SocketMTU - HeaderSize))) + 1;
    for (size_t i = 0; i < PacketSize; i++)
    {
        uint8_t tmpData[SocketMTU + 1] = {0x00};
        if (type == BroadCastType::VideoStream)
            std::copy(PacketVideo[InterfaceID], PacketVideo[InterfaceID] + HeaderSize, tmpData);
        if (type == BroadCastType::DataStream)
            std::copy(PacketDatae[InterfaceID], PacketDatae[InterfaceID] + HeaderSize, tmpData);

        if (!(((float)len / (float)(SocketMTU - HeaderSize)) == ((int)(len / (SocketMTU - HeaderSize)))) && i == (PacketSize - 1))
        {
            int size = ((SocketMTU - HeaderSize) - (PacketSize * (SocketMTU - HeaderSize) - len));
            //
            if (type == BroadCastType::VideoStream)
                tmpData[((size + HeaderSize))] = FrameQueueID << 4 | 0xf;
            if (type == BroadCastType::DataStream)
                tmpData[((size + HeaderSize))] = FrameQueueID << 4 | 0xf;
            //
            tmpData[FrameTypeL - 1] = (size + HeaderSize + 1);
            tmpData[FrameTypeL - 2] = (size + HeaderSize + 1) >> 8;
            //
            int dataStart = (i * (SocketMTU - HeaderSize));
            int dataEnd = (i * (SocketMTU - HeaderSize)) + size;
            std::copy(data + dataStart, data + dataEnd, tmpData + HeaderSize);
            SocketInjectors[InterfaceID]->Inject(tmpData, (size + HeaderSize + 1));
        }
        else
        {
            //
            if (type == BroadCastType::VideoStream)
                tmpData[(SocketMTU)] = FrameQueueID << 4 | FrameCounter68;
            if (type == BroadCastType::DataStream)
                tmpData[(SocketMTU)] = FrameQueueID << 4 | FrameCounter69;
            //
            tmpData[(FrameTypeL - 1)] = (uint8_t)(SocketMTU + 1);
            tmpData[(FrameTypeL - 2)] = (uint8_t)((SocketMTU + 1) >> 8);
            std::copy((data + (i * (SocketMTU - HeaderSize))), (data + (i * (SocketMTU - HeaderSize))) + (SocketMTU - HeaderSize), tmpData + HeaderSize);
            SocketInjectors[InterfaceID]->Inject(tmpData, SocketMTU + 1);
        }
        if (delayUS)
            usleep(delayUS);

        if (type == BroadCastType::VideoStream && PacketSize - 1 != i)
        {
            FrameCounter68++;
            if (FrameCounter68 >= 0xf)
                FrameCounter68 = 0x0;
        }
        else if (type == BroadCastType::DataStream && PacketSize - 1 != i)
        {
            FrameCounter69++;
            if (FrameCounter69 >= 0xf)
                FrameCounter69 = 0x0;
        }
    }
}

void WIFIBroadCast::WIFICastDriver::WIFIRecvSinff()
{
    RecvThread.reset(new FlowThread(
        [&]()
        {
            uint8_t dataTmp[SocketMTUMAX] = {0x00};
            SocketInjectors[0]->Sniff(dataTmp, SocketMTUMAX);
            // From data HeaderSize:
            int size = dataTmp[FrameTypeL - 1];
            size |= dataTmp[FrameTypeL - 2] << 8;

            int FramestreamID = (dataTmp[size - 1] >> 4);
            int Framesequeue = (dataTmp[size - 1] - (FramestreamID << 4));
            //
            int LocateID = -1;
            bool PacketNotReg = true;
            for (size_t i = 0; i < VideoFullPackets.size(); i++)
                if (std::get<int *>(VideoFullPackets[i])[DATA_STREAMID] == FramestreamID || std::get<int *>(VideoFullPackets[i])[DATA_STREAMID] == dataTmp[HeaderSize])
                {
                    LocateID = i;
                    PacketNotReg = false;
                }
            //
            if (dataTmp[FrameTypeL] == VideoTrans && !PacketNotReg)
            {
                //Step 1: locate 0xf signal for the frame start
                if (Framesequeue == 0xf)
                {
                    if (!std::get<bool>(VideoFullPackets[LocateID]))
                    {
                        //Step 2: if frame is not located before , set frame size 0 and start recving
                        std::get<bool>(VideoFullPackets[LocateID]) = true;
                        std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] = 0;
                    }
                    else
                    {
                        //Step 4: finsh a frame transision.
                        if ((std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] + (size - HeaderSize - 1)) <= std::get<int *>(VideoFullPackets[LocateID])[DATA_SIZEMAX])
                        {
                            std::get<bool>(VideoFullPackets[LocateID]) = false;
                            // copy data to buffer before add tmp size
                            std::copy(dataTmp + HeaderSize, dataTmp + size - 1, std::get<unsigned char *>(VideoFullPackets[LocateID]) + std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE]);
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] += (size - HeaderSize - 1);
                            // Pop out the frame with resize SIZENOW
                            // TODO: add a signal to notify data is ready
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_SIZENOW] = std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE];
                            //
                            std::unique_lock<std::mutex> lockWaitMain{copylock};
                            IsFrameGet = true;
                            notifyWait.notify_all();
                            // FIXME: Direct to wait next frame?
                            std::get<bool>(VideoFullPackets[LocateID]) = true;
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] = 0;
                        }
                        else
                        {
                            std::get<bool>(VideoFullPackets[LocateID]) = false;
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] = 0;
                        }
                    }
                }
                else if (std::get<bool>(VideoFullPackets[LocateID]))
                {
                    //Step 3: check size and copy single frame data
                    if ((std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] + (size - HeaderSize - 1)) <= std::get<int *>(VideoFullPackets[LocateID])[DATA_SIZEMAX])
                    {
                        // copy data to buffer before add tmp size
                        std::copy((dataTmp + HeaderSize), (dataTmp + size - 1), (std::get<unsigned char *>(VideoFullPackets[LocateID]) + std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE]));
                        std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] += (size - HeaderSize - 1);
                        // check Frame sequeue is correct
                        // TODO: deal with frame is disconnect
                        if (Framesequeue == (std::get<int *>(VideoFullPackets[LocateID])[DATA_FRAMESEQ] + 1) || (std::get<int *>(VideoFullPackets[LocateID])[DATA_FRAMESEQ] == 0xe && Framesequeue == 0x0))
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_FRAMESEQ] = Framesequeue;
                        else
                        {
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_LOSE]++;
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_FRAMESEQ] = Framesequeue;
                            //Throw frame if lose
                            //FIXME: should not throw
                            std::get<bool>(VideoFullPackets[LocateID]) = false;
                            std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] = 0;
                        }
                    }
                    else
                    {
                        std::get<bool>(VideoFullPackets[LocateID]) = false;
                        std::get<int *>(VideoFullPackets[LocateID])[DATA_TMPSIZE] = 0;
                    }
                }
            }
            else if (dataTmp[FrameTypeL] == DataETrans)
            {
                //TODO: Get max video size , width and height from DataEFrame. also reattch ebpf with mac bind.
                // 1 btye: FrameID, 4 byte: Max size, 2 btye: width, 2 byte: height
                if (dataTmp[size - 1] == 0xff)
                {
                    if (PacketNotReg)
                    {
                        int FrameID = dataTmp[HeaderSize];
                        int ssize = (dataTmp[HeaderSize + 1] << 24) | (dataTmp[HeaderSize + 2] << 16) | (dataTmp[HeaderSize + 3] << 8) | (dataTmp[HeaderSize + 4]);
                        int width = dataTmp[HeaderSize + 5] << 8 | dataTmp[HeaderSize + 6];
                        int height = dataTmp[HeaderSize + 7] << 8 | dataTmp[HeaderSize + 8];
                        if (ssize > 0)
                        {
                            int FrameInfo[] = {FrameID, ssize, width, height, 0, 0, ssize, 0, 0};
                            VideoFullPackets.push_back(std::make_tuple(new unsigned char[ssize], FrameInfo, false));
                        }
                    }
                    // Send a feedBack frame for caculating latency
                    // uint8_t FeedBack[] = {
                    //     FeedBackTrans,
                    //     (uint8_t)FramestreamID,
                    // };
                    // WIFICastInject(FeedBack, sizeof(FeedBack), 0, BroadCastType::DataStream, 0, 0x0);
                }
                else if (Framesequeue == 0xf)
                {
                    // recv a feedBack frame for caculating latency
                    std::string dataTransfer(dataTmp + HeaderSize, dataTmp + size - 1);
                    DataEBuffer.push(dataTransfer);
                }
                //
            }
            else
            {
                // Not the Target Frame, throw.
            }
        }));
}

void WIFIBroadCast::WIFICastDriver::WIFIRecvWaitFrame()
{
    std::unique_lock<std::mutex> lockWait{copylock};
    while (!IsFrameGet)
        notifyWait.wait(lockWait);

    IsFrameGet = false;
};