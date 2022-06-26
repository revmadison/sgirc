#ifndef _NET_H
#define _NET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>

#include <mbedtls/net.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>

typedef struct {
	int state;
	int encoding;
	int contentLength;
	int contentRead;

	char *getOrPost;

	mbedtls_net_context server_fd;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;
	mbedtls_ssl_context ssl;
	mbedtls_ssl_config conf;
	mbedtls_x509_crt cacert;
} SSLContext;


#define ENCODING_UNKNOWN 0
#define ENCODING_NONE 1
#define ENCODING_CHUNKED 2

unsigned char *fetchURL(char *url, char *headers, int *outBufferLen);
unsigned char *postFile(char *url, char *headers, char *filename, 
	int *outBufferLen);

#endif
