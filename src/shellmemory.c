#include<stdlib.h>
#include<string.h>
#include<stdio.h>
#include"shell.h"
#include"interpreter.h"

struct directory_struct{
	char *directory;
	char *files[100];
	struct directory_struct *directories[100];
	struct directory_struct *parent;
};
struct memory_struct{
	char *var;
	char *value;
};

struct memory_struct shellmemory[1000];
struct directory_struct home;
struct directory_struct *pwd;

// These variables will be initialized with the macro substitution values
//int frame_size = framesize; 
//int var_mem_size = varmemsize;

// The index in shellmemory where the variable store starts
int variable_store= framesize;
int variable_store_boundary = framesize + varmemsize; // The boundry of variable store, excludes the last index 
int LRU_counter[framesize/3]; // An array to keep track of the LRU process

// Helper functions
int match(char *model, char *var) {
	int i, len=strlen(var), matchCount=0;
	for(i=0;i<len;i++)
		if (*(model+i) == *(var+i)) matchCount++;
	if (matchCount == len)
		return 1;
	else
		return 0;
}

char *extract(char *model) {
	char token='=';    // look for this to find value
	char value[1000];  // stores the extract value
	int i,j, len=strlen(model);
	for(i=0;i<len && *(model+i)!=token;i++); // loop till we get there
	// extract the value
	for(i=i+1,j=0;i<len;i++,j++) value[j]=*(model+i);
	value[j]='\0';
	return strdup(value);
}


// Shell memory functions

void mem_init(){
	int i;
	for (i=0; i<1000; i++){		
		shellmemory[i].var = "none";
		shellmemory[i].value = "none";
	}
	memset(LRU_counter, 0, sizeof(LRU_counter));
}
void mem_cleanup(int start, int end){
	int i = start;
	for (i; i<= end; i++){
		shellmemory[i].var = "none";
		shellmemory[i].value = "none";
	}
}

// Set key value pair
int mem_set_value(char *var_in, char *value_in) {
	int i;
	for (i=variable_store; i<variable_store_boundary; i++){
		if (strcmp(shellmemory[i].var, var_in) == 0){
			shellmemory[i].value = strdup(value_in);
			return i;
		} 
	}

	//Value does not exist, need to find a free spot.
	for (i=variable_store; i<variable_store_boundary; i++){
		if (strcmp(shellmemory[i].var, "none") == 0){
			shellmemory[i].var = strdup(var_in);
			shellmemory[i].value = strdup(value_in);
			return i;
		} 
	}

	return -1;

}

void mem_set_value_at_index(char *var_in, char *value_in, int index){
	shellmemory[index].var = strdup(var_in);
	shellmemory[index].value = strdup(value_in);
}

// Set key value pair
int find_contiguous_memory(int entries) { //returns the starting index for a contiguous piece of memory of size entries
	int i;
	int contiguous_slot_count = 0;
	for (i=0; i<1000; i++){
		if (strcmp(shellmemory[i].var, "none") == 0 && strcmp(shellmemory[i].value, "none")==0){
			contiguous_slot_count +=1;
		}
		else contiguous_slot_count = 0;
		if (contiguous_slot_count == entries) return i-contiguous_slot_count+1;
	}
	return -1;
}

//get value based on input key
char *mem_get_value(char *var_in) {
	int i;
	for (i=variable_store; i<variable_store_boundary; i++){
		if (strcmp(shellmemory[i].var, var_in) == 0){
			return strdup(shellmemory[i].value);
		} 
	}
	return NULL;

}
// Takes as input the shellmemory address that was accessed and updates the LRU_counter based on that
void increment_LRU_counter(int address){

	int frame_num = address/3; // The frame number of the instruction that was executed
	// Age all frame access times
	for (int i=0; i<frame_size/3; i++){
		LRU_counter[i]++;
	}
	LRU_counter[frame_num] = 0; // This corresponds to the frame that was just executed
	
}

int find_empty_frame(){ // returns the index of the first empty frame in memory
	// If there is an empty slot

	for(int i=0; i< frame_size; i+=3){
		if (strcmp(shellmemory[i].var, "none") == 0 && strcmp(shellmemory[i].value, "none")==0){
			return i;
		}
	}
	
	

	// Must evict a page
	int LRU_frame = 0; // Variable to hold the index of LRU frame
	// Find the LRU frame
	for (int i=0;i<frame_size/3; i++){
		if (LRU_counter[i]>LRU_counter[LRU_frame]) LRU_frame = i; 
	}
	char evicted_PCB_PID[100]; // Buffer to hold the PID of the frame the frame being evicted
	char* evicted_frame_var = shellmemory[LRU_frame*3].var;


	// Extracting the PID from the frame saved in shellmemory which is being evicted
	int hyphen_index;
	for (int i =0; i< strlen(evicted_frame_var);i++){
		if (evicted_frame_var[i] != '-'){
			hyphen_index = i;
			evicted_PCB_PID[i]= evicted_frame_var[i];
		}
		else evicted_PCB_PID[i]= '\0';
	}
	// Extracting the page entry from the frame saved in shellmemory which is being evicted
	char page_entry[10];
	memcpy(page_entry, evicted_frame_var+hyphen_index, strlen(evicted_frame_var)-hyphen_index);
	page_entry[strlen(evicted_frame_var)-hyphen_index] = '\0';

	
	// Iterating over the ready queue to find the process whose page is getting evicted and updating its pagetable
	struct PCB * temp_PCB = Ready_queue.head->next;
	while(temp_PCB != NULL){
		if (strcmp(temp_PCB->PID, evicted_PCB_PID ) == 0){

			temp_PCB->page_table[atoi(page_entry)] = -1;
			break;
		}
		if(temp_PCB == temp_PCB->next) break;
		temp_PCB = temp_PCB->next;
	}
	
	printf("Page fault! Victim page contents:\n\n");
	for (int i = LRU_frame*3; i<LRU_frame*3+3; i++){
		if(strcmp(shellmemory[i].var, "none") == 0 && strcmp(shellmemory[i].value, "none") ==0) continue; // If a slot within a frame is unused
		printf("%s", shellmemory[i].value);
		// Delete the data
		shellmemory[i].var = "none";
		shellmemory[i].value = "none";
	}
	printf("\nEnd of victim page contents.\n");

	return (LRU_frame*3);
}

void intialize_directory(){
	home.directory = "~";
	pwd = &home;
}