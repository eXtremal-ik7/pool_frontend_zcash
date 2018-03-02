#include "poolcore/base58.h"

class CZECAddress : public CBase58Data {
private:
  enum Base58Type {
    PUBKEY_ADDRESS,
    SCRIPT_ADDRESS,
    SECRET_KEY,
    EXT_PUBLIC_KEY,
    EXT_SECRET_KEY,

    ZCPAYMENT_ADDRRESS,
    ZCSPENDING_KEY,

    MAX_BASE58_TYPES
  };  
  
public:
  bool SetString(const char* pszAddress) { CBase58Data::SetString(pszAddress, 2); }
  bool SetString(const std::string& strAddress) { SetString(strAddress.c_str()); }
  CZECAddress() {}
  CZECAddress(const std::string& strAddress) { SetString(strAddress); }
  CZECAddress(const char* pszAddress) { SetString(pszAddress); }
  
  bool isValid() {
    std::vector<unsigned char> base58Prefixes[MAX_BASE58_TYPES];
    // guarantees the first 2 characters, when base58 encoded, are "t1"
    base58Prefixes[PUBKEY_ADDRESS]     = {0x1C,0xB8};
    // guarantees the first 2 characters, when base58 encoded, are "t3"
    base58Prefixes[SCRIPT_ADDRESS]     = {0x1C,0xBD};
    // the first character, when base58 encoded, is "5" or "K" or "L" (as in Bitcoin)
    base58Prefixes[SECRET_KEY]         = {0x80};
    // do not rely on these BIP32 prefixes; they are not specified and may change
    base58Prefixes[EXT_PUBLIC_KEY]     = {0x04,0x88,0xB2,0x1E};
    base58Prefixes[EXT_SECRET_KEY]     = {0x04,0x88,0xAD,0xE4};
    // guarantees the first 2 characters, when base58 encoded, are "zc"
    base58Prefixes[ZCPAYMENT_ADDRRESS] = {0x16,0x9A};
    // guarantees the first 2 characters, when base58 encoded, are "SK"
    base58Prefixes[ZCSPENDING_KEY]     = {0xAB,0x36};  
  
    bool fCorrectSize = vchData.size() == 20;
    bool fKnownVersion = vchVersion == base58Prefixes[PUBKEY_ADDRESS] ||
                         vchVersion == base58Prefixes[SCRIPT_ADDRESS];
    return fCorrectSize && fKnownVersion;
  }
};
