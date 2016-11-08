#include "asyncio/asyncio.h"
#include "zmtp.h"
#include "zmtpProto.h"

void aioZmtpSendMessage(asyncBase *base, aioObject *socket, uint64_t timeout, void *data, size_t size, bool more, aioCb callback, void *arg)
{
  if (size >= 256) {
    uint8_t X[16];
    X[0] = more ? zmtpLongMsgMore : zmtpLongMsgLast;
    *(uint64_t*)(X+1) = size;
    aioWrite(base, socket, X, 9, afWaitAll, timeout, 0, 0);
  } else {
    uint8_t X[2];
    X[0] = more ? zmtpShortMsgMore : zmtpShortMsgLast;
    X[1] = size;
    aioWrite(base, socket, X, 2, afWaitAll, timeout, 0, 0);
  }
  
  aioWrite(base, socket, data, size, afWaitAll, timeout, callback, arg);
}


bool ioZmtpAccept(asyncBase *base, aioObject *socket, uint64_t timeout)
{
  static uint8_t localSignature[] = {0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F};
  static uint8_t localMajorVersion = 3;
  static uint8_t localGreetingOther[] = {
    0,
    'N', 'U', 'L', 'L', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
  };
  
  uint8_t buffer[128];
  
  // greeting = (10)signature (2)version (20)mechanism (1)as-server (31)filler
    
  // signature
  if (!(ioRead(base, socket, buffer, 10, afWaitAll, timeout) == 10 &&
        buffer[0] == 0xFF &&
        buffer[9] == 0x7F))
    return false;
  ioWrite(base, socket, localSignature, sizeof(localSignature), afWaitAll, timeout);
  
  // major version
  if (!(ioRead(base, socket, buffer, 1, afWaitAll, timeout) == 1))
    return false;  
  ioWrite(base, socket, &localMajorVersion, sizeof(localMajorVersion), afWaitAll, timeout);
  
  // minor version + mechanism as-server filler
  if (!ioRead(base, socket, buffer, 1+20+1+31, afWaitAll, timeout) == 1+20+1+31)
    return false;
  
  return ioWrite(base, socket, localGreetingOther, sizeof(localGreetingOther), afWaitAll, timeout) == 1+20+1+31;
}


zmtpMsgTy ioZmtpRecv(asyncBase *base, aioObject *socket, uint64_t timeout, zmtpStream *msg, size_t limit)
{
  bool success = true;
  uint8_t msgType = zmtpError;
  uint8_t size8 = 0;
  uint64_t size64 = 0;
  size_t size = 0;
  
  success &= (ioRead(base, socket, &msgType, 1, afNone, timeout) == 1);
  if (msgType == zmtpShortMsgLast || msgType == zmtpShortMsgMore || msgType == zmtpShortCmd) {
    success &= (ioRead(base, socket, &size8, 1, afNone, timeout) == 1);
    size = size8;
  } else if (msgType == zmtpLongMsgLast || msgType == zmtpLongMsgMore || msgType == zmtpLongCmd) {
    // TODO: 32bit size_t
    success &= (ioRead(base, socket, &size64, 8, afWaitAll, timeout) == 8);
    size = xhton<uint64_t>(size64);
  } else {
    success = false;
  }

  if (!success || size > limit)
    return zmtpError;

  msg->reset();  
  if (ioRead(base, socket, msg->alloc<void>(size), size, afWaitAll, timeout) != size)
    return zmtpError;
  msg->seekSet(0);
  return (zmtpMsgTy)msgType;
}

bool ioZmtpSendCommand(asyncBase *base, aioObject *socket, uint64_t timeout, void *data, size_t size)
{
  if (size >= 256) {
    uint8_t X[16];
    X[0] = 6;
    *(uint64_t*)(X+1) = size;
    if (ioWrite(base, socket, X, 9, afWaitAll, timeout) != 9)
      return false;
  } else {
    uint8_t X[2];
    X[0] = 4;
    X[1] = size;
    if (ioWrite(base, socket, X, 2, afWaitAll, timeout) != 2)
      return false;
  }
  
  return ioWrite(base, socket, data, size, afWaitAll, timeout) == size;
}

bool ioZmtpSendMessage(asyncBase *base, aioObject *socket, uint64_t timeout, void *data, size_t size, bool more)
{
  if (size >= 256) {
    uint8_t X[16];
    X[0] = more ? zmtpLongMsgMore : zmtpLongMsgLast;
    *(uint64_t*)(X+1) = size;
    if (ioWrite(base, socket, X, 9, afWaitAll, timeout) != 9)
      return false;
  } else {
    uint8_t X[2];
    X[0] = more ? zmtpShortMsgMore : zmtpShortMsgLast;
    X[1] = size;
    if (ioWrite(base, socket, X, 2, afWaitAll, timeout) != 2)
      return false;
  }
  
  return ioWrite(base, socket, data, size, afWaitAll, timeout) == size;
}
