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

// TODO : put these into a constants header file
#define SAMPL_PER_SEG 30
#define FILENAME_LEN 256 // Max filename length is effectively 255 due to processing

typedef enum{
	Null,
	Quit,
	Pause,
	Play
} inputCode;

struct recvPCMargs{
	int fd;
	ao_device* device;
	uint32_t segSize;
	inputCode* globalCode;
};

void* usrCtrl(void* globalCodeVoid){
	inputCode* globalCode = globalCodeVoid;
	char usrIn;

	uint8_t reading = 1;
	while(reading){
		printf("netFlac > ");
		usrIn = getchar();
		char bufCh;
		while((bufCh = getchar()) != '\n' && bufCh != EOF); // Clear buffer

		switch (usrIn){
			case 'q':
				*globalCode = Quit;
				printf("Quiting...\n");
				fflush(stdout);
				reading = 0;
				break;
			case Play:
				printf("Playing...\n");
				fflush(stdout);
				break;
			case Pause:
				printf("Pausing...\n");
				fflush(stdout);
				break;
			default:
				printf("Invalid input...\n");
				fflush(stdout);
				break;
		}
	}
	pthread_exit(NULL);
}

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

void* recvPCM(void* args){
	struct recvPCMargs* pcmArgs = args;
	char* seg = malloc(pcmArgs->segSize);

	int16_t recvBytes; // Must be signed for errors
	int segProg;
	uint8_t receiving = 1;
	while (receiving){
		if (*pcmArgs->globalCode == Quit){
			break;
		}

		// TODO : buffering
		segProg = 0;
		while (segProg != pcmArgs->segSize){
			recvBytes = recv(pcmArgs->fd, seg, pcmArgs->segSize - segProg, 0);
			if (recvBytes == -1){
				perror("recv");
				exit(EXIT_FAILURE);
			}
			ao_play(pcmArgs->device, seg, recvBytes);
			segProg += recvBytes;

			// Exit when server is no longer sending
			if (recvBytes == 0){
				segProg = pcmArgs->segSize;
				receiving = 0;
			}
		}
	}

	free(seg);
	close(pcmArgs->fd);
	pthread_exit(NULL);
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
		if (recv(fd, &findingFlac, sizeof(findingFlac), 0) == -1){
			perror("send");
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
		fprintf(stderr, "USAGE : client ADDRESS PORT\n");
		exit(EXIT_FAILURE);
	}

	struct addrinfo *servInfo = getServerInfo(argv[1], argv[2]);
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
	uint32_t segSize = (format.bits * 2) * SAMPL_PER_SEG;
	struct recvPCMargs pcmArgs = {sockfd, device, segSize, globalCode};
	pthread_t pcmThread;
	pthread_create(&pcmThread, NULL, recvPCM, &pcmArgs);

	// Start thread to take user input
	pthread_t usrCtrlThread;
	pthread_create(&usrCtrlThread, NULL, usrCtrl, globalCode);

	// Exiting
	pthread_join(pcmThread, NULL);
	//pthread_join(usrCtrlThread, NULL);
	freeaddrinfo(servInfo);
	ao_close(device);
	ao_shutdown();
	return EXIT_SUCCESS;
}
