#include "zcashpool.h"
#include "poolcommon/poolapi.h"
#include "poolcore/backend.h"
#include "poolcore/base58.h"
#include "p2p/p2p.h"
#include "equihash_original.h"
#include <openssl/sha.h>


#include "block.h"
#include <stdio.h>


inline void mpz_set_uint256(mpz_t r, uint256& u)
{
    mpz_import(r, 32 / sizeof(unsigned long), -1, sizeof(unsigned long), -1, 0, &u);
}

unsigned writeCompactSize(size_t size, uint8_t *out)
{
  if (size < 253) {
    out[0] = size;
    return 1;
  } else if (size <= std::numeric_limits<unsigned short>::max()) {
    out[0] = 253;
    *(uint16_t*)(out+1) = size;
    return 3;
  } else if (size <= std::numeric_limits<unsigned int>::max()) {
    out[0] = 254;
    *(uint32_t*)(out+1) = size;
    return 5;
  } else {
    out[0] = 255;
    *(uint64_t*)(out+1) = size;
  }
  return 0;
}

bool equiHashShareCheck(const CBlockHeader *header, const uint8_t *proof, size_t size)
{
  crypto_generichash_blake2b_state state;
  Eh200_9.InitialiseState(state);
  crypto_generichash_blake2b_update(&state, (const uint8_t*)&header->data, sizeof(header->data));
  crypto_generichash_blake2b_update(&state, (const uint8_t*)header->nNonce.begin(), 32);
  std::vector<uint8_t> proofForCheck(proof, proof+size);
  return Eh200_9.IsValidSolution(state, proofForCheck);
}

bool checkRequest(poolContext *context, 
                  pool::proto::Request &req,
                  pool::proto::Reply &rep,
                  void *msg,
                  size_t msgSize)
{
  if (!req.ParseFromArray(msg, msgSize)) {
    fprintf(stderr, "invalid message received\n");
    return false;
  }

  rep.Clear();
  rep.set_type(req.type());
  rep.set_reqid(req.reqid());
  rep.set_error(pool::proto::Reply::NONE);
  
  if(req.has_height() && req.height() < context->mCurrBlock.height()) {
    rep.mutable_block()->CopyFrom(context->mCurrBlock);
  }  
  return true;
}


void onConnect(poolContext *context, pool::proto::Request &req, pool::proto::Reply &rep)
{
  // TODO: check version  
  bool versionIsValid = true;
  
  if (!versionIsValid) {
    rep.set_error(pool::proto::Reply::VERSION);
    rep.set_errstr("Your miner version will no longer be supported in the near future. Please upgrade.");
  }  
  
  // fill 'bitcoin' and 'signals' host addresses
  pool::proto::ServerInfo mServerInfo;
  mServerInfo.set_host(context->xpmclientHost);
  mServerInfo.set_router(context->xpmclientWorkPort);
  mServerInfo.set_pub(context->xpmclientWorkPort+1);
  mServerInfo.set_target(context->difficulty != 0 ? (unsigned)context->difficulty : 10);

  rep.mutable_sinfo()->CopyFrom(mServerInfo);  
}


void onGetWork(poolContext *context, pool::proto::Request &req, pool::proto::Reply &rep, bool *needDisconnect)
{
  // getblocktemplate call
  *needDisconnect = false;  
  auto block = ioGetBlockTemplate(context->client);
  if (!block) {
    *needDisconnect = true;
    rep.set_error(pool::proto::Reply::HEIGHT);
    rep.set_errstr("Can't receive work from ZCash wallet");
    return;
  }
  
  context->extraNonceMap[block->merkle.c_str()] = block->extraNonce;
  
  pool::proto::Work* work = rep.mutable_work();
  work->set_height(context->mCurrBlock.height());
  work->set_merkle(block->merkle.c_str());
  work->set_time(std::max(time(0), block->time));
  work->set_bits(block->bits);  
  work->set_hashreserved(block->hashreserved.c_str());
  work->set_k(block->equilHashK);
  work->set_n(block->equilHashN);
}

