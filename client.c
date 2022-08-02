#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <ao/ao.h>

#define SAMPL_PER_SEG 30

ao_sample_format recvFormat(int fd){
	ao_sample_format format;

	recv(fd, &format.bits, sizeof(int), 0);
	recv(fd, &format.rate, sizeof(int), 0);
	recv(fd, &format.channels, sizeof(int), 0);

	format.byte_format = AO_FMT_NATIVE; // TODO : delegated by server?

	printf("Format bits = %d\n", format.bits);
	printf("Format rate = %d\n", format.rate);
	printf("Format channels = %d\n", format.channels);

	return format;
}

void recvPCM(int fd, ao_device* device, uint32_t segSize){
	char* seg = malloc(segSize);

	int16_t recvBytes; // Must be signed for errors
	int segProg;
	uint8_t recieving = 1;
	while (recieving){
		segProg = 0;
		while (segProg != segSize){
			recvBytes = recv(fd, seg, segSize - segProg, 0);
			if (recvBytes == -1){
				perror("recv");
				exit(EXIT_FAILURE);
			}
			ao_play(device, seg, recvBytes);
			segProg += recvBytes;

			// Exit when server is no longer sending
			if (recvBytes == 0){
				segProg = segSize;
				recieving = 0;
			}
		}

	}

	free(seg);
	close(fd);
}

ao_device* openLive(int driver_id, ao_sample_format *format){
	ao_device* device = ao_open_live(driver_id, format, NULL);
	if(device == NULL){
		printf("main : Failure to open live playback\n");
		exit(EXIT_FAILURE);
	}

	return device;
}

int socketAndConnect(struct addrinfo* servInfo){
	// Find suitible address
	struct addrinfo* cur;
	int sockfd;
	for(cur = servInfo; cur != NULL; cur = cur->ai_next){
		sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (sockfd == -1){
			perror("socket");
			exit(EXIT_FAILURE);
		}
	}

	if (connect(sockfd, servInfo->ai_addr, servInfo->ai_addrlen) == -1){
		perror("connect");
		exit(EXIT_FAILURE);
	}
	printf("Connected!\n");

	return sockfd;
}

// Returns an addrinfo for the (supposed) server
struct addrinfo* getServerInfo(char* addr, char* port){
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

	printf("Found %s...\n", addr);
	return servInfo;
}

int main(int argc, char* argv[]){
	if (argc != 3) {
		fprintf(stderr, "USAGE : socketTest ADDRESS PORT\n");
		exit(EXIT_FAILURE);
	}

	struct addrinfo *servInfo = getServerInfo(argv[1], argv[2]);
	int sockfd = socketAndConnect(servInfo);

	// Perpare audio
	int driver_id;
	ao_device* device;

	// Initialize audio output library
	ao_initialize();

	// Perpare driver
	driver_id = ao_default_driver_id();
	if (driver_id == -1){
		printf("main : No useable audio output found\n");
		exit(EXIT_FAILURE);
	}

	// Set up format
	ao_sample_format format = recvFormat(sockfd);

	// Open live playback
	device = openLive(driver_id, &format);

	// Receive and play pcm
	uint32_t segSize = (format.bits * 2) * SAMPL_PER_SEG;
	recvPCM(sockfd, device, segSize);

	// Exiting
	freeaddrinfo(servInfo);
	ao_close(device);
	ao_shutdown();
	return EXIT_SUCCESS;
}
