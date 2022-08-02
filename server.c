#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>

#define DR_FLAC_IMPLEMENTATION
#include <dr_flac.h>

#define BACKLOG 1
#define SAMPL_PER_SEG 30
#define FILE_NAME "lindy.flac"

void sendMetadata(int fd, drflac* flacFile){
	// Necessarry casting
	int bits = flacFile->bitsPerSample * 2;
	int rate = flacFile->sampleRate;
	int channels = flacFile->channels;

	if (send(fd, &bits, sizeof(int), 0) == -1){
		perror("send");
		exit(EXIT_FAILURE);
	}
	if (send(fd, &rate, sizeof(int), 0) == -1){
		perror("send");
		exit(EXIT_FAILURE);
	}
	if (send(fd, &channels, sizeof(int), 0) == -1){
		perror("send");
		exit(EXIT_FAILURE);
	}
}

// TODO : Remainder of all segments results in some truncated data. Fix this
void sendPCM(int fd, drflac* flacFile){
	// Get PCM data
	uint32_t pcmBytes = (flacFile->totalPCMFrameCount * flacFile->channels * sizeof(uint32_t));
	int32_t* pcmData = malloc(pcmBytes);
	drflac_read_pcm_frames_s32(flacFile, flacFile->totalPCMFrameCount, pcmData);

	// Serialize
	uint32_t segSize = (flacFile->bitsPerSample * 2) * SAMPL_PER_SEG;
	char* seg = malloc(segSize);

	// Send out segments
	for (int x = 0; x < pcmBytes; x += segSize){
		int32_t* pos = &pcmData[x / 4]; // Conversion to index 32 bit
		memcpy(seg, pos, segSize);
		if (send(fd, seg, segSize, 0) == -1){
			perror("send");
			exit(EXIT_FAILURE);
		}
	}
	close(fd);
}

// Finds suitible socket and binds it
int socketAndBind(struct addrinfo* localInfo){
	struct addrinfo* cur;
	int sockfd;
	for(cur = localInfo; cur != NULL; cur = localInfo->ai_next){
		sockfd = socket(cur->ai_family, cur->ai_socktype, cur->ai_protocol);
		if (sockfd == -1){
			perror("socket");
			continue;
		}
		// Free port before attempting bind
		int on = 1;
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
			perror("setsockopt");
			exit(1);
		}
		if (bind(sockfd, cur->ai_addr, cur->ai_addrlen) == -1){
			perror("bind");
			exit(EXIT_FAILURE);
		}
		break;
	}
	if (cur == NULL){
		fprintf(stderr, "failed to bind\n");
		exit(EXIT_FAILURE);
	}

	return sockfd;
}

// Returns an addrinfo for the local device
struct addrinfo* getLocalInfo(char* port){
	struct addrinfo aHints;
	struct addrinfo* localInfo;

	memset(&aHints, 0, sizeof(aHints));
	aHints.ai_family = AF_UNSPEC;
	aHints.ai_socktype = SOCK_STREAM; // TCP
	aHints.ai_flags = AI_PASSIVE;

	int status;
	if ((status = getaddrinfo("0.0.0.0", port, &aHints, &localInfo)) != 0){
		fprintf(stderr, "getaddrinfo : (%s)\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	return localInfo;
}

int main(int argc, char* argv[]){
	if (argc != 2) {
		fprintf(stderr, "USAGE : socketTest ADDRESS...\n");
		exit(EXIT_FAILURE);
	}

	struct addrinfo* localInfo = getLocalInfo(argv[1]);
	int sockfd = socketAndBind(localInfo);
	freeaddrinfo(localInfo);

	// Begin listening
	if (listen(sockfd, BACKLOG) == -1){
		perror("listen");
		exit(EXIT_FAILURE);
	}
	printf("Listening on port %s\n", argv[1]);

	// Accept connections
	struct sockaddr_storage clientAddr;
	socklen_t addrSize;
	int newfd;
	uint8_t waiting = 1;
	while(waiting){
		addrSize = sizeof(clientAddr);
		newfd = accept(sockfd, (struct sockaddr*)&clientAddr, &addrSize);
		if (newfd == -1){
			perror("accept");
			continue;
		}
		drflac* flacFile = drflac_open_file(FILE_NAME, NULL);
		sendMetadata(newfd, flacFile);
		sendPCM(newfd, flacFile);
		drflac_close(flacFile);
		waiting = 0;
	}

	close(newfd);
	return EXIT_SUCCESS;
}