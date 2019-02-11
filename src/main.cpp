#include "zcashpool.h"
#include "address.h"
#include "stratum.h"
#include "poolcore/backend.h"
#include "poolcommon/poolapi.h"

#include "asyncio/coroutine.h"
#include "asyncio/socket.h"
#include "asyncioextras/zmtp.h"
#include "p2p/p2p.h"
#include "p2putils/coreTypes.h"
#include "p2putils/uriParse.h"

__NO_DEPRECATED_BEGIN
#include "config4cpp/Configuration.h"
__NO_DEPRECATED_END

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "uint256.h"

#define TM 5000000

static poolContext *gPoolContext;

struct listenerContext {
  asyncBase *base;
  aioObject *socket;
  coroutineProcTy *proc;
  void *arg;
};

inline void mpz_from_uint256(mpz_t r, uint256& u)
{
  mpz_import(r, 32 / sizeof(unsigned long), -1, sizeof(unsigned long), -1, 0, &u);
}

inline void mpz_to_uint256(mpz_t r, uint256 &u)
{
  memset(&u, 0, sizeof(uint256));
  mpz_export(&u, nullptr, -1, 4, 0, 0, r);
}

static double difficultyFromBits(int64_t nBits)
{
  uint256 powLimit256("03ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  uint32_t powLimit = powLimit256.GetCompact(false);
  int nShift = (nBits >> 24) & 0xff;
  int nShiftAmount = (powLimit >> 24) & 0xff;

  double dDiff = (double)(powLimit & 0x00ffffff) /  (double)(nBits & 0x00ffffff);
  while (nShift < nShiftAmount) {
    dDiff *= 256.0;
    nShift++;
  }

  while (nShift > nShiftAmount) {
    dDiff /= 256.0;
    nShift--;
  }

  return dDiff;;
}

static mpz_class hashTargetFromBits(unsigned nBits)
{
  mpz_class target;
  uint256 hashTarget;
  hashTarget = nBits & 0x007FFFFF;
  unsigned exponent = nBits >> 24;
  if (exponent <= 3)
    hashTarget >>= 8*(3-exponent);
  else
    hashTarget <<= 8*(exponent-3);
  mpz_from_uint256(target.get_mpz_t(), hashTarget);
  return target;
}

static void sigIntHandler(int c)
{
  int msg = 0;
  write(gPoolContext->signalPipeFd.write, &msg, sizeof(msg));
}

void sendSignalCb(AsyncOpStatus status, zmtpSocket *socket, void *arg)
{
  poolContext *ctx = (poolContext*)arg;
  if (status == aosDisconnected) {
    for (auto I = ctx->signalSockets.begin(), IE = ctx->signalSockets.end(); I != IE; ++I) {
      if (*I == socket) {
        ctx->signalSockets.erase(I);
        break;
      }
    }

    zmtpSocketDelete(socket);
  }
}

void sendSignal(poolContext *ctx, const void *data, size_t size)
{
  for (size_t i = 0; i < ctx->signalSockets.size(); i++) {
    aioZmtpSend(ctx->signalSockets[i], (void*)data, size, zmtpMessage, afNone, TM, sendSignalCb, ctx);
  }
}

void updateStratumWorkers(void *arg)
{
  poolContext *ctx = (poolContext*)arg;
  for (auto &w: ctx->stratumWorkers)
    stratumSendNewWork(ctx, w.second.socket, w.first);
}

void frontendProc(void *arg)
{
  readerContext *rctx = (readerContext*)arg;
  poolContext *poolCtx = rctx->poolCtx;
  zmtpSocket *socket = zmtpSocketNew(poolCtx->base, newSocketIo(poolCtx->base, rctx->socket), zmtpSocketDEALER);

  if (ioZmtpAccept(socket, afNone, TM) < 0) {
    zmtpSocketDelete(socket);
    return;
  }

  pool::proto::Request req;
  pool::proto::Reply rep;
  zmtpStream stream;
  zmtpUserMsgTy msgType;
  while ((ioZmtpRecv(socket, stream, 65536, afNone, 0, &msgType) > 0) && msgType == zmtpMessage) {
    if (checkRequest(poolCtx, req, rep, stream.data<uint8_t>(), stream.remaining())) {
      pool::proto::Request::Type requestType = req.type();

      // Valid requests here:
      //   CONNECT
      if (requestType == pool::proto::Request::CONNECT) {
        onConnect(poolCtx, req, rep);
      }

      size_t repSize = rep.ByteSize();
      stream.reset();
      rep.SerializeToArray(stream.alloc<void>(repSize), repSize);
      ioZmtpSend(socket, stream.data(), stream.sizeOf(), zmtpMessage, afNone, TM);
    } else {
      break;
    }
  }

  zmtpSocketDelete(socket);
}

void mainProc(void *arg)
{
  readerContext *rctx = (readerContext*)arg;
  poolContext *poolCtx = rctx->poolCtx;
  zmtpSocket *socket = zmtpSocketNew(poolCtx->base, newSocketIo(poolCtx->base, rctx->socket), zmtpSocketROUTER);



  if (ioZmtpAccept(socket, afNone, TM) < 0) {
    zmtpSocketDelete(socket);
    return;
  }

  zmtpStream stream;
  pool::proto::Request req;
  pool::proto::Reply rep;
  zmtpUserMsgTy msgType;
  while ((ioZmtpRecv(socket, stream, 65536, afNone, 0, &msgType) > 0) && msgType == zmtpMessage) {
    if (checkRequest(poolCtx, req, rep, stream.data<uint8_t>(), stream.remaining())) {
      pool::proto::Request::Type requestType = req.type();

      // Valid requests here:
      //   GETWORK
      //   SHARE
      //   STATS
      bool needDisconnect = false;
      if (requestType == pool::proto::Request::GETWORK) {
        onGetWork(poolCtx, req, rep, &needDisconnect);
      } else if (requestType == pool::proto::Request::SHARE) {
        onShare(poolCtx, req, rep, &needDisconnect);
      } else if (requestType == pool::proto::Request::STATS) {
        onStats(poolCtx, req, rep);
      }

      size_t repSize = rep.ByteSize();
      stream.reset();
      rep.SerializeToArray(stream.alloc<void>(repSize), repSize);
      ioZmtpSend(socket, stream.data(), stream.sizeOf(), zmtpMessage, afNone, TM);
      if (needDisconnect)
        break;
    } else {
      break;
    }
  }

  zmtpSocketDelete(socket);
}

void stratumProc(void *arg)
{
  readerContext *rctx = (readerContext*)arg;
  poolContext *poolCtx = rctx->poolCtx;
  aioObject *socket = newSocketIo(poolCtx->base, rctx->socket);

  int64_t sessionId = poolCtx->sessionId++;

  // TODO: lookup '\n' after message
  bool sessionActive = true;
  ssize_t bytesRead;
  ssize_t offset = 0;
  char *buffer = (char*)malloc(40960);
  while ( sessionActive && (bytesRead = ioRead(socket, buffer+offset, 40960 - offset - 1, afNone, 0)) > 0) {
    offset += bytesRead;
    buffer[offset] = 0;
    char *p = strchr(buffer, '\n');
    if (!p)
      continue;

    *p = 0;

    StratumMessage msg;
    switch (decodeStratumMessage(buffer, &msg)) {
      case StratumDecodeStatusTy::Ok :
        // Process stratum messages here
        switch (msg.method) {
          case StratumMethodTy::Subscribe :
            onStratumSubscribe(poolCtx, socket, &msg, sessionId);
            break;
          case StratumMethodTy::Authorize :
            onStratumAuthorize(poolCtx, socket, &msg, sessionId);

            // send target and work
            stratumSendSetTarget(poolCtx, socket);
            stratumSendNewWork(poolCtx, socket, sessionId);
            break;
          case StratumMethodTy::ExtraNonceSubscribe :
            // nothing to do
            break;
          case StratumMethodTy::Submit :
            onStratumSubmit(poolCtx, socket, &msg, sessionId);
            break;
          default :
            // unknown method
            break;
        }

        break;
      case StratumDecodeStatusTy::JsonError :
        sessionActive = false;
        break;
      case StratumDecodeStatusTy::FormatError :
        break;
      default :
        break;
    }

    // move remaining to begin of buffer
    ssize_t nextMsgOffset = p+1-buffer;
    if (nextMsgOffset < offset) {
      memcpy(buffer, buffer+nextMsgOffset, offset-nextMsgOffset);
      offset = offset - nextMsgOffset;
    } else {
      offset = 0;
    }
  }

  poolCtx->stratumWorkers.erase(sessionId);
  free(buffer);
  deleteAioObject(socket);
}


void signalsProc(void *arg)
{
  readerContext *rctx = (readerContext*)arg;
  poolContext *ctx = (poolContext*)rctx->poolCtx;
  zmtpSocket *socket = zmtpSocketNew(ctx->base, newSocketIo(ctx->base, rctx->socket), zmtpSocketPUB);

  zmtpStream stream;
  if (ioZmtpAccept(socket, afNone, TM) < 0) {
    zmtpSocketDelete(socket);
    return;
  }

  ctx->signalSockets.push_back(socket);
}

template<typename Ty>
void mpz_class_set(mpz_class &rop, Ty op)
{
  mpz_import(rop.get_mpz_t(), 1, 1, sizeof(op), 0, 0, &op);
}

void timerProc(void *arg)
{
  poolContext *ctx = (poolContext*)arg;

  aioUserEvent *timerEvent = newUserEvent(ctx->base, nullptr, nullptr);
  bool connectedBefore = ctx->client->connected();
  xmstream stream;
  while (true) {
    ioSleep(timerEvent, 500000);
    if (connectedBefore != ctx->client->connected()) {
      if (connectedBefore == false) {
        auto receivedBlock = ioGetCurrentBlock(ctx->client);
        if (!receivedBlock)
          continue;
        ctx->difficulty = difficultyFromBits(receivedBlock->bits);
        ctx->extraNonceMap.clear();
        ctx->mCurrBlock.set_height(receivedBlock->height);
        ctx->mCurrBlock.set_hash(receivedBlock->hash.c_str());
        ctx->mCurrBlock.set_prevhash(receivedBlock->prevhash.c_str());
        ctx->mCurrBlock.set_reqdiff(ctx->shareTargetBits);
        ctx->mCurrBlock.set_minshare(0);

        ctx->uniqueShares.clear();
        ctx->stratumTaskMap.clear();

        mpz_class blockTarget;
        mpz_class_set(blockTarget, receivedBlock->bits & 0x007FFFFF);
        unsigned exponent = receivedBlock->bits >> 24;
        if (exponent <= 3)
          blockTarget >>= 8*(3-exponent);
        else
          blockTarget <<= 8*(exponent-3);
        mpz_to_uint256(blockTarget.get_mpz_t(), ctx->blockTarget);

        mpz_class sharesPerBlock = ctx->shareTargetMpz / blockTarget;
        fprintf(stderr,
                " * new block: %u, diff=%.5lf, approximate shares per block: %lu\n",
                (unsigned)receivedBlock->height,
                ctx->difficulty,
                std::max(sharesPerBlock.get_ui(), 1ul));

        pool::proto::Signal sig;
        pool::proto::Block* block = sig.mutable_block();


      } else {
        ctx->mCurrBlock.set_height(0);
      }

      pool::proto::Signal sig;
      pool::proto::Block* block = sig.mutable_block();
      sig.set_type(pool::proto::Signal::NEWBLOCK);
      block->CopyFrom(ctx->mCurrBlock);
      stream.reset();
      stream.write<uint8_t>(1);
      size_t size = sig.ByteSize();
      sig.SerializeToArray(stream.alloc(size), size);
      sendSignal(ctx, stream.data(), stream.offsetOf());
      updateStratumWorkers(ctx);
    }

    connectedBefore = ctx->client->connected();

    // update work for stratum miners if needed
    time_t tm = time(0);
    for (auto &w: ctx->stratumWorkers) {
      if (tm - w.second.lastUpdateTime >= ctx->stratumWorkLifeTime)
        stratumSendNewWork(ctx, w.second.socket, w.first);
    }
  }
}


void stratumStatsProc(void *arg)
{
  poolContext *ctx = (poolContext*)arg;
  aioUserEvent *timerEvent = newUserEvent(ctx->base, nullptr, nullptr);
  while (true) {
    ioSleep(timerEvent, 60*1000000);

    for (auto &w: ctx->stratumWorkers)
      stratumSendStats(ctx, w.second);
  }
}


void listener(void *arg)
{
  listenerContext *ctx = (listenerContext*)arg;
  while (true) {
    HostAddress address;
    socketTy acceptSocket = ioAccept(ctx->socket, 0);
    if (acceptSocket > 0) {
      readerContext *rctx = new readerContext;
      rctx->socket = acceptSocket;
      rctx->poolCtx = (poolContext*)ctx->arg;
      coroutineTy *proc = coroutineNew(ctx->proc, rctx, 0x40000);
      coroutineCall(proc);
    }
  }
}


aioObject *createListener(asyncBase *base, uint16_t port, coroutineProcTy proc, aioObject **socketPtr, void *arg)
{
  HostAddress address;
  address.family = AF_INET;
  address.ipv4 = INADDR_ANY;
  address.port = htons(port);
  socketTy hSocket = socketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP, 1);
  socketReuseAddr(hSocket);
  if (socketBind(hSocket, &address) != 0) {
    fprintf(stderr, "cannot bind port: %i\n", port);
    exit(1);
  }

  if (socketListen(hSocket) != 0) {
    fprintf(stderr, "listen error: %i\n", port);
    exit(1);
  }

  aioObject *socket = newSocketIo(base, hSocket);
  listenerContext *ctx = new listenerContext;
  ctx->base = base;
  ctx->socket = socket;
  ctx->proc = proc;
  ctx->arg = arg;

  if (socketPtr)
    *socketPtr = socket;

  coroutineTy *listenerProc = coroutineNew(listener, ctx, 0x10000);
  coroutineCall(listenerProc);
}

