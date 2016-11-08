class zmtpStream;

enum zmtpMsgTy {
  zmtpError = -1,
  zmtpShortMsgLast = 0,
  zmtpShortMsgMore = 1,
  zmtpLongMsgLast = 2,
  zmtpLongMsgMore = 3,
  zmtpShortCmd = 4,
  zmtpLongCmd = 6
};

inline bool zmtpIsCommand(zmtpMsgTy type) {
  return type == zmtpShortCmd || type == zmtpLongCmd;
}

inline bool zmtpIsMessage(zmtpMsgTy type) {
  return type == zmtpShortMsgLast || type == zmtpShortMsgMore || type == zmtpLongMsgLast || type == zmtpLongMsgMore;
}

void aioZmtpSendMessage(asyncBase *base, aioObject *socket, uint64_t timeout, void *data, size_t size, bool more, aioCb callback, void *arg);

bool ioZmtpAccept(asyncBase *base, aioObject *socket, uint64_t timeout);
zmtpMsgTy ioZmtpRecv(asyncBase *base, aioObject *socket, uint64_t timeout, zmtpStream *msg, size_t limit);
bool ioZmtpSendCommand(asyncBase *base, aioObject *socket, uint64_t timeout, void *data, size_t size);
bool ioZmtpSendMessage(asyncBase *base, aioObject *socket, uint64_t timeout, void *data, size_t size, bool more);
