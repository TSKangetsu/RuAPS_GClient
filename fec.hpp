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
