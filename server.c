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
#include <signal.h>

#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#define BACKLOG 1
#include "netflac.h"

typedef struct{
	int fd;
	inputCode* globalCode;
} clientInArgs;

typedef struct{
	int fd;
	inputCode* globalCode;
	drflac* flacFile;
} flacRoutineArgs;

// TODO : Add a passphrase

// Listens to input from client
void* clientIn(void* voidArgs){
	clientInArgs* args = voidArgs;

	int recvRet;
	while (*args->globalCode != Quit){
		// Process user control from client
		if (recvErrChk("clientIn", recv(args->fd, args->globalCode, sizeof(inputCode), 0))){
			break;
		}
		fprintf(stderr, "new clientCode : %d\n", *args->globalCode);
	}
	pthread_exit(NULL);
}

// TODO : Remainder of all segments results in some truncated data. Fix this
void sendPCM32(int fd, drflac* flacFile){
	// Get PCM data
	uint32_t pcmBytes = (flacFile->totalPCMFrameCount * flacFile->channels * sizeof(uint32_t));
	int32_t* pcmData = malloc(pcmBytes);

	drflac_read_pcm_frames_s32(flacFile, flacFile->totalPCMFrameCount, pcmData);

	// Serialize
	int segSize = flacFile->bitsPerSample * SAMPL_PER_SEG;
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
	free(seg);
}

// TODO : Remainder of all segments results in some truncated data. Fix this
void sendPCM16(int fd, drflac* flacFile){
	// Get PCM data
	uint32_t pcmBytes = (flacFile->totalPCMFrameCount * flacFile->channels * sizeof(uint32_t));
	int16_t* pcmData = malloc(pcmBytes);

	drflac_read_pcm_frames_s16(flacFile, flacFile->totalPCMFrameCount, pcmData);

	// Serialize
	int segSize = flacFile->bitsPerSample * SAMPL_PER_SEG;
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
	free(seg);
}

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

void* flacRoutine(void* voidArgs){
	flacRoutineArgs* args = voidArgs;

	// Send flac data
	sendMetadata(args->fd, args->flacFile);

	// Begin stream of 16/32 bit PCM
	if (args->flacFile->bitsPerSample > 16){
		sendPCM32(args->fd, args->flacFile);
	}
	else{
		sendPCM16(args->fd, args->flacFile);
	}

	// Wrap up flac file and exit thread
	drflac_close(args->flacFile);
	pthread_exit(NULL);
}

// Receives filenames from the client until there is a match with a flac file
drflac* findFlac(int fd){
	char fileName[FILENAME_LEN];
	uint8_t findingFlac = 1;
	drflac* flacFile;

	int recvRet;
	while(findingFlac){
		// Recieve file name from client
		if (recvErrChk("findFlac", recv(fd, fileName, FILENAME_LEN, 0))){
			return NULL;
		}

		// Test file name
		flacFile = drflac_open_file(fileName, NULL);
		if (flacFile == NULL){
			fprintf(stderr, "drflac_open_file : issue opening file \"%s\"\n", fileName);
		}
		else{
			findingFlac = 0;
		}

		// Tell client if the file exists or not
		if (send(fd, &findingFlac, sizeof(findingFlac), 0) == -1){
			perror("send");
			return NULL;
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
			exit(EXIT_FAILURE);
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

int main(int argc, char* argv[]){
	if (argc != 2) {
		fprintf(stderr, "USAGE : server PORT...\n");
		exit(EXIT_FAILURE);
	}

	// Ready networking
	struct addrinfo* localInfo = getNetInfo("0.0.0.0", argv[1]);
	int sockfd = socketAndBind(localInfo);
	freeaddrinfo(localInfo);
	signal(SIGPIPE, SIG_IGN); // Ignore sigpipe

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


		// Let client request flac
		drflac* flacFile = findFlac(newfd);
		if (flacFile == NULL){
			continue;
		}

		// Perpare variables and args for threads
		inputCode code = Null;
		inputCode* globalCode = &code;
		flacRoutineArgs routineArgs = {newfd, globalCode, flacFile};
		clientInArgs inArgs = {newfd, globalCode};

		// Begin stream of this flac
		pthread_t routineThread;
		pthread_create(&routineThread, NULL, flacRoutine, &routineArgs);

		// Listen for client input
		pthread_t clientInThread;
		pthread_create(&clientInThread, NULL, clientIn, &inArgs);

		// Wait for stream completion
		pthread_join(clientInThread, NULL);
		close(newfd);
	}

	close(sockfd);
	return EXIT_SUCCESS;
}
