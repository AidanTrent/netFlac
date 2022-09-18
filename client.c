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
#include <pthread.h>
#include "netflac.h"

typedef struct{
	int fd;
	inputCode* globalCode;
} usrCtrlArgs;

typedef struct{
	int fd;
	ao_device* device;
	int segSize;
	inputCode* globalCode;
} recvPCMargs;

// Thread pausing
pthread_cond_t playPauseCond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t playPauseLock = PTHREAD_MUTEX_INITIALIZER;
uint8_t paused = 0;

void* usrCtrl(void* voidArgs){
	usrCtrlArgs* args = voidArgs;

	char usrIn;
	uint8_t reading = 1;
	while(reading){
		usrIn = getchar();
		char bufCh;
		while((bufCh = getchar()) != '\n' && bufCh != EOF); // Clear buffer

		switch (usrIn){
			case 'q':
				*args->globalCode = Quit;
				reading = 0;
				printf("Quiting...\n");
				fflush(stdout);
				break;
			case 'p':
				if (paused){
					paused = 0;
					pthread_cond_signal(&playPauseCond);
				}
				else{
					paused = 1;
				}
				*args->globalCode = PlayPause;
				printf("Pause...\n");
				fflush(stdout);
				break;
			default:
				*args->globalCode = Null;
				printf("Invalid input...\n");
				fflush(stdout);
				break;
		}
		if (send(args->fd, args->globalCode, sizeof(inputCode), 0) == -1){
			perror("send");
			exit(EXIT_FAILURE);
		}
	}
	pthread_exit(NULL);
}

void* recvPCM(void* voidArgs){
	recvPCMargs* args = voidArgs;
	char* seg = malloc(args->segSize);

	int recvBytes; // Must be signed for errors
	int segProg;
	uint8_t receiving = 1;
	while (receiving){
		// User input play/pause thread
		while(paused){
			pthread_cond_wait(&playPauseCond, &playPauseLock);
		}

		// TODO : buffering
		// PCM stream
		segProg = 0;
		while (segProg != args->segSize){
			recvBytes = recv(args->fd, seg, args->segSize - segProg, 0);
			recvErrChk("recvPCM", recvBytes);
			// Exit when server is no longer sending
			if (recvBytes == 0){
				segProg = args->segSize;
				receiving = 0;
				break;
			}

			ao_play(args->device, seg, recvBytes);
			segProg += recvBytes;
		}
	}

	free(seg);
	pthread_exit(NULL);
}

// TODO : Proper error handling for recv here.
ao_sample_format recvFormat(int fd){
	ao_sample_format format;

	recv(fd, &format.bits, sizeof(int), 0);
	recv(fd, &format.rate, sizeof(int), 0);
	recv(fd, &format.channels, sizeof(int), 0);

	format.byte_format = AO_FMT_NATIVE; // TODO : delegated by server?

	printf("bps = %d\n", format.bits);
	printf("sample rate = %d\n", format.rate);
	printf("channels = %d\n", format.channels);

	return format;
}

ao_device* openLive(int driver_id, ao_sample_format *format){
	ao_device* device = ao_open_live(driver_id, format, NULL);
	if(device == NULL){
		printf("main : Failure to open live playback\n");
		exit(EXIT_FAILURE);
	}

	return device;
}

// Takes user input to send a file name to the server. Will continue taking user
// input until the server replies that the file exists.
void requestFlac(int fd){
	uint8_t findingFlac = 1;
	while(findingFlac){
		char fileName[FILENAME_LEN];
		printf("Enter the flac's file name : ");
		fgets(fileName, FILENAME_LEN, stdin);

		// Input processing
		int nlnIndex = strcspn(fileName, "\n");
		if (nlnIndex == (FILENAME_LEN - 1)){ // Overflow has occured
			char bufCh;
			while((bufCh = getchar()) != '\n' && bufCh != EOF); // Clear buffer
		}
		else{
			fileName[nlnIndex] = 0; // Remove new line
		}

		// Send file name for check
		if (send(fd, fileName, FILENAME_LEN, 0) == -1){
			perror("send");
			exit(EXIT_FAILURE);
		}

		// Recieve confimation of file's existance
		if (recvErrChk("requestFlac", recv(fd, &findingFlac, sizeof(findingFlac), 0))){
			exit(EXIT_FAILURE);
		}
		if (findingFlac){
			fprintf(stderr, "flac file \"%s\" does not exist on server...\n", fileName);
		}
	}
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

int main(int argc, char* argv[]){
	if (argc != 3) {
		fprintf(stderr, "USAGE : client ADDRESS PORT\n");
		exit(EXIT_FAILURE);
	}

	struct addrinfo *servInfo = getNetInfo(argv[1], argv[2]);
	int sockfd = socketAndConnect(servInfo);

	requestFlac(sockfd);

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

	// Initialize globalCode
	inputCode code = Null;
	inputCode* globalCode = &code;

	// Start thread to receive and play pcm
	int segSize = format.bits * SAMPL_PER_SEG;
	recvPCMargs pcmArgs = {sockfd, device, segSize, globalCode};
	pthread_t pcmThread;
	pthread_create(&pcmThread, NULL, recvPCM, &pcmArgs);

	// Start thread to take user input
	usrCtrlArgs ctrlArgs = {sockfd, globalCode};
	pthread_t usrCtrlThread;
	pthread_create(&usrCtrlThread, NULL, usrCtrl, &ctrlArgs);

	// Exiting
	pthread_join(usrCtrlThread, NULL);
	freeaddrinfo(servInfo);
	close(sockfd);
	ao_close(device);
	ao_shutdown();
	return EXIT_SUCCESS;
}
