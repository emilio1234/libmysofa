/*
 Copyright (c) 2016, Christian Hoene, Symonics GmbH
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 1. Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 3. All advertising materials mentioning features or use of this software
 must display the following acknowledgement:
 This product includes software developed by the Symonics GmbH.
 4. Neither the name of the Symonics GmbH nor the
 names of its contributors may be used to endorse or promote products
 derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 * fractalhead.c
 *
 *  Created on: 19.11.2016
 *      Author: hoene
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "mysofa.h"
#include "reader.h"

static int log2i(int a) {
	return roundf(log2f(a));
}

static int directblockRead(struct READER *reader, struct DATAOBJECT *dataobject, 
		struct FRACTALHEAP *fractalheap) {

	char buf[4], *name, *value;
	int size, offset_size, length_size, err;
	uint8_t typeandversion;
	uint64_t unknown, heap_header_address, block_offset, block_size, offset,
			length, store;
	struct DIR *dir;
	struct MYSOFA_ATTRIBUTE *attr;

    UNUSED(offset);
    UNUSED(block_size);
    UNUSED(block_offset);
    
	/* read signature */
	if (fread(buf, 1, 4, reader->fhd) != 4 || strncmp(buf, "FHDB", 4)) {
		log("cannot read signature of fractal heap indirect block\n");
		return MYSOFA_INVALID_FORMAT;
	}
	log("%08lX %.4s\n", (uint64_t )ftello(reader->fhd) - 4, buf);

	if (fgetc(reader->fhd) != 0) {
		log("object FHDB must have version 0\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}

	// ignore heap_header_address
	fseek(reader->fhd, reader->superblock.size_of_offsets, SEEK_CUR);

	size = (fractalheap->maximum_heap_size + 7) / 8;
	block_offset = readValue(reader, size);

	if (fractalheap->flags & 2)
		if(fseek(reader->fhd, 4, SEEK_CUR))
		    return errno;

	offset_size = ceilf(log2f(fractalheap->maximum_heap_size) / 8);
	if (fractalheap->maximum_direct_block_size < fractalheap->maximum_size)
		length_size = ceilf(log2f(fractalheap->maximum_direct_block_size) / 8);
	else
		length_size = ceilf(log2f(fractalheap->maximum_size) / 8);

	/*
	 * 00003e00  00 46 48 44 42 00 40 02  00 00 00 00 00 00 00 00  |.FHDB.@.........|
	 00003e10  00 00 00 83 8d ac f6 >03  00 0c 00 08 00 04 00 00  |................|
	 00003e20  43 6f 6e 76 65 6e 74 69  6f 6e 73 00 13 00 00 00  |Conventions.....|
	 00003e30  04 00 00 00 02 00 00 00  53 4f 46 41< 03 00 08 00  |........SOFA....|
	 00003e40  08 00 04 00 00 56 65 72  73 69 6f 6e 00 13 00 00  |.....Version....|
	 00003e50  00 03 00 00 00 02 00 00  00 30 2e 36 03 00 10 00  |.........0.6....|
	 00003e60  08 00 04 00 00 53 4f 46  41 43 6f 6e 76 65 6e 74  |.....SOFAConvent|
	 00003e70  69 6f 6e 73 00 13 00 00  00 13 00 00 00 02 00 00  |ions............|
	 00003e80  00 53 69 6d 70 6c 65 46  72 65 65 46 69 65 6c 64  |.SimpleFreeField|
	 00003e90  48 52 49 52 03 00 17 00  08 00 04 00 00 53 4f 46  |HRIR.........SOF|
	 00003ea0  41 43 6f 6e 76 65 6e 74  69 6f 6e 73 56 65 72 73  |AConventionsVers|
	 00003eb0  69 6f 6e 00 13 00 00 00  03 00 00 00 02 00 00 00  |ion.............|
	 *
	 */
	do {
		typeandversion = fgetc(reader->fhd);
		offset = readValue(reader, offset_size);
		length = readValue(reader, length_size);

//		log(" %d %4X %d %8LX\n",typeandversion,offset,length,ftello(reader->fhd));

		/* TODO: for the following part, the specification is incomplete */
		if (typeandversion == 3) {
			/*
			 * this seems to be a name and value pair
			 */

			if (readValue(reader, 5) != 0x0000040008) {
				log("FHDB type 3 unsupported values");
				return MYSOFA_UNSUPPORTED_FORMAT;
			}

			if (!(name = malloc(length)))
				return MYSOFA_NO_MEMORY;
			fread(name, 1, length, reader->fhd);

			if (readValue(reader, 4) != 0x00000013) {
				log("FHDB type 3 unsupported values");
				return MYSOFA_UNSUPPORTED_FORMAT;
			}

			length = readValue(reader, 2);

			unknown = readValue(reader, 6);
			if (unknown == 0x000000020200)
				value = NULL;
			else if (unknown == 0x000000020000) {
				if (!(value = malloc(length + 1))) {
					return MYSOFA_NO_MEMORY;
			    }
				fread(value, 1, length, reader->fhd);
				value[length] = 0;
			} else {
				log("FHDB type 3 unsupported values");
				return MYSOFA_UNSUPPORTED_FORMAT;
			}
			log(" %s = %s\n", name, value);

			attr = malloc(sizeof(struct MYSOFA_ATTRIBUTE));
			attr->name = name;
			attr->value = value;
			attr->next = dataobject->attributes;
			dataobject->attributes = attr;
			
		} else if (typeandversion == 1) {
			/*
			 * pointer to another data object
			 */
			unknown = readValue(reader, 6);
			if (unknown) {
				log("FHDB type 1 unsupported values");
				return MYSOFA_UNSUPPORTED_FORMAT;
			}

			length = fgetc(reader->fhd);
			if (!(name = malloc(length + 1)))
				return MYSOFA_NO_MEMORY;
			fread(name, 1, length, reader->fhd);
			name[length] = 0;

			heap_header_address = readValue(reader,
					reader->superblock.size_of_offsets);

			log("\nfractal head type 1 length %4lX name %s address %lX\n", length, name, heap_header_address);

			dir = malloc(sizeof(struct DIR));
			dir->next = dataobject->directory;
			dataobject->directory = dir;

			store = ftello(reader->fhd);
			if(fseeko(reader->fhd, heap_header_address, SEEK_SET)) 
			    return errno;
			
			err=dataobjectRead(reader, &dir->dataobject,name);
			if(err)
				return err;
				
			fseeko(reader->fhd, store, SEEK_SET);

		} else if (typeandversion != 0) {
			log("fractal head unknown type %d\n", typeandversion);
			return MYSOFA_UNSUPPORTED_FORMAT;
		}

	} while (typeandversion != 0);

	return MYSOFA_OK;
}

/*  III.G. Disk Format: Level 1G - Fractal Heap
 * indirect block
 */

static int indirectblockRead(struct READER *reader, struct DATAOBJECT *dataobject,
		struct FRACTALHEAP *fractalheap, uint64_t iblock_size) {
	int size, nrows, max_dblock_rows, k, n, err;
	uint32_t filter_mask;
	uint64_t store, heap_header_address, block_offset, child_direct_block,
			size_filtered, child_indirect_block;

	char buf[4];

    UNUSED(size_filtered);
    UNUSED(heap_header_address);
    UNUSED(filter_mask);

	/* read signature */
	if (fread(buf, 1, 4, reader->fhd) != 4 || strncmp(buf, "FHIB", 4)) {
		log("cannot read signature of fractal heap indirect block\n");
		return MYSOFA_INVALID_FORMAT;
	}
	log("%08lX %.4s\n", (uint64_t )ftello(reader->fhd) - 4, buf);

	if (fgetc(reader->fhd) != 0) {
		log("object FHIB must have version 0\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}

	/* ignore it */
	heap_header_address = readValue(reader, reader->superblock.size_of_offsets);

	size = (fractalheap->maximum_heap_size + 7) / 8;
	block_offset = readValue(reader, size);

	if (block_offset) {
		log("FHIB block offset is not 0\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}

	//	 The number of rows of blocks, nrows, in an indirect block of size iblock_size is given by the following expression:
	nrows = (log2i(iblock_size) - log2i(fractalheap->starting_block_size)) + 1;

	// The maximum number of rows of direct blocks, max_dblock_rows, in any indirect block of a fractal heap is given by the following expression:
	max_dblock_rows = (log2i(fractalheap->maximum_direct_block_size)
			- log2i(fractalheap->starting_block_size)) + 2;

	// Using the computed values for nrows and max_dblock_rows, along with the Width of the doubling table, the number of direct and indirect block entries (K and N in the indirect block description, below) in an indirect block can be computed:
	if (nrows < max_dblock_rows)
		k = nrows * fractalheap->table_width;
	else
		k = max_dblock_rows * fractalheap->table_width;

	// If nrows is less than or equal to max_dblock_rows, N is 0. Otherwise, N is simply computed:
	n = k - (max_dblock_rows * fractalheap->table_width);

	while (k > 0) {
		child_direct_block = readValue(reader,
				reader->superblock.size_of_offsets);
		if (fractalheap->encoded_length > 0) {
			size_filtered = readValue(reader,
					reader->superblock.size_of_lengths);
			filter_mask = readValue(reader, 4);
		}

		if (validAddress(reader, child_direct_block)) {
			store = ftello(reader->fhd);
			fseeko(reader->fhd, child_direct_block, SEEK_SET);
			err = directblockRead(reader, dataobject, fractalheap);
			if (err)
				return err;
			fseeko(reader->fhd, store, SEEK_SET);
		}

		k--;
	}

	while (n > 0) {
		child_indirect_block = readValue(reader,
				reader->superblock.size_of_offsets);

		if (validAddress(reader, child_direct_block)) {
			store = ftello(reader->fhd);
			fseeko(reader->fhd, child_indirect_block, SEEK_SET);
			err = indirectblockRead(reader, dataobject, fractalheap, iblock_size * 2);
			if (err)
				return err;
			fseeko(reader->fhd, store, SEEK_SET);
		}

		n--;
	}

	return MYSOFA_OK;
}

/*  III.G. Disk Format: Level 1G - Fractal Heap

 00000240  46 52 48 50 00 08 00 00  00 02 00 10 00 00 00 00  |FRHP............|
 00000250  00 00 00 00 00 00 ff ff  ff ff ff ff ff ff a3 0b  |................|
 00000260  00 00 00 00 00 00 1e 03  00 00 00 00 00 00 00 10  |................|
 00000270  00 00 00 00 00 00 00 08  00 00 00 00 00 00 00 08  |................|
 00000280  00 00 00 00 00 00 16 00  00 00 00 00 00 00 00 00  |................|
 00000290  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  |................|
 000002a0  00 00 00 00 00 00 00 00  00 00 00 00 00 00 04 00  |................|
 000002b0  00 04 00 00 00 00 00 00  00 00 01 00 00 00 00 00  |................|
 000002c0  28 00 01 00 29 32 00 00  00 00 00 00 01 00 60 49  |(...)2........`I|
 000002d0  32 1d 42 54 48 44 00 08  00 02 00 00 11 00 00 00  |2.BTHD..........|

 */

int fractalheapRead(struct READER *reader, struct DATAOBJECT *dataobject, struct FRACTALHEAP *fractalheap) {
	int err;
	char buf[4];

	/* read signature */
	if (fread(buf, 1, 4, reader->fhd) != 4 || strncmp(buf, "FRHP", 4)) {
		log("cannot read signature of fractal heap\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}
	log("%08lX %.4s\n", (uint64_t )ftello(reader->fhd) - 4, buf);

	if (fgetc(reader->fhd) != 0) {
		log("object fractal heap must have version 0\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}

	fractalheap->heap_id_length = readValue(reader, 2);
	fractalheap->encoded_length = readValue(reader, 2);
	fractalheap->flags = fgetc(reader->fhd);
	fractalheap->maximum_size = readValue(reader, 4);

	fractalheap->next_huge_object_id = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->btree_address_of_huge_objects = readValue(reader,
			reader->superblock.size_of_offsets);
	fractalheap->free_space = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->address_free_space = readValue(reader,
			reader->superblock.size_of_offsets);
	fractalheap->amount_managed_space = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->amount_allocated_space = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->offset_managed_space = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->number_managed_objects = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->size_huge_objects = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->number_huge_objects = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->size_tiny_objects = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->number_tiny_objects = readValue(reader,
			reader->superblock.size_of_lengths);

	fractalheap->table_width = readValue(reader, 2);

	fractalheap->starting_block_size = readValue(reader,
			reader->superblock.size_of_lengths);
	fractalheap->maximum_direct_block_size = readValue(reader,
			reader->superblock.size_of_lengths);

	fractalheap->maximum_heap_size = readValue(reader, 2);
	fractalheap->starting_row = readValue(reader, 2);

	fractalheap->address_of_root_block = readValue(reader,
			reader->superblock.size_of_offsets);

	fractalheap->current_row = readValue(reader, 2);

	if (fractalheap->encoded_length > 0) {

		fractalheap->size_of_filtered_block = readValue(reader,
				reader->superblock.size_of_lengths);
		fractalheap->fitler_mask = readValue(reader, 4);

		fractalheap->filter_information = malloc(fractalheap->encoded_length);
		if (!fractalheap->filter_information)
			return MYSOFA_NO_MEMORY;

		fread(fractalheap->filter_information, 1, fractalheap->encoded_length,
				reader->fhd);
	}

	fseek(reader->fhd, 4, SEEK_CUR); /* skip checksum */

	if (fractalheap->number_huge_objects) {
		log("cannot handle huge objects\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}

	if (fractalheap->number_tiny_objects) {
		log("cannot handle tiny objects\n");
		return MYSOFA_UNSUPPORTED_FORMAT;
	}

	if (validAddress(reader, fractalheap->address_of_root_block)) {

		fseeko(reader->fhd, fractalheap->address_of_root_block, SEEK_SET);
		if (fractalheap->current_row)
			err = indirectblockRead(reader, dataobject, fractalheap,
					fractalheap->starting_block_size);
		else {
			err = directblockRead(reader, dataobject, fractalheap);

		}
		if (err)
			return err;
	}

	return MYSOFA_OK;
}

void fractalheapFree(struct FRACTALHEAP *fractalheap) {
	free(fractalheap->filter_information);
}

