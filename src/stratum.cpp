#include "stratum.h"
#include "jansson.h"

static const char *decodeAsCString(json_t *in)
{
  if (json_is_string(in)) {
    return json_string_value(in);
  } else if (json_is_null(in)) {
    return "";
  }
  
  return 0;
}

static bool decodeAsString(json_t *in, std::string &out)
{
  if (json_is_string(in)) {
    out = json_string_value(in);
    return true;
  } else if (json_is_null(in)) {
    out.clear();
    return true;
  }
  
  return false;
}

static bool decodeAsInteger(json_t *in, int64_t *out)
{
  if (json_is_integer(in)) {
    *out = json_integer_value(in);
    return true;
  } else if (json_is_string(in)) {
    const char *s = json_string_value(in);
    if (s[0] >= '0' && s[0] <= '9') {
      *out = xatoi<int64_t>(s);
      return true;
    }
  }
  
  return false;
}

static bool readHex(const char *in, uint8_t *out, size_t outSizeInBytes)
{
  const char *p = in;
  uint8_t *o = out;
  int state = 0;
  size_t offset = 0;
  uint8_t X = 0;
  while (*p) {
    if (*p >= '0' && *p <= '9')
      X |= (*p - '0');
    else if (*p >= 'A' && *p <= 'F')
      X |= (*p - 'A' + 10);
    else if (*p >= 'a' && *p <= 'f')
      X |= (*p - 'a' + 10);
    else
      return false;
    
    state ^= 1;
    if (state == 0) {
      *o++ = X;
      X = 0;
    } else {
      X <<= 4;
    }
    
    p++;
  }
  
  return state == 0;
}

static bool readHex(const char *in, std::vector<uint8_t> &out)
{
  out.clear();
  const char *p = in;
  int state = 0;
  size_t offset = 0;
  uint8_t X = 0;
  while (*p) {
    if (*p >= '0' && *p <= '9')
      X |= (*p - '0');
    else if (*p >= 'A' && *p <= 'F')
      X |= (*p - 'A' + 10);
    else if (*p >= 'a' && *p <= 'f')
      X |= (*p - 'a' + 10);
    else
      return false;
    
    state ^= 1;
    if (state == 0) {
      out.push_back(X);
      X = 0;
    } else {
      X <<= 4;
    }
    
    p++;
  }
  
  return state == 0;  
}

StratumDecodeStatusTy decodeStratumMessage(const char *in, StratumMessage *out)
{
  json_error_t jsonError;
  json_t *r = json_loads(in, 0, &jsonError);
  if (!r)
    return StratumDecodeStatusTy::JsonError;
  
  json_t *id = json_object_get(r, "id");
  json_t *method = json_object_get(r, "method");
  json_t *params = json_object_get(r, "params");
  
  if (id && json_is_integer(id)) {
    out->id = json_integer_value(id);
  } else {
    json_delete(r);    
    return StratumDecodeStatusTy::FormatError;
  }
  
  const char *methodName;
  if (method && json_is_string(method)) {
    methodName = json_string_value(method);
  } else {
    json_delete(r);
    return StratumDecodeStatusTy::FormatError;
  }
  
  if (!(params && json_is_array(params))) {
    json_delete(r);
    return StratumDecodeStatusTy::FormatError;
  }
  
  const char *time;
  const char *nonce;
  const char *equihashSolution;
  if (strcmp(methodName, "mining.subscribe") == 0 && json_array_size(params) >= 4) {
    out->method = StratumMethodTy::Subscribe;
    if ( !(decodeAsString(json_array_get(params, 0), out->subscribe.minerUserAgent) &&
           decodeAsString(json_array_get(params, 1), out->subscribe.sessionId) &&
           decodeAsString(json_array_get(params, 2), out->subscribe.connectHost) &&
           decodeAsInteger(json_array_get(params, 3), &out->subscribe.connectPort)) )
      return StratumDecodeStatusTy::FormatError;
  } else if (strcmp(methodName, "mining.authorize") == 0 && json_array_size(params) >= 1) {
    out->method = StratumMethodTy::Authorize;
    if ( !(decodeAsString(json_array_get(params, 0), out->authorize.login)) )
      return StratumDecodeStatusTy::FormatError;    
  } else if (strcmp(methodName, "mining.extranonce.subscribe") == 0) {
    out->method = StratumMethodTy::ExtraNonceSubscribe;
  } else if (strcmp(methodName, "mining.submit") == 0 && json_array_size(params) >= 5) {
    if ( !(decodeAsString(json_array_get(params, 0), out->submit.workerName) &&
           decodeAsString(json_array_get(params, 1), out->submit.jobId) &&
           (time = decodeAsCString(json_array_get(params, 2))) &&
           (nonce = decodeAsCString(json_array_get(params, 3))) &&
           (equihashSolution = decodeAsCString(json_array_get(params, 4)))) )
      return StratumDecodeStatusTy::FormatError;
    if (!readHex(time, (uint8_t*)&out->submit.time, 4) ||
        !readHex(nonce, out->submit.nonce, 28) ||
        !readHex(equihashSolution, out->submit.equihashSolution))
      return StratumDecodeStatusTy::FormatError;
    out->method = StratumMethodTy::Submit;
  } else {
    json_delete(r);    
    return StratumDecodeStatusTy::FormatError;
  }
  
  json_delete(r);  
  return StratumDecodeStatusTy::Ok;
}

void stratumLittleEndianHex(const char *in, size_t inSize, char *out)
{
  for (size_t i = 0; i < inSize/2; i++) {
    out[i*2] = in[inSize-i*2-2];
    out[i*2+1] = in[inSize-i*2-1];
  }
  
  out[inSize] = 0;
}
