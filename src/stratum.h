#ifndef __STRATUM_H_
#define __STRATUM_H_

#include "p2putils/xmstream.h"

enum StratumMethodTy {
  Subscribe = 0,
  Authorize,
  ExtraNonceSubscribe,
  Submit,
  Last
};

enum StratumDecodeStatusTy {
  Ok = 0,
  JsonError,
  FormatError
};

struct StratumMiningSubscribe {
  std::string minerUserAgent;
  std::string sessionId;
  std::string connectHost;
  int64_t connectPort;
};

struct StratumAuthorize {
  std::string login;
  std::string password;
};

struct StratumSubmit {
  std::string workerName;
  std::string jobId;
  unsigned time;
  uint8_t nonce[32];
  std::vector<uint8_t> equihashSolution;
};

struct StratumMessage {
  int64_t id;
  StratumMethodTy method;

  StratumMiningSubscribe subscribe;
  StratumAuthorize authorize;
  StratumSubmit submit;
  
  std::string error;
};

StratumDecodeStatusTy decodeStratumMessage(const char *in, StratumMessage *out);
// void encodeStratumMessage(const StratumMessage *in, xmstream &out);

void stratumLittleEndianHex(const char *in, size_t inSize, char *out);

template<typename T>
void stratumDumpHex(T data, char *out) {
  char *o = out;
  for (unsigned i = 0; i < sizeof(T); i++) {
    char digit1 = (data & 0xF);
    char digit2 = (data>>4 & 0xF);
    char hexDigit1 = (digit1 <= 9) ? '0'+digit1 : 'A'+digit1-10;    
    char hexDigit2 = (digit2 <= 9) ? '0'+digit2 : 'A'+digit2-10;
    *o++ = hexDigit2;
    *o++ = hexDigit1;
    data >>= 8;
  }
  *o = 0;
}

#endif //__STRATUM_H_
