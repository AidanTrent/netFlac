#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <pthread.h>

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define BACKLOG 1
#define SAMPL_PER_SEG 30
#define FILENAME_LEN 50

// TODO : Add a passphrase

// TODO : Proper error handling for send here. An issue here is basically
// impossible but it's still a potential way to "crash" the server
void sendMetadata(int fd, drflac* flacFile){
	// Necessarry casting
	int bps = flacFile->bitsPerSample;
	if (bps > 16){
		bps = 32;
	}
	else{
		bps = 16;
	}
	int rate = flacFile->sampleRate;
	int channels = flacFile->channels;

	if (send(fd, &bps, sizeof(int), 0) == -1){
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
void sendPCM32(int fd, drflac* flacFile){
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
			break;
		}
	}
	close(fd);
}

// TODO : Remainder of all segments results in some truncated data. Fix this
void sendPCM16(int fd, drflac* flacFile){
	// Get PCM data
	uint32_t pcmBytes = (flacFile->totalPCMFrameCount * flacFile->channels * sizeof(uint32_t));

	int16_t* pcmData = malloc(pcmBytes);

	drflac_read_pcm_frames_s16(flacFile, flacFile->totalPCMFrameCount, pcmData);

	// Serialize
	uint32_t segSize = (flacFile->bitsPerSample * 2) * SAMPL_PER_SEG;
	char* seg = malloc(segSize);

	// Send out segments
	for (int x = 0; x < pcmBytes; x += segSize){
		int16_t* pos = &pcmData[x / 2]; // Conversion to index 16 bit
		memcpy(seg, pos, segSize);
		if (send(fd, seg, segSize, 0) == -1){
			perror("send");
			break;
		}
	}
	close(fd);
}

// Receives filenames from the client until there is a match with a flac file
drflac* findFlac(int fd){
	char fileName[FILENAME_LEN];
	uint8_t findingFlac = 1;
	drflac* flacFile;

	while(findingFlac){
		if (recv(fd, fileName, FILENAME_LEN, 0) == 0){
			fprintf(stderr, "findFlac : client has closed the connection\n");
			return NULL;
		}

		flacFile = drflac_open_file(fileName, NULL);
		if (flacFile == NULL){
			fprintf(stderr, "drflac_open_file : issue opening file \"%s\"\n", fileName);
		}
		else{
			findingFlac = 0;
		}

		if (send(fd, &findingFlac, sizeof(findingFlac), 0) == -1){
			perror("send");
			exit(EXIT_FAILURE);
		}
	}
	return flacFile;
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

void* flacRoutine(void* fdArg){
	int* fd = fdArg;

	// Find desired flac file
	drflac* flacFile = findFlac(*fd);

	if (flacFile != NULL){
		// Send flac data
		sendMetadata(*fd, flacFile);

		if (flacFile->bitsPerSample > 16){
			sendPCM32(*fd, flacFile);
		}
		else{
			sendPCM16(*fd, flacFile);
		}

		drflac_close(flacFile);
	}
	pthread_exit(NULL);
}

int main(int argc, char* argv[]){
	if (argc != 2) {
		fprintf(stderr, "USAGE : server PORT...\n");
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
	uint8_t running = 1;
	while(running){
		// Waiting for client
		addrSize = sizeof(clientAddr);
		newfd = accept(sockfd, (struct sockaddr*)&clientAddr, &addrSize);
		if (newfd == -1){
			perror("accept");
			continue;
		}

		// Print out IP addr of connection
		char ip[INET_ADDRSTRLEN];
		struct sockaddr_in* clientAddrConv = (struct sockaddr_in*)&clientAddr;
		inet_ntop(AF_INET, &clientAddrConv->sin_addr, ip, INET_ADDRSTRLEN);
		printf("Accepted client @ %s\n", ip);


		// Begin stream
		pthread_t frThread;
		pthread_create(&frThread, NULL, flacRoutine, &newfd);
		// Exiting
		pthread_join(frThread, NULL);
		//running = 0;
	}

	close(newfd);
	return EXIT_SUCCESS;
}