void onShare(poolContext *context, pool::proto::Request &req, pool::proto::Reply &rep, bool *needDisconnect)
{
  *needDisconnect = false;
  if (!context->mCurrBlock.has_height()) {
    rep.set_error(pool::proto::Reply::STALE);
    return;
  }
  
  // share existing
  if (!req.has_share() || !req.share().has_bignonce() || !req.share().has_proofofwork()) {
    fprintf(stderr, "ERROR: !req.has_share().\n");
    rep.set_error(pool::proto::Reply::INVALID);
    return;
  }
  
  // block height
  const pool::proto::Share& share = req.share();
  if (share.height() != context->mCurrBlock.height()) {
    rep.set_error(pool::proto::Reply::STALE);
    return;
  }
  
  // merkle must be present at extraNonce map
  auto nonceIt = context->extraNonceMap.find(share.merkle());
  if (nonceIt == context->extraNonceMap.end()) {
    *needDisconnect = true;
    fprintf(stderr, "ERROR: share for unknown work\n");    
    rep.set_error(pool::proto::Reply::STALE);
    return;
  }
 
  // check share
  unsigned value = 0;  
  mpz_class mpzValue;
  mpz_class mpzHash;
 
  uint256 shareHeaderHash;
  
  {
    CBlockHeader header;
    header.data.nVersion = CBlockHeader::CURRENT_VERSION;
    header.data.hashPrevBlock.SetHex(context->mCurrBlock.hash());
    header.data.hashMerkleRoot.SetHex(share.merkle());
    // TODO: from work
    header.data.hashReserved = 0;
    header.data.nTime = share.time();
    header.data.nBits = share.bits();    
    header.nNonce.SetHex(share.bignonce());
 
    uint8_t compactSizeData[16];
    unsigned compactSize = writeCompactSize(share.proofofwork().size(), compactSizeData);
    
    SHA256_CTX ctx;
    uint8_t hash1[32];
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, &header.data, sizeof(header.data));
    SHA256_Update(&ctx, header.nNonce.begin(), 32);
    SHA256_Update(&ctx, compactSizeData, compactSize);
    SHA256_Update(&ctx, share.proofofwork().data(), share.proofofwork().size());
    SHA256_Final(hash1, &ctx);
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, hash1, sizeof(hash1));
    SHA256_Final(shareHeaderHash.begin(), &ctx);
    
    uint256 receivedHash;
    receivedHash.SetHex(share.hash());
    if (receivedHash != shareHeaderHash) {
      fprintf(stderr, "<info> invalid share(sha256), no penalty\n");
      rep.set_error(pool::proto::Reply::INVALID);
      return;
    }
    
    // warning: need a huge CPU power for share check
//     if (false) {
      if (!equiHashShareCheck(&header, (const uint8_t*)share.proofofwork().data(), share.proofofwork().size())) {
        fprintf(stderr, "<info> invalid share(equi), do a penalty\n");      
        rep.set_error(pool::proto::Reply::INVALID);
      }
//     }    
  }
  
  mpz_set_uint256(mpzHash.get_mpz_t(), shareHeaderHash);
  if (mpzHash > context->shareTarget) {
    fprintf(stderr, "<info> too small share\n");
    rep.set_error(pool::proto::Reply::INVALID);
    return;
  }
  
  mpzValue = context->shareTarget;
  mpzValue /= mpzHash;
  value = log(mpzValue.get_d()) / log(1.0625);
  if (!value && mpzValue > 0)
    value = 1;

  // check duplicate
  if (!context->uniqueShares.insert(shareHeaderHash).second) {
    fprintf(stderr, "<info> duplicate share\n");
    rep.set_error(pool::proto::Reply::DUPLICATE);
    return;
  }
  
  if (value) {
    int64_t generatedCoins = 0;
    if (share.isblock()) {
      fprintf(stderr, "<info> found block %s\n", shareHeaderHash.ToString().c_str());
      auto result = ioSendProofOfWork(context->client,
                                      context->mCurrBlock.height(),
                                      share.time(),
                                      share.bignonce(),
                                      nonceIt->second,
                                      share.proofofwork());
      if (!result) {
        rep.set_error(pool::proto::Reply::INVALID);
        return;
      }
      
      if (result->result == true) {
        generatedCoins = result->generatedCoins;
      } else {
        fprintf(stderr, "ERROR: proofOfWork check failed\n");
        rep.set_error(pool::proto::Reply::INVALID);
        return;
      }      
    }
    
    // send share information to backend
    {   
      flatbuffers::FlatBufferBuilder fbb;
      auto shareOffset = CreateShare(fbb, 
                                     context->mCurrBlock.height()+1,
                                     fbb.CreateString(share.addr()),
                                     value,
                                     share.isblock(),
                                     fbb.CreateString(shareHeaderHash.ToString()),
                                     generatedCoins);
      fbb.Finish(CreateP2PMessage(fbb, FunctionId_Share, Data_Share, shareOffset.Union()));
      if (!context->backend->sendMessage(context->base, fbb.GetBufferPointer(), fbb.GetSize())) {
        fprintf(stderr, "ERROR: can't send share to backend");
        rep.set_error(pool::proto::Reply::STALE);
        return;
      }
    }
    
  } else {
    fprintf(stderr, "ERROR: invalid share\n");
    rep.set_error(pool::proto::Reply::INVALID);
    return;
  }
}

