#include "stratum.h"
#include "protocol.pb.h"
#include <vector>
#include <gmpxx.h>
#include "uint256.h"

struct asyncBase;
struct aioObject;
class p2pNode;
class PoolBackend;

struct StratumWorker {
private:
  enum {
    AccumulateInterval = 60,
    Interval = 9,
  };  
  
  time_t begin;
  unsigned values[Interval];
  
  void shift() {
    time_t currentTime = time(0)+AccumulateInterval;
    if (currentTime - begin >= AccumulateInterval) {
      unsigned shiftCount = (currentTime-begin)/AccumulateInterval;
      if (shiftCount < Interval) {
        for (unsigned i = 0; i < Interval-shiftCount; i++)
          values[Interval-i-1] = values[Interval-shiftCount-i-1];
        for (unsigned i = 0; i < shiftCount; i++)
          values[i] = 0;
      } else {
        memset(values, 0, sizeof(values));
      }
      
      begin = currentTime;
    }
  }  
  
public:
  std::string wallet;
  std::string worker;
  aioObject *socket;
  
  StratumWorker() {
    begin = time(0)+AccumulateInterval;
    memset(values, 0, sizeof(values));
  }  
  
  void pushShare() {
    shift();
    values[0]++;
  }
  
  double updateStats() {
    unsigned sum = 0;
    for (unsigned i = 1; i < Interval; i++)
      sum += values[i];
    return (double)sum / ((Interval-1)*AccumulateInterval);
  }  
};

struct StratumTask {
  unsigned bits;
  std::string merkle;
};

struct poolContext {
  asyncBase *base;
  int signalPipeFd[2];
  aioObject *signalWriteObject;
  aioObject *signalReadObject;  
  aioObject *mainSocket;
  
  std::string xpmclientHost;
  unsigned xpmclientListenPort;
  unsigned xpmclientWorkPort; 
  PoolBackend *backend;
  p2pNode *client;

  pool::proto::Block mCurrBlock;
  std::map<std::string, unsigned> extraNonceMap;
  std::map<int64_t, StratumTask> stratumTaskMap;
  std::set<uint256> uniqueShares;
  double difficulty;
//   mpz_class blockTarget;
  uint256 blockTarget;
  
//   mpz_class shareTarget;
  uint256 shareTarget;
  mpz_class shareTargetMpz;
  uint32_t shareTargetBits;
  std::string shareTargetForStratum;
  
  
  // stratum
  bool checkAddress;
  int64_t sessionId;
  
  // must be in thread context
  std::vector<aioObject*> signalSockets;
  std::map<int64_t, StratumWorker> stratumWorkers;
};

struct readerContext {
  aioObject *socket;
  poolContext *poolCtx;
  unsigned shareCounter;
};

unsigned getMinShare(unsigned difficultyTarget);

bool checkRequest(poolContext *context, 
                  pool::proto::Request &req,
                  pool::proto::Reply &rep,
                  void *msg,
                  size_t msgSize);

void onConnect(poolContext *context,
               pool::proto::Request &req,
               pool::proto::Reply &rep);

void onGetWork(poolContext *context,
               pool::proto::Request &req,
               pool::proto::Reply &rep,
               bool *needDisconnect);

void onShare(poolContext *context,
             pool::proto::Request &req,
             pool::proto::Reply &rep,
             bool *needDisconnect);

void onStats(poolContext *context,
             pool::proto::Request &req,
             pool::proto::Reply &rep);



// -- stratum callbacks --

void onStratumSubscribe(poolContext *context, aioObject *socket, StratumMessage *subscribe, int64_t sessionId);
void onStratumAuthorize(poolContext *context, aioObject *socket, StratumMessage *msg, int64_t sessionId);
void onStratumSubmit(poolContext *context, aioObject *socket, StratumMessage *msg, int64_t sessionId);

void stratumSendSetTarget(poolContext *context, aioObject *socket);
bool stratumSendNewWork(poolContext *context, aioObject *socket);
void stratumSendStats(poolContext *context, StratumWorker &worker);
