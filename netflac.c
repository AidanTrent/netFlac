#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include "netflac.h"

// Error checking and printing for recv. Returns 1 on error
int recvErrChk(char* funcName, int recvRet){
	if (recvRet == 0){
		fprintf(stderr, "%s : client has closed the connection\n", funcName);
		return 1;
	}
	else if (recvRet == -1){
		perror("recv");
		return -1;
	}
	return 0; // Success
}

struct addrinfo* getNetInfo(char* addr, char* port){
	struct addrinfo aHints;
	struct addrinfo *servInfo;

	memset(&aHints, 0, sizeof(aHints));
	aHints.ai_family = AF_UNSPEC;
	aHints.ai_socktype = SOCK_STREAM; // TCP
	aHints.ai_flags = AI_PASSIVE;

	int status;
	if ((status = getaddrinfo(addr, port, &aHints, &servInfo)) != 0){
		fprintf(stderr,"getaddrinfo : (%s)\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	printf("Discovered %s...\n", addr);
	return servInfo;
}
