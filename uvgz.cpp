/* uvgz.cpp

   A sample of the generation process for block type 2. It's probably better
   not to use this as starter code (but you can if you want), since it is not
   written sustainably (just as a proof of concept).

   This basic implementation generates a fully compliant .gz output stream,
   using block mode 2 and limiting the uncompressed size of each block to 
   100000 bytes (which is a completely arbitrary limit).

   B. Bird - 2023-05-12
*/
#include <iostream>
#include <vector>
#include <array>
#include <unordered_map>
#include <string>
#include <cassert>
#include "output_stream.hpp"

// To compute CRC32 values, we can use this library
// from https://github.com/d-bahr/CRCpp
#define CRCPP_USE_CPP11
#include "CRC.h"

const int BLOCK_MAX = 100000;

using Block = std::array<u8, BLOCK_MAX>;



void write_cl_data(OutputBitStream& stream, std::vector<u32> const& ll_code_lengths, std::vector<u32> const& dist_code_lengths ){
    //This function should be easy to port into your own code to start you off
    //The construct_canonical_code function below might also be useful.
    //The rest of the code in this file is probably useless except as a point of reference.

    //This is a very basic implementation of the encoding logic for the block 2 header. There are plenty of ways
    //the scheme used here can be improved to use fewer bits.
    //In particular, this implementation does not use the clever CL coding (where a prefix code is generated
    //for the code length tables) at all.

    //Variables are named as in RFC 1951
    assert(ll_code_lengths.size() >= 257); //There needs to be at least one use of symbol 256, so the ll_code_lengths table must have at least 257 elements
    unsigned int HLIT = ll_code_lengths.size() - 257;

    unsigned int HDIST = 0;
    if (dist_code_lengths.size() == 0){
        //Even if no distance codes are used, we are required to encode at least one.
    }else{
        HDIST = dist_code_lengths.size() - 1;
    }

    //This is where we would compute a proper CL prefix code.

    //We will use a fixed CL code that uses 4 bits for values 0 - 13 and 5 bits for everything else
    //(including the RLE symbols, which we do not use).
    unsigned int HCLEN = 15; // = 19 - 4 (since we will provide 19 CL codes, whether or not they get used)
    
    //Push HLIT, HDIST and HCLEN. These are all numbers so Rule #1 applies
    stream.push_bits(HLIT, 5);
    stream.push_bits(HDIST,5);
    stream.push_bits(HCLEN,4);

    std::vector<u32> cl_code_lengths {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5};

    //The lengths are written in a strange order, dictated by RFC 1951
    //(This seems like a sadistic twist of the knife, but there is some amount of weird logic behind the ordering)
    std::vector<u32> cl_permutation {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    //Now push each CL code length in 3 bits (the lengths are numbers, so Rule #1 applies)
    assert(HCLEN+4 == 19);
    for (unsigned int i = 0; i < HCLEN+4; i++)
        stream.push_bits(cl_code_lengths.at(cl_permutation.at(i)),3);
    
    //Now push the LL code lengths, using the CL symbols
    //Notice that we just push each length as a literal CL value, without even using the 
    //RLE features of the CL encoding.
    for (auto len: ll_code_lengths){
        assert(len <= 15); //Lengths must be at most 15 bits
        unsigned int cl_symbol = len; 
        //Push the CL symbol as a 5 bit value, MSB first
        //(If we had computed a real CL prefix code, we would use it here instead)
        for(int i = 3; i >= 0; i--)
            stream.push_bit((cl_symbol>>(unsigned int)i)&1);
    }

    //Same for distance code lengths
    if (dist_code_lengths.size() == 0){
        //If no distance codes were used, just push a length of zero as the only code length
        stream.push_bits(0,5);
    }else{
        for (auto len: dist_code_lengths){
            assert(len <= 15); //Lengths must be at most 15 bits
            unsigned int cl_symbol = len; 
            for(int i = 3; i >= 0; i--)
                stream.push_bit((cl_symbol>>(unsigned int)i)&1);
        }
    }

}




//Given a vector of lengths where lengths.at(i) is the code length for symbol i,
//returns a vector V of unsigned int values, such that the lower lengths.at(i) bits of V.at(i)
//comprise the bit encoding for symbol i (using the encoding construction given in RFC 1951). Note that the encoding is in 
//MSB -> LSB order (that is, the first bit of the prefix code is bit number lengths.at(i) - 1 and the last bit is bit number 0).
//The codes for symbols with length zero are undefined.
std::vector< u32 > construct_canonical_code( std::vector<u32> const & lengths ){

    unsigned int size = lengths.size();
    std::vector< unsigned int > length_counts(16,0); //Lengths must be less than 16 for DEFLATE
    u32 max_length = 0;
    for(auto i: lengths){
        assert(i <= 15);
        length_counts.at(i)++;
        max_length = std::max(i, max_length);
    }
    length_counts[0] = 0; //Disregard any codes with alleged zero length

    std::vector< u32 > result_codes(size,0);

    //The algorithm below follows the pseudocode in RFC 1951
    std::vector< unsigned int > next_code(size,0);
    {
        //Step 1: Determine the first code for each length
        unsigned int code = 0;
        for(unsigned int i = 1; i <= max_length; i++){
            code = (code+length_counts.at(i-1))<<1;
            next_code.at(i) = code;
        }
    }
    {
        //Step 2: Assign the code for each symbol, with codes of the same length being
        //        consecutive and ordered lexicographically by the symbol to which they are assigned.
        for(unsigned int symbol = 0; symbol < size; symbol++){
            unsigned int length = lengths.at(symbol);
            if (length > 0)
                result_codes.at(symbol) = next_code.at(length)++;
        }  
    } 
    return result_codes;
}


void write_block(OutputBitStream& stream, Block& block_data, u32 block_size, bool is_last){
    stream.push_bit(is_last?1:0); //1 = last block
    stream.push_bits(2, 2); //Two bit block type (in this case, block type 2)

    //We will construct placeholder LL and distance codes
    std::vector<u32> ll_code_lengths {};
    //Construct a basic code with 0 - 225 having length 8 and 226 - 285 having length 9
    //(This will satisfy the Kraft-McMillan inequality exactly, and thereby fool gzip's
    // detection process for suboptimal codes)
    for(unsigned int i = 0; i <= 225; i++)
        ll_code_lengths.push_back(8);
    for(unsigned int i = 226; i <= 285; i++)
        ll_code_lengths.push_back(9);
    
    std::vector<u32> dist_code_lengths {};
    //Construct a distance code similarly, with 0 - 1 having length 4 and 2 - 29 having length 5
    //(This is irrelevant since we don't actually use distance codes in this example)
    for(unsigned int i = 0; i <= 1; i++)
        dist_code_lengths.push_back(4);
    for(unsigned int i = 2; i <= 29; i++)
        dist_code_lengths.push_back(5);

    auto ll_code = construct_canonical_code(ll_code_lengths);
    auto dist_code = construct_canonical_code(dist_code_lengths);

    write_cl_data(stream,ll_code_lengths,dist_code_lengths);

    //Now write each value.
    //Since we didn't do LZSS encoding (which would normally occur before computing the LL
    //and distance codes), we will just write everything as a literal. As mentioned above,
    //we created a dummy LL code that will result in each symbol's encoding being the 9-bit
    //binary representation of that symbol, MSB first. Since everything is a literal,
    //(and therefore fits in 8 bits), this will guarantee that no compression will occur.
    for(u32 k = 0; k < block_size; k++){
        u32 symbol = block_data.at(k);
        u32 bits = ll_code_lengths.at(symbol);
        u32 code = ll_code.at(symbol);
        for(int i = bits-1; i >= 0; i--)
            stream.push_bit((code>>(unsigned int)i)&1);
    }

    //Throw in a 256 (EOB marker)
    {
        u32 symbol = 256;
        u32 bits = ll_code_lengths.at(symbol);
        u32 code = ll_code.at(symbol);
        for(int i = bits-1; i >= 0; i--)
            stream.push_bit((code>>(unsigned int)i)&1);
    }

}


int main(){

    //See output_stream.hpp for a description of the OutputBitStream class
    OutputBitStream stream {std::cout};

    //Pre-cache the CRC table
    auto crc_table = CRC::CRC_32().MakeTable();

    //Push a basic gzip header
    stream.push_bytes( 0x1f, 0x8b, //Magic Number
        0x08, //Compression (0x08 = DEFLATE)
        0x00, //Flags
        0x00, 0x00, 0x00, 0x00, //MTIME (little endian)
        0x00, //Extra flags
        0x03 //OS (Linux)
    );


    //This starter implementation writes a series of blocks with type 0 (store only)
    //Each store-only block can contain up to 2**16 - 1 bytes of data.
    //(This limit does NOT apply to block types 1 and 2)
    //Since we have to keep track of how big each block is (and whether any more blocks 
    //follow it), we have to save up the data for each block in an array before writing it.
    

    //Note that the types u8, u16 and u32 are defined in the output_stream.hpp header
    Block block_contents {};
    u32 block_size {0};
    u32 bytes_read {0};

    char next_byte {}; //Note that we have to use a (signed) char here for compatibility with istream::get()

    //We need to see ahead of the stream by one character (e.g. to know, once we fill up a block,
    //whether there are more blocks coming), so at each step, next_byte will be the next byte from the stream
    //that is NOT in a block.

    //Keep a running CRC of the data we read.
    u32 crc {};


    if (!std::cin.get(next_byte)){
        //Empty input?
        
    }else{

        bytes_read++;
        //Update the CRC as we read each byte (there are faster ways to do this)
        crc = CRC::Calculate(&next_byte, 1, crc_table); //This call creates the initial CRC value from the first byte read.
        //Read through the input
        while(1){
            block_contents.at(block_size++) = next_byte;
            if (!std::cin.get(next_byte))
                break;

            bytes_read++;
            crc = CRC::Calculate(&next_byte,1, crc_table, crc); //Add the character we just read to the CRC (even though it is not in a block yet)

            //If we get to this point, we just added a byte to the block AND there is at least one more byte in the input waiting to be written.
            if (block_size == block_contents.size()){
                write_block(stream,block_contents,block_size,false);
                block_size = 0;
            }
        }
    }
    //At this point, we've finished reading the input (no new characters remain), and we may have an incomplete block to write.
    if (block_size > 0){
        write_block(stream,block_contents,block_size,true);
        block_size = 0;
    }
    //After the last block, restore byte alignment
    stream.flush_to_byte();

    //Now close out the bitstream by writing the CRC and the total number of bytes stored.
    stream.push_u32(crc);
    stream.push_u32(bytes_read);

    return 0;
}