void signalHandler(p2pPeer *peer, void *buffer, size_t size, void *arg)
{
  poolContext *context = (poolContext*)arg;
  const Signal *signal = flatbuffers::GetRoot<Signal>(buffer);
  xmstream stream;
  switch (signal->signalId()) {
    case SignalId_NewBlock : {
      const Block *receivedBlock = static_cast<const Block*>(signal->data());
      context->difficulty = difficultyFromBits(receivedBlock->bits());
      context->extraNonceMap.clear();
      context->mCurrBlock.set_height(receivedBlock->height());
      context->mCurrBlock.set_hash(receivedBlock->hash()->c_str());
      context->mCurrBlock.set_prevhash(receivedBlock->prevhash()->c_str());
      context->mCurrBlock.set_reqdiff(context->shareTargetBits);
      context->mCurrBlock.set_minshare(0);

      context->uniqueShares.clear();
      context->stratumTaskMap.clear();

      mpz_class blockTarget;
      mpz_class_set(blockTarget, receivedBlock->bits() & 0x007FFFFF);
      unsigned exponent = receivedBlock->bits() >> 24;
      if (exponent <= 3)
        blockTarget >>= 8*(3-exponent);
      else
        blockTarget <<= 8*(exponent-3);
      mpz_to_uint256(blockTarget.get_mpz_t(), context->blockTarget);

      mpz_class sharesPerBlock = context->shareTargetMpz / blockTarget;

      fprintf(stderr,
              " * new block signal: %u, diff=%.5lf, approximate shares per block: %lu\n",
              (unsigned)receivedBlock->height(),
              context->difficulty,
              std::max(sharesPerBlock.get_ui(), 1ul));

      pool::proto::Signal sig;
      pool::proto::Block* block = sig.mutable_block();

      sig.set_type(pool::proto::Signal::NEWBLOCK);
      block->CopyFrom(context->mCurrBlock);

      stream.reset();
      stream.write<uint8_t>(1);
      size_t size = sig.ByteSize();
      sig.SerializeToArray(stream.alloc(size), size);
      sendSignal(context, stream.data(), stream.offsetOf());
      coroutineCall(coroutineNew(updateStratumWorkers, context, 0x10000));
    }
  }
}

