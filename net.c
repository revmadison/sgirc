#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>

#include "net.h"

#define REQ_FMT "%s /%s HTTP/1.1\r\nHost: %s\r\n%s\r\n"

static void my_debug(void *ctx, int level, const char *file, int line, const char *str)
{
	fprintf((FILE *)ctx, "%s:%04d: %s", file, line, str);
	fflush((FILE *)ctx);
}


int initSSLContext(SSLContext *ctx)
{
	const char *pers = "httpclient";
	ctx->state = 0;
	ctx->encoding = ENCODING_UNKNOWN;
	ctx->contentLength = -1;
	ctx->contentRead = 0;

	ctx->getOrPost = strdup("GET");

	mbedtls_net_init(&ctx->server_fd);
	mbedtls_ssl_init(&ctx->ssl);
	mbedtls_ssl_config_init(&ctx->conf);
	mbedtls_x509_crt_init(&ctx->cacert);
	mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
	mbedtls_entropy_init(&ctx->entropy);

	mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy, (const unsigned char *)pers, strlen(pers));
	return 0;
}
int killSSLContext(SSLContext *ctx)
{
	free(ctx->getOrPost);
	mbedtls_net_free(&ctx->server_fd);
	mbedtls_ssl_free(&ctx->ssl);
	mbedtls_x509_crt_free(&ctx->cacert);
	mbedtls_ssl_config_free(&ctx->conf);
	mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
	mbedtls_entropy_free(&ctx->entropy);
	return 0;
}

