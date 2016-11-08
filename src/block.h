#include "bignum.h"
#include <openssl/sha.h>
#include "p2putils/xmstream.h"

#pragma pack(push, 1)
struct CBlockHeader {
  // header
  static const size_t HEADER_SIZE=4+32+32+32+4+4+32; // excluding Equihash solution
  static const int32_t CURRENT_VERSION=4;
    
  struct {
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint256 hashReserved;
    uint32_t nTime;
    uint32_t nBits;
  } data;
    
  uint256 nNonce;
  std::vector<unsigned char> nSolution;
};
#pragma pack(pop)