void onStats(poolContext *context, pool::proto::Request &req, pool::proto::Reply &rep)
{
  if (!req.has_stats()) {
    fprintf(stderr, "<error> !req.has_stats().\n");
    return;
  }  
  
  const pool::proto::ClientStats &stats = req.stats();
  flatbuffers::FlatBufferBuilder fbb;
  auto statsOffset = CreateStats(fbb,
                                 fbb.CreateString(stats.addr()),
                                 fbb.CreateString(stats.name()),
                                 (int64_t)(stats.cpd()*1000.0),
                                 stats.latency(),
                                 fbb.CreateString("<unknown>"),
                                 stats.has_unittype() ? (UnitType)stats.unittype() : UnitType_GPU,
                                 stats.ngpus(),
                                 stats.temp());
  fbb.Finish(CreateP2PMessage(fbb, FunctionId_Stats, Data_Stats, statsOffset.Union()));
  if (!context->backend->sendMessage(context->base, fbb.GetBufferPointer(), fbb.GetSize())) {
    fprintf(stderr, "ERROR: can't send stats to backend");
    return;
  }
}

static bool splitStratumWorkerName(char *input, const char **wallet, const char **worker)
{
  char *s = strchr(input, '.');
  if (s) {
    *s = 0;
    *wallet = input;
    *worker = s+1;
    return true;
  } else {
    *wallet = input;
    *worker = "default";
    return true;
  }
}


int64_t stratumCheckShare(poolContext *context,
                          aioObject *socket,
                          StratumMessage *msg,
                          uint256 *shareHeaderHash,
                          const char **error,
                          int64_t *generatedCoins)
{
  if (!context->mCurrBlock.has_height()) {
    *error = "\"Stale share\"";
    return -1;
  }

  const char *extraNoncePos = strchr(msg->submit.jobId.c_str(), '#');
  if (!extraNoncePos) {
    *error = "\"Invalid job name\"";
    return -1;
  }
  
  int64_t extraNonce = xatoi<int64_t>(extraNoncePos+1);
  const StratumTask &task = context->stratumTaskMap[extraNonce];
  if (task.merkle.empty()) {
    *error = "\"Share for unknown work\"";
    return -1;
  }
  
  if (msg->submit.equihashSolution.size() < 4) {
    *error = "\"Invalid share(too short solution)\"";
    return -1;
  }
  
  unsigned value = 0;  
  mpz_class mpzValue;
  mpz_class mpzHash;

  CBlockHeader header;
  header.data.nVersion = CBlockHeader::CURRENT_VERSION;
  header.data.hashPrevBlock.SetHex(context->mCurrBlock.hash());
  header.data.hashMerkleRoot.SetHex(task.merkle);
  header.data.hashReserved = 0;
  header.data.nTime = msg->submit.time;
  header.data.nBits = task.bits;    

  // hi 32 bits always 0
  memset(header.nNonce.begin(), 0, 4);
  memcpy(header.nNonce.begin()+4, msg->submit.nonce, 28);

  const uint8_t *equihashSolution = msg->submit.equihashSolution.data()+3;
  size_t equihashSolutionSize = msg->submit.equihashSolution.size()-3;
  uint8_t compactSizeData[16];
  unsigned compactSize = writeCompactSize(equihashSolutionSize, compactSizeData);
  
  SHA256_CTX ctx;
  uint8_t hash1[32];
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, &header.data, sizeof(header.data));
  SHA256_Update(&ctx, header.nNonce.begin(), 32);
  SHA256_Update(&ctx, compactSizeData, compactSize);
  SHA256_Update(&ctx, equihashSolution, equihashSolutionSize);
  SHA256_Final(hash1, &ctx);
  SHA256_Init(&ctx);
  SHA256_Update(&ctx, hash1, sizeof(hash1));
  SHA256_Final(shareHeaderHash->begin(), &ctx);
  
  if (!equiHashShareCheck(&header, equihashSolution, equihashSolutionSize)) {
    *error = "\"Invalid share(equi)\"";
    return -1;
  }
  
  mpz_set_uint256(mpzHash.get_mpz_t(), *shareHeaderHash);
  if (mpzHash > context->shareTarget) {
    *error = "\"Too small share\"";    
    return -1;
  }
  
  mpzValue = context->shareTarget;
  mpzValue /= mpzHash;
  value = log(mpzValue.get_d()) / log(1.0625);
  if (!value && mpzValue > 0)
    value = 1;
  
  // check duplicate
  if (!context->uniqueShares.insert(*shareHeaderHash).second) {
    *error = "\"Duplicate share\"";
    return -1;
  }

  if (mpzHash <= context->blockTarget) {
    fprintf(stderr, "<info> (stratum) found block %s\n", shareHeaderHash->ToString().c_str());
    std::string proofofwork((const char*)equihashSolution, equihashSolutionSize);
    auto result = ioSendProofOfWork(context->client,
                                    context->mCurrBlock.height(),
                                    header.data.nTime,
                                    header.nNonce.GetHex(),
                                    extraNonce,
                                    proofofwork);
    if (!result || result->result == false) {
      *error = "\"Check proof of work failed\"";
      return -1;
    }
    
    *generatedCoins = result->generatedCoins;
  }
  
  return value;
}


