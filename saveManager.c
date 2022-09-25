/* Aidan Trent
 * For a save management system. Linked list capable of being saved and loaded
 * off of storage.
 * Limited to 65,535 Kb nodes, easy as changing some data types if an increase
 * is needed though.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

// TODO : Eventually plan on removing pragma pack(1)
#pragma pack(1)
typedef struct Node{
	struct Node* next;
	uint16_t dfBytes; // Number of bytes in data field
	uint8_t data[];
} Node;

typedef struct{
	Node* head;
	Node* cur;
} LList;

// TODO : a function to free the entire list

// Do not define in header, only used by other saveManager functions
Node* makeNode(uint16_t dfBytes, uint8_t* data){
	uint8_t nodeHeaderSize = (sizeof(struct Node*) + sizeof(uint16_t)); // Size of next + dfBytes
	Node* newNode = malloc(nodeHeaderSize + dfBytes);
	if (newNode == NULL){
		fprintf(stderr, "ERROR: malloc fail for newNode @ makeNode\n");
		return(NULL);
	}

	newNode->next = NULL;
	newNode->dfBytes = dfBytes;
	memcpy(&newNode->data, data, dfBytes);

	return newNode;
}

// Make a list containing one node with given params
LList* makeList(uint16_t dfBytes, uint8_t* data){
	LList* newList;
	newList = malloc(sizeof(struct Node*) * 2);
	if (newList == NULL){
		fprintf(stderr, "ERROR: malloc fail for newList @ makeList\n");
		return(NULL);
	}
	Node* newNode = makeNode(dfBytes, data);
	if (newNode == NULL){
		return NULL;
	}

	newList->head = newNode;
	newList->cur = newNode;

	return newList;
}

// Add a Node of given params to top of list
Node* push(LList* list, uint16_t dfBytes, uint8_t* data){
	Node* newNode = makeNode(dfBytes, data);
	if (newNode == NULL){
		return(NULL);
	}

	// Loop to top of list
	while (list->cur->next != NULL){
		list->cur = list->cur->next;
	}
	list->cur->next = newNode;
	return newNode;
}

// Remove node from list
void freeNode(LList* list, Node* node){
	uint8_t reading = 1;
	list->cur = list->head; // Go to start of list

	// If removing head
	if (list->cur == node){
		if (list->cur->next == NULL){
			fprintf(stderr, "ERROR: Cannot free node when list has 1 node @ freeNode\n");
		}
		else{
			list->cur = list->cur->next;
			free(list->head);
			list->head = list->cur;
		}
		reading = 0;
	}

	while (reading){
		// See if at end of list (or if only one node exists)
		// Check if matching
		if (list->cur->next == node){
			// Check if patching is necessary
			if (list->cur->next->next != NULL){
				list->cur->next = list->cur->next->next; // Patch
			}
			else{
				list->cur->next = NULL;
			}
			free(list->cur->next);
			reading = 0;
		}
		else {
			list->cur = list->cur->next; // Continue to next node
		}
	}
}

// Saves an EntityList as a binary file. Returns 1 on failure, 0 success
int saveList(LList* list, char saveName[]){
	FILE* saveFile = fopen(saveName, "wb");
	if (saveFile == NULL){
		fprintf(stderr, "ERROR: failed to save EntityList file @ saveList\n");
		return(1);
	}
	list->cur = list->head; // Go to start of list

	uint8_t reading = 1;
	while (reading){
		fwrite(&list->cur->dfBytes, sizeof(list->cur->dfBytes), 1, saveFile);
		if (fwrite(list->cur->data, list->cur->dfBytes, 1, saveFile) != 1){
			fprintf(stderr, "ERROR: failed to write to saveFile file @ saveList\n");
			fclose(saveFile);
			return(1);
		}

		if (list->cur->next != NULL){
			list->cur = list->cur->next; // Continue to next node
		}
		else{
			reading = 0;
		}
	}

	fclose(saveFile);
	return(0);
}

// Loads a saved EntityList from a file
LList* loadSave(char saveName[]){
	LList* newList = NULL;

	FILE* saveFile = fopen(saveName, "rb");
	if (saveFile == NULL){
		fprintf(stderr, "ERROR: failed to load EntityList file @ loadSave\n");
		return(NULL);
	}

	uint16_t curDFBytes;
	uint8_t* curDataField = NULL;

	uint8_t reading = 1;
	while (reading){
		// Get number of bytes in this node
		if (fread(&curDFBytes, sizeof(curDFBytes), 1, saveFile) != 1){
			// Possibly at end of file
			reading = 0;
			if (newList == NULL){ // Check if file failed to open on first run
				fprintf(stderr, "ERROR: empty saveFile @ loadSave");
				return(NULL);
			}
		}
		// Read this node into memory
		else {
			curDataField = malloc(curDFBytes);
			if (curDataField == NULL){
				fprintf(stderr, "ERROR: malloc fail for curDataField @ loadSave\n");
				return(NULL);
			}
			if (fread(curDataField, 1, curDFBytes, saveFile) != curDFBytes){
				fprintf(stderr, "ERROR: bytesRead is not equal to curDFBytes @ loadSave\n");
				return(NULL);
			}

			if (newList == NULL){
				newList = makeList(curDFBytes, curDataField);
			}
			else{
				push(newList, curDFBytes, curDataField);
			}
		}
	}

	return newList;
}