void sigintProc(void *arg)
{
  int msg;
  poolContext *context = (poolContext*)arg;
  ioRead(context->signalReadObject, &msg, sizeof(msg), afWaitAll, 0);

  deleteAioObject(context->mainSocket);

  xmstream stream;
  pool::proto::Signal sig;
  sig.set_type(pool::proto::Signal_Type_SHUTDOWN);
  stream.reset();
  stream.write<uint8_t>(1);
  size_t size = sig.ByteSize();
  sig.SerializeToArray(stream.alloc(size), size);
  sendSignal(context, stream.data(), stream.offsetOf());

  context->backend->stop();

  aioUserEvent *timerEvent = newUserEvent(context->base, nullptr, nullptr);
  printf("\n");
  for (unsigned i = 0; i < 3; i++) {
    printf(".");
    fflush(stdout);
    ioSleep(timerEvent, 1000000);
  }

  postQuitOperation(context->base);
}

static bool checkZECAddress(const char *address)
{
  CZECAddress A(address);
  return A.isValid();
}

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <configuration file>\n", argv[0]);
    return 1;
  }

  PoolBackend::config backendConfig;
  poolContext context;
  bool checkAddress;
  uint16_t stratumPort;
  config4cpp::Configuration *cfg = config4cpp::Configuration::create();

  try {
    cfg->parse(argv[1]);

    backendConfig.isMaster = cfg->lookupBoolean("pool_frontend_zcash", "isMaster", true);
    backendConfig.poolFee = cfg->lookupInt("pool_frontend_zcash", "poolFee", 0);
    backendConfig.poolFeeAddr = cfg->lookupString("pool_frontend_zcash", "poolFeeAddr", "");


    config4cpp::StringVector wallets;
    cfg->lookupList("pool_frontend_zcash", "walletAddrs", wallets);
    for (decltype(wallets.length()) i = 0; i < wallets.length(); i++) {
      URI uri;
      if (!uriParse(wallets[i], &uri)) {
        fprintf(stderr, "<error> can't read walletaddrs from configuration file\n");
        return 1;
      }

      if (uri.schema != "p2p" || !uri.ipv4 || !uri.port) {
        fprintf(stderr, "<error> walletaddrs can be contain only p2p://xxx.xxx.xxx.xxx:port address now\n");
        return 1;
      }

      HostAddress address;
      address.family = AF_INET;
      address.ipv4 = uri.ipv4;
      address.port = xhton<uint16_t>(uri.port);
      backendConfig.peers.push_back(address);
    }

    {
      URI uri;
      const char *localAddress = cfg->lookupString("pool_frontend_zcash", "localAddress");
      if (!uriParse(localAddress, &uri)) {
        fprintf(stderr, "<error> can't read localAddress from configuration file\n");
        return 1;
      }

      if (uri.schema != "p2p" || !uri.ipv4 || !uri.port) {
        fprintf(stderr, "<error> localAddress can be contain only p2p://xxx.xxx.xxx.xxx:port address now\n");
        return 1;
      }

      HostAddress address;
      address.family = AF_INET;
      address.ipv4 = uri.ipv4;
      address.port = xhton<uint16_t>(uri.port);
      backendConfig.listenAddress = address;
    }

    checkAddress = cfg->lookupBoolean("pool_frontend_zcash", "checkAddress", true);

    backendConfig.walletAppName = cfg->lookupString("pool_frontend_zcash", "walletAppName", "pool_rpc");
    backendConfig.poolAppName = cfg->lookupString("pool_frontend_zcash", "poolAppName", "pool_frontend_zcash");
    backendConfig.requiredConfirmations = cfg->lookupInt("pool_frontend_zcash", "requiredConfirmations", 10);
    backendConfig.defaultMinimalPayout = (int64_t)(cfg->lookupFloat("pool_frontend_zcash", "defaultMinimalPayout", 4)*COIN);
    backendConfig.minimalPayout = (int64_t)(cfg->lookupFloat("pool_frontend_zcash", "minimalPayout", 0.001)*COIN);
    backendConfig.dbPath = cfg->lookupString("pool_frontend_zcash", "dbPath");
    backendConfig.keepRoundTime = cfg->lookupInt("pool_frontend_zcash", "keepRoundTime", 3) * 24*3600;
    backendConfig.keepStatsTime = cfg->lookupInt("pool_frontend_zcash", "keepStatsTime", 2) * 60;
    backendConfig.confirmationsCheckInterval = cfg->lookupInt("pool_frontend_zcash", "confirmationsCheckInterval", 10) * 60 * 1000000;
    backendConfig.payoutInterval = cfg->lookupInt("pool_frontend_zcash", "payoutInterval", 60) * 60 * 1000000;
    backendConfig.balanceCheckInterval = cfg->lookupInt("pool_frontend_zcash", "balanceCheckInterval", 3) * 60 * 1000000;
    backendConfig.statisticCheckInterval = cfg->lookupInt("pool_frontend_zcash", "statisticCheckInterval", 1) * 60 * 1000000;

    backendConfig.checkAddressProc = checkAddress ? checkZECAddress : 0;
    backendConfig.useAsyncPayout = true;
    backendConfig.poolZAddr = cfg->lookupString("pool_frontend_zcash", "pool_zaddr");
    backendConfig.poolTAddr = cfg->lookupString("pool_frontend_zcash", "pool_taddr");

    context.xpmclientHost = cfg->lookupString("pool_frontend_zcash", "zmqclientHost");
    context.xpmclientListenPort = cfg->lookupInt("pool_frontend_zcash", "zmqclientListenPort");
    context.xpmclientWorkPort = cfg->lookupInt("pool_frontend_zcash", "zmqclientWorkPort");
    context.stratumWorkLifeTime = cfg->lookupInt("pool_frontend_zcash", "stratumWorkLifeTime", 9) * 60;
    stratumPort = cfg->lookupInt("pool_frontend_zcash", "stratumPort", 3357);

    // calculate share target
    context.shareTargetCoeff = cfg->lookupInt("pool_frontend_zcash", "shareTarget", 1024);
    mpz_class N = 1;
    N <<= 256;
    N /= context.shareTargetCoeff;
    mpz_to_uint256(N.get_mpz_t(), context.shareTarget);
    context.shareTargetBits = context.shareTarget.GetCompact(false);
    context.shareTargetMpz = N;
    context.shareTargetForStratum = context.shareTarget.ToString();
    fprintf(stderr, "<info> share target for stratum is %s\n", context.shareTargetForStratum.c_str());

    context.equihashShareCheck = cfg->lookupBoolean("pool_frontend_zcash", "equihashShareCheck", true);
  } catch(const config4cpp::ConfigurationException& ex){
    fprintf(stderr, "<error> %s\n", ex.c_str());
    exit(1);
  }

  initializeSocketSubsystem();

  asyncBase *base = createAsyncBase(amOSDefault);


  context.base = base;
  context.mCurrBlock.set_height(0);

  // ZMQ protocol
  createListener(base, context.xpmclientListenPort, frontendProc, &context.mainSocket, &context);
  createListener(base, context.xpmclientWorkPort, mainProc, nullptr, &context);
  createListener(base, context.xpmclientWorkPort+1, signalsProc, nullptr, &context);

  // Stratum protocol
  context.checkAddress = checkAddress;
  context.sessionId = 0;
  createListener(base, stratumPort, stratumProc, nullptr, &context);
  coroutineCall(coroutineNew(stratumStatsProc, &context, 0x10000));

  context.client =
    p2pNode::createClient(base,
                          &backendConfig.peers[0],
                          backendConfig.peers.size(),
                          backendConfig.walletAppName.c_str());

  context.client->setSignalHandler(signalHandler, &context);

  coroutineCall(coroutineNew(timerProc, &context, 0x10000));


  context.backend = new PoolBackend(&backendConfig);
  context.backend->start();

  // Handle CTRL+C (SIGINT)
  {
    gPoolContext = &context;
    pipeCreate(&context.signalPipeFd, 1);
    context.signalReadObject = newDeviceIo(base, context.signalPipeFd.read);
    context.signalWriteObject = newDeviceIo(base, context.signalPipeFd.write);
    signal(SIGINT, sigIntHandler);
    coroutineCall(coroutineNew(sigintProc, &context, 0x10000));
  }

  asyncLoop(base);
  printf("pool_frondend_zcash stopped\n\n");
  return 0;
}
