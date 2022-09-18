#ifndef NETFLAC
#define NETFLAC

#define SAMPL_PER_SEG 30 // avoid large numbers, could overflow segSize
#define FILENAME_LEN 256 // Max filename length is effectively 255 due to processing

typedef enum{
	Null,
	Quit,
	PlayPause,
} inputCode;

// Error checking and printing for recv. Returns 1 on error
int recvErrChk(char* functName, int recvRet);

struct addrinfo* getNetInfo(char* addr, char* port);
#endif