int sslConnect(SSLContext *ctx, char *hostname, char *port)
{
	int ret;

	if ((ret = mbedtls_net_connect(&ctx->server_fd, hostname, port, MBEDTLS_NET_PROTO_TCP)) != 0)
	{
		//printf("Failed to connect.\n");
		killSSLContext(ctx);
		return 1;
	}
	if ((ret = mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		//printf("Failed to configure SSL.\n");
		killSSLContext(ctx);
		return 2;
	}
	// As insecure as it gets!
	mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
	mbedtls_ssl_conf_ca_chain(&ctx->conf, &ctx->cacert, NULL);
	mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
	mbedtls_ssl_conf_dbg(&ctx->conf, my_debug, stdout);
	if ((ret = mbedtls_ssl_setup(&ctx->ssl, &ctx->conf)) != 0)
	{
		//printf("failed to setup SSL.\n");
		killSSLContext(ctx);
		return 3;
	}

	if ((ret = mbedtls_ssl_set_hostname(&ctx->ssl, "httpclient")) != 0)
	{
		//printf("failed\n mbedtls_ssl_set_hostname returned %d\n", ret);
		killSSLContext(ctx);
		return 4;
	}
	mbedtls_ssl_set_bio(&ctx->ssl, &ctx->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

	while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0)
	{
		if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
		{
			//printf("failed!\n");
			killSSLContext(ctx);
			return 5;
		}
	}

	return 0;
}

int readLine(SSLContext *ctx, unsigned char *line, const int lineLen)
{
	// First read headers
	int lineAt = 0;
	int ret;
	line[0] = 0;

	while(1) {
		ret = mbedtls_ssl_read(&ctx->ssl, line+lineAt, 1);
		if(ret <= 0) {
			printf("Error reading header!\n");
			return -1;
		}
		if(line[lineAt] == '\r') {
			// Do nothing...
		} else if(line[lineAt] == '\n') {
			line[lineAt] = 0;
			return lineAt;
		} else {
			lineAt++;
			if(lineAt >= lineLen) {
				return lineAt;
			}
		}
	}	
}

int parseHeader(SSLContext *ctx)
{
	// First read headers
	int ret;
	unsigned char line[1024];
	
	ret = readLine(ctx, line, 1023);
	if(ret <= 0) {
		return ret;
	}

	if(strstr((char *)line, "Transfer-Encoding: chunked")) {
		ctx->encoding = ENCODING_CHUNKED;
//		printf("Using chunked encoding...\n");
	} else if(strstr((char *)line, "Content-Length:")) {
		char *at = strchr((char *)line, ':');
		int len = 0;
		while(*at) {
			char c = *at;
			if(c >= '0' && c <= '9') {
				len = (len*10) + (c-'0');
			}
			at++;
		}
		//printf("Parsed content-length of %d\n", len);
		ctx->contentLength = len;
	}
		
	//printf("Header: '%s'\n", line);
	return 1;
}

unsigned char *readChunkedBody(SSLContext *ctx, int *outBufferLen) {
	unsigned char line[1024];
	unsigned char *buffer;
	int bufferAt;
	int ret, len;

	ret = readLine(ctx, line, 1023);
	if(ret <= 0) {
		printf("Failed to read size of chunked body\n");
		return NULL;
	}
	
	len = 0;
	for(int i = 0; i < ret; i++) {
		char c = line[i];
		if(c >= '0' && c <= '9') {
			len = (len<<4) + (c-'0');
		} else if(c >= 'A' && c <= 'F') {
			len = (len<<4) + (c-'A') + 10;
		} else if(c >= 'a' && c <= 'f') {
			len = (len<<4) + (c-'a') + 10;
		}
	}
//	printf("Parsed chunk length of %d (%x) from %s\n", len, len, line);

	buffer = (unsigned char *)malloc(len+1);
	bufferAt = 0;
	while(bufferAt < len) {
		ret = mbedtls_ssl_read(&ctx->ssl, buffer+bufferAt, len-bufferAt);
		if(ret <= 0) {
			printf("Failed to read chunk.\n");
			free(buffer);
			return NULL;
		} else {
			bufferAt += ret;
		}
	}
	buffer[bufferAt] = 0;

	if(outBufferLen) {
		*outBufferLen = bufferAt;
	}
	return buffer;
}

unsigned char *readBody(SSLContext *ctx, int *outBufferLen) {
	unsigned char *buffer;
	int bufferAt;
	int ret, len;

	len = ctx->contentLength;

	buffer = (unsigned char *)malloc(len+1);
	bufferAt = 0;
	while(bufferAt < len) {
		ret = mbedtls_ssl_read(&ctx->ssl, buffer+bufferAt, len-bufferAt);
		if(ret <= 0) {
			printf("Failed to read chunk.\n");
			free(buffer);
			return NULL;
		} else {
			bufferAt += ret;
		}
	}
	buffer[bufferAt] = 0;

	if(outBufferLen) {
		*outBufferLen = bufferAt;
	}
	return buffer;
}

unsigned char *fetchURL(char *url, char *headers, int *outBufferLen) {
	char *host = strdup(url+8);
	char *port = strchr(host, ':');
	char *slash = strchr(host, '/');
	unsigned char *retBuffer = NULL;
	int ret, len;
	char buffer[1024];
	char *empty = "";
	SSLContext ctx;
	int isHttp = 0;

	initSSLContext(&ctx);

	if(strstr(url, "http://") == url) {
		isHttp = 1;

		host = strdup(url+7);
		port = strchr(host, ':');
		slash = strchr(host, '/');
	} else if(strstr(url, "https://") != url) {
		printf("URL doesn't start with https:// - %s\n", url);
		return NULL;
	}

	if(port && slash && port < slash) {
		*port = 0;
		port++;
	}
	if(slash) {
		*slash = 0;
		slash++;
		//printf("Request path: %s\n", slash);
	} else {
		slash = empty;
	}

	//printf("\n . Connecting to %s:%s..", host, port?port:(isHttp?"80":"443"));

	ret = sslConnect(&ctx, host, port?port:(isHttp?"80":"443"));
	if(ret) {
		printf("FAILED with code %d\n", ret);
		return 0;
	}

	snprintf(buffer, 1023, REQ_FMT, ctx.getOrPost, slash, host, headers?headers:"");
	
	len = strlen(buffer);
	while ((ret = mbedtls_ssl_write(&ctx.ssl, (unsigned char *)buffer, len)) <= 0)
	{
		if (ret != 0)
		{
			printf("failed to write data!\n");
			killSSLContext(&ctx);
			return NULL;
		}
	}

	while((ret = parseHeader(&ctx)) > 0) {
		// Parsing a header...
	}
	if(ret < 0) {
		printf("last parseHeader returned %d\n", ret);
	}

	if(ctx.encoding == ENCODING_CHUNKED) {
		retBuffer = readChunkedBody(&ctx, outBufferLen);
	} else if(ctx.contentLength > 0) {
		retBuffer = readBody(&ctx, outBufferLen);
	}
	killSSLContext(&ctx);
	return retBuffer;

}