void onStratumSubscribe(poolContext *context, aioObject *socket, StratumMessage *msg, int64_t sessionId)
{
  // {"id": 1, "result": ["SESSION_ID", "NONCE_1"], "error": null} \n
  char sessionBuffer[32];
  char response[256];
  xitoa<int64_t>(sessionId, sessionBuffer);
  snprintf(response, sizeof(response),
           "{\"id\": %i, \"result\": [\"%s\", \"00000000\"], \"error\": null}\n",
           (int)msg->id,
           sessionBuffer);
  aioWrite(context->base, socket, response, strlen(response), afWaitAll, 0, 0, 0);
}

void onStratumAuthorize(poolContext *context, aioObject *socket, StratumMessage *msg, int64_t sessionId)
{
  // {"id": 2, "result": AUTHORIZED, "error": ERROR} \n
  char response[256];
  const char *authorized;
  const char *error = "null";
  
  const char *wallet;
  const char *workerName;
  if (splitStratumWorkerName((char*)msg->authorize.login.data(), &wallet, &workerName)) {
    CBitcoinAddress address(wallet);
    if (address.IsValidForZCash()) {
      authorized = "true";
    } else {
      authorized = "false";
      error = "\"Invalid ZCash T-Address sent at a login\"";
    }
  } else {
    authorized = "false";
    error = "\"Invalid login format, should be Address/WorkerName\"";
  }
    
  snprintf(response, sizeof(response),
           "{\"id\": %i, \"result\": %s, \"error\": %s}\n",
           (int)msg->id,
           authorized,
           error);
  aioWrite(context->base, socket, response, strlen(response), afWaitAll, 0, 0, 0);
  
  // add worker record
  StratumWorker worker;
  worker.wallet = wallet;
  worker.worker = workerName;
  worker.socket = socket;
  context->stratumWorkers[sessionId] = worker;
}

void onStratumSubmit(poolContext *context, aioObject *socket, StratumMessage *msg, int64_t sessionId)
{
  // {"id": 4, "method": "mining.submit", "params": ["WORKER_NAME", "JOB_ID", "TIME", "NONCE_2", "EQUIHASH_SOLUTION"]} \n 
  int64_t value = -1;
  const char *error = "null";
  const char *wallet;
  const char *workerName;
  if (splitStratumWorkerName((char*)msg->submit.workerName.data(), &wallet, &workerName)) {
    int64_t generatedCoins = 0;
    uint256 shareHeaderHash = 0;
    value = stratumCheckShare(context, socket, msg, &shareHeaderHash, &error, &generatedCoins);
    if (value > 0) {
      flatbuffers::FlatBufferBuilder fbb;
      auto shareOffset = CreateShare(fbb, 
                                     context->mCurrBlock.height()+1,
                                     fbb.CreateString(wallet),
                                     value,
                                     generatedCoins > 0,
                                     fbb.CreateString(shareHeaderHash.ToString()),
                                     generatedCoins);
      fbb.Finish(CreateP2PMessage(fbb, FunctionId_Share, Data_Share, shareOffset.Union()));
      if (!context->backend->sendMessage(context->base, fbb.GetBufferPointer(), fbb.GetSize())) {
        value = -1;
        error = "\"Stale share\"";
      }
    }
  } else {
    value = -1;
    error = "\"Invalid login format\"";
  }

  // {"id": 4, "result": ACCEPTED, "error": ERROR}\n
  char response[256];
  snprintf(response, sizeof(response),
           "{\"id\": %i, \"result\": %s, \"error\": %s}\n",
           (int)msg->id,
           (value>0 ? "true" : "false"),
           error);

  aioWrite(context->base, socket, response, strlen(response), afWaitAll, 0, 0, 0);
  if (value > 0) {
    context->stratumWorkers[sessionId].pushShare();
  } else {
    stratumSendNewWork(context, socket);
  }
}

