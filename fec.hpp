#pragma once
#include <cstdint>

typedef struct fec_parms *fec_code_t;

template <int Size1D, int Group2D, int GroupPackets2D>
struct FecPacket
{
	uint8_t *dataout[Group2D];

	FecPacket()
	{
		for (size_t i = 0; i < Group2D; i++)
		{
			dataout[i] = FecDataType_t.data2d[i];
		}
	};

	union FecDataType
	{
		uint8_t data1d[Size1D];
		uint8_t data2d[Group2D][GroupPackets2D];
	} FecDataType_t;
};

/*
 * create a new encoder, returning a descriptor. This contains k,n and
 * the encoding matrix.
 * n is the number of data blocks + fec blocks (matrix height)
 * k is just the data blocks (matrix width)
 */
void fec_init(void);

void fec_encode(unsigned int blockSize,
				unsigned char **data_blocks,
				unsigned int nrDataBlocks,
				unsigned char **fec_blocks,
				unsigned int nrFecBlocks);

void fec_decode(unsigned int blockSize,
				unsigned char **data_blocks,
				unsigned int nr_data_blocks,
				unsigned char **fec_blocks,
				unsigned int *fec_block_nos,
				unsigned int *erased_blocks,
				unsigned short nr_fec_blocks /* how many blocks per stripe */);

void fec_print(fec_code_t code, int width);

void fec_license(void);

// LEFT a test unit here
// #include <iostream>
// #include <unistd.h>
// #include "fec.hpp"
// #include <bitset>
// #include <climits>
// #include <cstring>
// #include <iostream>

// int main(int argc, char const *argv[])
// {
//     // TEST PASSED
//     fec_init();

//     FecPacket<2500, 5, 500> dataPool;
//     FecPacket<2500, 5, 500> fecPool;
//     FecPacket<2500, 5, 500> fecPoolUse;
//     FecPacket<2500, 5, 500> dataPoolFake;

//     std::memset(fecPool.FecDataType_t.data1d, 0x00, 2500);
//     std::memset(dataPool.FecDataType_t.data1d, 0x00, 2500);
//     std::memset(dataPool.FecDataType_t.data1d, 0xFF, 500);
//     std::memset(dataPoolFake.FecDataType_t.data1d, 0x00, 2500);
//     std::memset(dataPoolFake.FecDataType_t.data1d + 1000, 0xFF, 500);
//     dataPool.FecDataType_t.data1d[15] = 0x7F;

//     for (size_t i = 0; i < 2500; i++)
//     {
//         std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data1d[i] << " ";
//     }
//     std::cout << '\n';
//     std::cout << '\n';
//     for (size_t i = 0; i < 5; i++)
//     {
//         for (size_t k = 0; k < 500; k++)
//         {
//             std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data2d[i][k] << " ";
//         }
//     }
//     std::cout << '\n';
//     std::cout << '\n';

//     fec_encode(500, dataPool.dataout, 5, fecPool.dataout, 5);

//     unsigned int fecnos[] = {0, 1, 0, 0, 0};
//     unsigned int earnos[] = {0, 2, 0, 0, 0};
//     std::memcpy(fecPoolUse.FecDataType_t.data2d[0],
//                 fecPool.FecDataType_t.data2d[0],
//                 sizeof(fecPool.FecDataType_t.data2d[0]));
//     std::memcpy(fecPoolUse.FecDataType_t.data2d[1],
//                 fecPool.FecDataType_t.data2d[1],
//                 sizeof(fecPool.FecDataType_t.data2d[2]));

//     fec_decode(500, dataPoolFake.dataout, 5,
//                fecPoolUse.dataout,
//                fecnos, earnos,
//                2);

//     for (size_t i = 0; i < 2500; i++)
//     {
//         std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data1d[i] << " ";
//     }
//     std::cout << '\n';
//     std::cout << '\n';
//     for (size_t i = 0; i < 5; i++)
//     {
//         for (size_t k = 0; k < 500; k++)
//         {
//             std::cout << std::hex << (int)dataPoolFake.FecDataType_t.data2d[i][k] << " ";
//         }
//     }
//     std::cout << '\n';

//     return 0;
// }