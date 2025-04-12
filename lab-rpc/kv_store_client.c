/**
 * Client binary.
 */

#include "kv_store_client.h"

#define HOST "localhost"

CLIENT* clnt_connect(char* host) {
  CLIENT* clnt = clnt_create(host, KVSTORE, KVSTORE_V1, "udp");
  if (clnt == NULL) {
    clnt_pcreateerror(host);
    exit(1);
  }
  return clnt;
}

int example(int input) {
  CLIENT *clnt = clnt_connect(HOST);

  int ret;
  int *result;

  result = example_1(&input, clnt);
  if (result == (int *)NULL) {
    clnt_perror(clnt, "call failed");
    exit(1);
  }
  ret = *result;
  xdr_free((xdrproc_t)xdr_int, (char *)result);

  clnt_destroy(clnt);
  
  return ret;
}

char* echo(char* input) {
  CLIENT *clnt = clnt_connect(HOST);
  /* TODO */
  char **result = echo_1(&input, clnt);
  char* ret = strdup(*result);
  xdr_free((xdrproc_t)xdr_string, (char*)result);
  clnt_destroy(clnt);
  return ret;
}

void put(buf key, buf value) {
  CLIENT *clnt = clnt_connect(HOST);
  /* TODO */
  put_request* request = (put_request*)malloc(sizeof(put_request));
  request->key = key;
  request->value = value;
  void* result = put_1(request, clnt);
  xdr_free((xdrproc_t)xdr_int, (char*)result);
  free(request);
  clnt_destroy(clnt);
}

buf* get(buf key) {
  CLIENT *clnt = clnt_connect(HOST);
  /* TODO */
  buf* ret = (buf*)malloc(sizeof(buf));
  buf* result = get_1(&key, clnt);
  ret->buf_len = result->buf_len;
  ret->buf_val = (char*)malloc(result->buf_len);
  memcpy(ret->buf_val, result->buf_val, result->buf_len);
  xdr_free((xdrproc_t)xdr_int, (char*)result);
  clnt_destroy(clnt);
  return ret;
}