void stratumSendSetTarget(poolContext *context, aioObject *socket)
{
  char message[256];
  // TODO: take minimal share from context, don't send
  // 0080000000000000000000000000000000000000000000000000000000000000
  snprintf(message, sizeof(message),
           "{\"id\": null, \"method\": \"mining.set_target\", \"params\": [\"%s\"]}\n",
           "0080000000000000000000000000000000000000000000000000000000000000");  
  
  aioWrite(context->base, socket, message, strlen(message), afWaitAll, 0, 0, 0);
}

bool stratumSendNewWork(poolContext *context, aioObject *socket)
{
  //{"id": null, "method": "mining.notify", "params": ["JOB_ID", "VERSION", "PREVHASH", "MERKLEROOT", "RESERVED", "TIME", "BITS", CLEAN_JOBS]} \n
  
  // getblocktemplate call
  auto block = ioGetBlockTemplate(context->client);
  if (!block || context->mCurrBlock.hash().empty() || block->merkle.empty())
    return false;
  
  char message[4096];
  char extraNonceBuffer[64];
  char prevHash[128];
  char merkleroot[128];
  char reserved[128];
  char timeInHex[16];
  char bitsInHex[16];
  sprintf(extraNonceBuffer, "%u#%li", (unsigned)context->mCurrBlock.height()+1, (long)block->extraNonce);
  stratumLittleEndianHex(context->mCurrBlock.hash().c_str(), context->mCurrBlock.hash().size(), prevHash);
  stratumLittleEndianHex(block->merkle.c_str(), block->merkle.size(), merkleroot);
  stratumLittleEndianHex(block->hashreserved.c_str(), block->hashreserved.size(), reserved);    
  stratumDumpHex((uint32_t)std::max(time(0), block->time), timeInHex);
  stratumDumpHex((uint32_t)block->bits, bitsInHex);
    
  snprintf(message, sizeof(message),
           "{\"id\": null, \"method\": \"mining.notify\", \"params\": [\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", true]}\n",
           extraNonceBuffer,       // JOB_ID = extraNonce
           "04000000",             // VERSION
           prevHash,               // PREVHASH
           merkleroot,             // MERKLEROOT
           reserved,               // RESERVED
           timeInHex,              // TIME
           bitsInHex);             // BITS

  aioWrite(context->base, socket, message, strlen(message), afWaitAll, 0, 0, 0);
  
  StratumTask task;
  task.merkle = block->merkle;
  task.bits = block->bits;
  context->stratumTaskMap[block->extraNonce] = task;
  return true;
}


void stratumSendStats(poolContext *context, StratumWorker &worker)
{
  // TODO: don't use constant 512.0
  double solsPerSec = worker.updateStats() * 512.0;
  
  flatbuffers::FlatBufferBuilder fbb;
  auto statsOffset = CreateStats(fbb,
                                 fbb.CreateString(worker.wallet),
                                 fbb.CreateString(worker.worker),
                                 (unsigned)(solsPerSec*1000.0),
                                 -1,
                                 fbb.CreateString("<unknown>"),
                                 UnitType_GPU,
                                 0,
                                 0);
  fbb.Finish(CreateP2PMessage(fbb, FunctionId_Stats, Data_Stats, statsOffset.Union()));
  if (!context->backend->sendMessage(context->base, fbb.GetBufferPointer(), fbb.GetSize())) {
    fprintf(stderr, "ERROR: can't send stats to backend");
    return;
  }
}
