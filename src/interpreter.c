#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include <unistd.h> //new included library for chdir
#include <sys/stat.h> //new included library for stat
#include "shellmemory.h"
#include "shell.h"
#include <pthread.h>
#include <semaphore.h>

struct PCB{
	char *PID;
	int memory_location;
	int PC;
	int num_of_instructions;
	struct PCB * next;
	int job_score;
	int executing;
	int page_table[100];
	int instructions_loaded;
};

struct Ready_queue{
	struct PCB * head;
	struct PCB * tail;
	int num_PCBs;
};

struct Ready_queue Ready_queue = {NULL, NULL, 0};

int MAX_ARGS_SIZE = 7;
pthread_t p1, p2;
pthread_mutex_t lock;
sem_t fullqueue;
int quit_fullqueue; // True if quit is called when the ready queue is not empty
int MT_in_process =0; // True if there is an MT execution in process

int badcommand(){
	printf("%s\n", "Unknown Command");
	return 1;
}


int badcommandTooManyTokens(){
	printf("%s\n", "Bad command: Too many tokens");
	return 2;
}

int badcommandFileDoesNotExist(){
	printf("%s\n", "Bad command: File not found");
	return 1;
}

int badcommandMkdir(){
	printf("%s\n", "Bad command: my_mkdir");
	return 4;
}

int badcommandCd(){
	printf("%s\n", "Bad command: my_cd");
	return 5;
}

int help();
int quit();
int set(char* var, char* value);
int print(char* var);
int run(char* script);
int badcommandFileDoesNotExist();
int echo(char *token);
int my_ls();
int my_mkdir(char *name);
int my_touch(char *name);
int my_cd(char *name);
void FCFS_scheduler();
int exec(char* command_args[], int args_size);
struct PCB * create_PCB(char *script);
int compare_PCBs(const void * PCB_1, const void * PCB_2);
int get_num_instructions(char * filename);
void RR_scheduler();
void AGING_scheduler();
struct PCB * create_background_PCB(char * instructions[], int num_instructions);
void add_to_front_ready_queue(struct PCB * pcb);
void RR30_scheduler();
void * MT_RR_scheduler();
void * MT_RR30_scheduler();
void copyf_to_backingstore(char * fname);
void load_to_framestore(struct PCB * pcb);
void load2_to_framestore(struct PCB * pcb);

// Interpret commands and their arguments
int interpreter(char* command_args[], int args_size){
	int i;

	if ( args_size < 1){
		return badcommand();
	}
	if (args_size > MAX_ARGS_SIZE){
		return badcommandTooManyTokens();
	}

	if (args_size > MAX_ARGS_SIZE){
		return badcommandTooManyTokens();
	}

	for (int i=0; i<args_size; i++){ //strip spaces new line etc
		command_args[i][strcspn(command_args[i], "\r\n")] = 0;
	}

	if (strcmp(command_args[0], "help")==0){
	    //help
	    if (args_size != 1) return badcommand();
	    return help();
	
	} else if (strcmp(command_args[0], "quit")==0) {
		//quit
		if (args_size != 1) return badcommand();
		return quit();

	} else if (strcmp(command_args[0], "set")==0) {
		//set
		if (args_size < 3) return badcommand();
		if (args_size > 7) {
			printf("Bad command: Too many tokens\n");
			return 1;
		}

		char temp_string[100]; //An array to hold concatenated tokens
		strcpy(temp_string, command_args[2]); // Copying first token into the array

		// This loop will execute if there are more than one token to be set to the variable
		for (int i = 3; i < args_size; i++){ 
			int temp_string_length = strlen(temp_string);
			int token_length =strlen(command_args[i]); //Length of the token to be appended
			temp_string[temp_string_length] = ' '; // Adding a space char to seperate tokens within the array

			for (int j = 0; j < token_length; j++){ // Copies the token to the array
				temp_string[j+temp_string_length+1] = command_args[i][j];
			}
			temp_string[temp_string_length+1+token_length] = '\0';
		}
		return set(command_args[1], temp_string);
	
	} else if (strcmp(command_args[0], "print")==0) {
		if (args_size != 2) return badcommand();
		return print(command_args[1]);
	
	} else if (strcmp(command_args[0], "run")==0) {
		if (args_size != 2) return badcommand();
		return run(command_args[1]);
	
	} else if (strcmp(command_args[0], "echo")==0){
		if (args_size != 2) return badcommand();
		return echo(command_args[1]);
		
	} else if (strcmp(command_args[0], "my_ls")==0) {
		if (args_size != 1) return badcommand();
		return(my_ls());
	} else if  (strcmp(command_args[0], "my_mkdir")==0){
		if (args_size !=2) return badcommand();
		return my_mkdir(command_args[1]);

	} else if ((strcmp(command_args[0], "my_touch")==0)){
		if (args_size != 2) return badcommand();
		return my_touch(command_args[1]);
	} else if ((strcmp(command_args[0], "my_cd")==0)){
		if (args_size != 2) return badcommand();
		return my_cd(command_args[1]);
	} else if (strcmp(command_args[0], "exec") ==0){
		if (args_size > 7 || args_size < 3) return badcommand();
		return exec(command_args, args_size);
	}
	badcommand();
}
//Adds PCB to ready queue
void add_to_ready_queue(struct PCB * PCB){
	if (Ready_queue.head == NULL){
		Ready_queue.head = PCB;
		Ready_queue.tail = PCB;
	}
	else{
		Ready_queue.tail->next = PCB; // Make the current tail point to the PCB
		Ready_queue.tail = PCB; //Make the PCB the new tail
	}
	Ready_queue.num_PCBs = Ready_queue.num_PCBs +1;
}

int exec(char* command_args[], int args_size){
	char* POLICIES[] = {"FCFS", "SJF", "RR", "AGING", "RR30"};
	int scheduling_policy; // Will hold an index representing one of the secheduling policies in the above array
	int background_exec = 0;
	int MT = 0;
	if (Ready_queue.num_PCBs == 0) MT_in_process =0;

	if (strcmp(command_args[args_size-1], "#") == 0 || strcmp(command_args[args_size-2], "#") == 0) background_exec=1; // Check if it is a background execution
	if (strcmp(command_args[args_size-1], "MT") == 0) MT = 1;
	// Checking if the policy entered is valid
	int i;
	int valid_policy = 0;
	for (i=0; i<5; i++){
		if (strcmp(POLICIES[i], command_args[args_size-1-background_exec-MT]) ==0 ) {//If the policy is a valid policy
			valid_policy =1;
			scheduling_policy = i;
		} 
	}
	if (valid_policy ==0) {
		printf("Invalid scheduling policy\n");
		return 1;
	}


	/*
	// Checking if there are duplicate scripts
	int j;
	for (i=1; i<args_size-1-background_exec-MT; i++){
		for (j=i+1; j<args_size-1-background_exec-MT; j++){
			if (strcmp(command_args[i], command_args[j]) == 0){
				printf("Bad command: same file name\n");
				return 1;
			}
		}
	}
	*/

	//Loading the scripts into shell memory and creating the PCBs and adding them an array
	struct PCB * temp_PCB;
	struct PCB * PCB_arr[args_size-2];
	for (i=1; i<args_size-1-background_exec-MT; i++){
		temp_PCB = create_PCB(command_args[i]); //Creating new PCB
		copyf_to_backingstore(command_args[i]);
		load2_to_framestore(temp_PCB);
		if (scheduling_policy == 3) temp_PCB->job_score = temp_PCB-> num_of_instructions; // If AGING then add jobscore
		PCB_arr[i-1] = temp_PCB;
	}
	int number_of_PCBs = i-1; //The number of PCBs
	
	struct PCB * background_PCB; // Pointer to hold the PCB of the shell
	if (background_exec){ // If background execution then create the PCB for the shell
		char userInput[1000];		// user's input stored here
		int errorCode = 0;					// zero means no error, default
		char * background_instructions[500]; // Array to hold the instructions given from the background (shell)
		//init user input
		for (int i=0; i<1000; i++) userInput[i] = '\0';
		int i=0;
		while (fgets(userInput, 999, stdin) != NULL){ // Read the instructions from shell stdin
			background_instructions[i] = strdup(userInput);
			i++;
		}
		background_PCB = create_background_PCB(background_instructions, i); // Create a PCB for the shell/background execution
		load2_to_framestore(background_PCB);
	}

	switch(scheduling_policy){
		case 0 : // If FCFS
			for (i=0; i<number_of_PCBs; i++){
				add_to_ready_queue(PCB_arr[i]);
			}
			if (background_exec) add_to_front_ready_queue(background_PCB);
			FCFS_scheduler();
			break;
		case 1 : // SJF
			qsort(PCB_arr, number_of_PCBs, sizeof(PCB_arr[0]), compare_PCBs); // Sorting in ascending order according to job len
			for (i=0; i<number_of_PCBs; i++){
				add_to_ready_queue(PCB_arr[i]);
			}
			if (background_exec) add_to_front_ready_queue(background_PCB);
			FCFS_scheduler();
			break;
		case 2 : // RR
			for (i=0; i<number_of_PCBs; i++){
				add_to_ready_queue(PCB_arr[i]);
			}
			if (background_exec) add_to_front_ready_queue(background_PCB);
			if (MT) {
				if (MT_in_process==1) { // If there is an MT execution in process
					// Post the semaphore for the number of new processes
					for(int i =0; i<number_of_PCBs; i++){
						sem_post(&fullqueue);
					}
					return 0;
				}
				sem_init(&fullqueue,0,Ready_queue.num_PCBs);
				MT_in_process =1;
				pthread_create(&p1, NULL, MT_RR_scheduler, NULL);
				pthread_create(&p2, NULL, MT_RR_scheduler, NULL);
				pthread_join(p1, NULL);
				pthread_join(p2, NULL);
				if (quit_fullqueue) { // if quit was called during the execution of other threads 
					quit_fullqueue=0;
					exit(0);
				}
				Ready_queue.tail = NULL;
				Ready_queue.head = NULL;
			}
			else RR_scheduler();
			break;
		case 3 : // AGING
			qsort(PCB_arr, number_of_PCBs, sizeof(PCB_arr[0]), compare_PCBs);
			for (i=0; i<number_of_PCBs; i++){
				add_to_ready_queue(PCB_arr[i]);
			}
			if (background_exec) add_to_front_ready_queue(background_PCB);
			AGING_scheduler();
			break;
		case 4 : //RR 30
			for (i=0; i<number_of_PCBs; i++){
				add_to_ready_queue(PCB_arr[i]);
			}
			if (background_exec) add_to_front_ready_queue(background_PCB); // If it is an # execution
			if (MT) { // If it is an MT execution
				
				if (MT_in_process==1) { // If there is an MT execution in process
					// Post the semaphore for the number of new processes
					for(int i =0; i<number_of_PCBs; i++){
						sem_post(&fullqueue);
					}
					return 0;
				}
				sem_init(&fullqueue,0,Ready_queue.num_PCBs);
				MT_in_process =1;
				pthread_create(&p1, NULL, MT_RR30_scheduler, NULL);
				pthread_create(&p2, NULL, MT_RR30_scheduler, NULL);
				pthread_join(p1, NULL);
				pthread_join(p2, NULL);
				if (quit_fullqueue) { // if quit was called during the execution of other threads 
					quit_fullqueue=0;
					exit(0);
				}
				Ready_queue.tail = NULL;
				Ready_queue.head = NULL;
			}
			else RR30_scheduler();
			break;
	}

	return 0;
}
void copyf_to_backingstore(char * fname){
	char dest_file_name[100]; //array to hold the path of the file in the backing store directory
	sprintf(dest_file_name, "backing_store/%s", fname);
	FILE *source_file = fopen(fname, "rt");
	FILE *dest_file = fopen(dest_file_name, "wt");
	
	char line[1000];
	char temp_line[1000];
	int i=0;
	int start;
	int one_liner;
	memset(temp_line, 0, sizeof(temp_line));
	memset(line, 0, sizeof(line));
	// Copying the file from PWD to backingstore line by line
	while(fgets(line,999,source_file) != NULL){
		one_liner=0; // Flag that indicates whether a line read is a oneliner
		start =0;
		i=0;
		while (i<999){
			if (line[i]==';'){
				
				one_liner=1;
				memcpy(temp_line, &line[start], i-start); // Copy the intruction right up to the ';'
				
				fputs(temp_line, dest_file); // write it to the file in the backing store
				fputs("\n", dest_file); // Add a new line character

				memset(temp_line, 0, sizeof(temp_line)); // Reset the buffer
				i++; // Increment i
				while(line[i]==' ' && i<999) i++; // remove all blank space
				start=i; // update the varible representing the starting index for the next instruction
			} else i++;
		}
		if (one_liner){ // If oneliner then copy the rest of the line i.e. everything past the last ';'
			memcpy(temp_line, &line[start], i-start+1);
			fputs(temp_line, dest_file);
			memset(temp_line, 0, sizeof(temp_line));
		}
		else fputs(line, dest_file); // If it is not a one liner then write the entire buffer
		memset(line, 0, sizeof(line));
	}
	fclose(source_file);
	fclose(dest_file);
}
// Loads one pages into memory
void load1_to_framestore(struct PCB * pcb){
	char *fname = pcb->PID;
	char fpath[100]; //contains the path of the file in the backing store
	char line[1000]; 
	char buffer[100];
	memset(line, 0, sizeof(line));
	memset(buffer, 0, sizeof(buffer));

	sprintf(fpath, "backing_store/%s", fname); //formating the path of the file to read from
	FILE *f =fopen(fpath, "r");

	int pageno = pcb->PC/3; // Indicates the next page that must be read
	int i = pcb->PC%3; // Counter for the instruction within each page
	int empty_frame_index = find_empty_frame(); //Finds empty space in memory for a frame
	int num_lines_read = 0; // The number of lines read from the file
	int num_lines_saved =0; // The number of instructions saved to memory

	while(fgets(line,999,f) != NULL){
		if (num_lines_read>= pcb->PC){
			pcb->page_table[pageno] = empty_frame_index; // Updating the page table of the PCB
			sprintf(buffer, "%s-%d", fname, pageno); //format of the variable name for the frame in shell memory
			// Saving the frame in shell memory
			shellmemory[empty_frame_index+i].var = strdup(buffer);
			shellmemory[empty_frame_index+i].value = strdup(line);
			// If an entire frame was copied to shellmemory
			i++;
			num_lines_saved++;
			if (num_lines_saved == 3) break;
			/*
			if (i==3){
				i=0; //reset counter
				empty_frame_index = find_empty_frame(); //get new empty index in memory for the next frame
				pageno++; // increment the variable representing the current page number
			}*/
		}
		num_lines_read++;
		memset(line, 0, sizeof(line));
		memset(buffer, 0, sizeof(buffer));
	}
	increment_LRU_counter(empty_frame_index);
	pcb->instructions_loaded = pcb->instructions_loaded + num_lines_saved;
	fclose(f);
}
// Loads two pages into memory
void load2_to_framestore(struct PCB * pcb){
	char *fname = pcb->PID;
	char fpath[100]; //contains the path of the file in the backing store
	char line[1000]; 
	char buffer[100];
	memset(line, 0, sizeof(line));
	memset(buffer, 0, sizeof(buffer));
	
	sprintf(fpath, "backing_store/%s", fname); //formating the path of the file to read from
	FILE *f =fopen(fpath, "r");

	int pageno = 0; // Indicates the next page that must be read
	int i = 0; // Counter for the instruction within each page
	int empty_frame_index = find_empty_frame(); //Finds empty space in memory for a frame
	increment_LRU_counter(empty_frame_index);
	int num_lines_read = 0; // The number of lines read from the file
	int num_lines_saved =0; // The number of instructions saved to memory
	while(fgets(line,999,f) != NULL){
		if (num_lines_read>= pcb->PC){

			pcb->page_table[pageno] = empty_frame_index; // Updating the page table of the PCB
			sprintf(buffer, "%s-%d", fname, pageno); //format of the variable name for the frame in shell memory
			// Saving the frame in shell memory
			//printf("----%s\n", line);

			shellmemory[empty_frame_index+i].var = strdup(buffer);
			shellmemory[empty_frame_index+i].value = strdup(line);

			// If an entire frame was copied to shellmemory
			i++;
			num_lines_saved++;
			if (num_lines_saved == 6) break;
			if (i==3){
				i=0; //reset counter
				empty_frame_index = find_empty_frame(); //get new empty index in memory for the next frame
				increment_LRU_counter(empty_frame_index);
				pageno++; // increment the variable representing the current page number
			}
		}
		num_lines_read++;
		memset(line, 0, sizeof(line));
		memset(buffer, 0, sizeof(buffer));
	}

	for (int i =0; i<num_lines_saved;i++){
		//printf("----%s\n",shellmemory[pcb->page_table[i/3]+i%3].value);
	}

	pcb->instructions_loaded = pcb->instructions_loaded + num_lines_saved;
	fclose(f);
}
// Loads an entire program represented using its PCB into memory
void load_to_framestore(struct PCB * pcb){
	char *fname = pcb->PID;
	char fpath[100]; //contains the path of the file in the backing store
	char line[1000]; 
	char buffer[100];
	
	sprintf(fpath, "backing_store/%s", fname); //formating the path of the file to read from
	FILE *f =fopen(fpath, "r");

	int pageno =0;
	int i = 0;
	int empty_frame_index = find_empty_frame(); //Finds empty space in memory for a frame
	int num_lines = 0;

	while(fgets(line,999,f) != NULL){
		
		pcb->page_table[pageno] = empty_frame_index; // Updating the page table of the PCB
		sprintf(buffer, "%s-%d", fname, pageno); //format of the variable name for the frame in shell memory
		// Saving the frame in shell memory
		//shellmemory[empty_frame_index+i].var = strdup(buffer);
		//shellmemory[empty_frame_index+i].value = strdup(line);
		// If an entire frame was copied to shellmemory
		i++;
		if (i==3){
			i=0; //reset counter
			empty_frame_index = find_empty_frame(); //get new empty index in memory for the next frame
			pageno++; // increment the variable representing the current page number
		}
	}
	fclose(f);
}

void add_to_front_ready_queue(struct PCB * pcb){
	pcb->next = Ready_queue.head;
	Ready_queue.head = pcb;
	Ready_queue.num_PCBs = Ready_queue.num_PCBs+1;
}
// Creates PCB for the background process
struct PCB * create_background_PCB(char * instructions[], int num_instructions){
	FILE * dest_file = fopen("backing_store/background", "wt");
	struct PCB *background_PCB = (struct PCB *) malloc(sizeof(struct PCB)); // Initializing struct
	background_PCB->PID = strdup("background"); //Assigning a PID
	int one_liner;
	int j;
	int start;
	char temp_line[1000];
	// Holds the index of the read instruction in shellmemory
	for(int i=0; i< num_instructions;i++){
		one_liner=0; // Flag that indicates whether a line read is a oneliner
		start =0;
		j=0;
		while (j<strlen(instructions[i])){
			if(instructions[i][j]==';'){
				one_liner=1;
				memcpy(temp_line, &instructions[i][start], i-start); // Copy the intruction right up to the ';'
				
				fputs(temp_line, dest_file); // write it to the file in the backing store
				//fputs("\n", dest_file); // Add a new line character

				memset(temp_line, 0, sizeof(temp_line)); // Reset the buffer
				j++; // Increment i
				while(instructions[i][j]==' ' && j<strlen(instructions[i])) j++; // remove all blank space
				start=j; // update the varible representing the starting index for the next instruction
			} else j++;
		}
		if (one_liner){ // If oneliner then copy the rest of the line i.e. everything past the last ';'
			memcpy(temp_line, &instructions[i][start], i-start+1);
			fputs(temp_line, dest_file);
			memset(temp_line, 0, sizeof(temp_line));
		}
		else fputs(instructions[i], dest_file); // If it is not a one liner then write the entire buffer
	}
	fclose(dest_file);
	background_PCB->num_of_instructions = get_num_instructions("backing_store/background"); //setting the number of instructions
	background_PCB->next = NULL;
	background_PCB->PC = 0;
	background_PCB->executing =0;
	background_PCB->instructions_loaded =0;
	return background_PCB;
}

// Gets the number of lines from a file
int get_num_instructions(char * filename){
	FILE *fp = fopen(filename,"r");
	int c;
	int b='\n';
	int count =0;

	while((c = fgetc(fp)) != EOF){
		b=c;
		if (c =='\n' || c == ';') count +=1;
	}
	if (b != '\n') count++;
	
	fclose(fp);
	return count;
}

// This function returns a pointer to a PCB given the name of the script to be executed and loads its instructions in shell memory
struct PCB * create_PCB(char * script){
	char line[1000];
	FILE *p = fopen(script,"r");  // the program is in a file
	if (p == NULL){
		 badcommandFileDoesNotExist();
		 return NULL;
	}
	fclose(p);
	char script_line[100]; //String to contain "Script name"_"line number" as the var name in memory
	int line_number=0; //Holds the number of lines read
	int total_num_instrctions = get_num_instructions(script); //The total number of lines in the script

	struct PCB *script_PCB = (struct PCB *) malloc(sizeof(struct PCB)); // Initializing struct

	memset(script_PCB->page_table, -1, sizeof(script_PCB->page_table)); // initialize the page table to hold -1 values
	script_PCB->PID = strdup(script); //Assigning a PID
	script_PCB->PC = 0; //Setting the PC
	script_PCB->next = NULL;
	script_PCB->executing =0;
	script_PCB->num_of_instructions = total_num_instrctions;
	script_PCB-> instructions_loaded =0;
	return script_PCB;
}

int my_cd(char *name){
	int i = 0;
	while (pwd->directories[i] != NULL){ // iterating through the directories in the pwd
		if (strcmp(pwd->directories[i]->directory, name) ==0){
			struct directory_struct *parent; // Parent directory pointer
			parent = pwd; // Make parent point to pwd
			pwd = pwd->directories[i]; // Change the pwd
			pwd->parent = parent; //Assign the parent
			return 0;
		}
		i++;
	}
	printf("Bad command: my_cd\n");
	return 0;
}

int my_touch(char *name){
	int i = 0;
	// Checks for next empty slot in files array
	while (i<100 && pwd->files[i] !=NULL ){
		i++;
	}

	if (name[0] =='$'){ //Checks if the argument to my_touch is a variable in shell memory
		int j;
		int name_len = strlen(name);
		char temp[name_len]; // Array to hold the variable name after removing the '$'

		// Copying the string less the '$'
		for(j=1; j<name_len; j++){
			temp[j-1] = name[j];
		}
		temp[j-1] = '\0';

		char *value = mem_get_value(temp); //Getting the value associated with the variable

		for(j=0; j<strlen(value); j++){ // Checking if the value is a valid alphanumerical token
			if (value[j] == ' '){
				printf("Bad command: my_touch\n");
				return 0;
			}
		}
		pwd->files[i] = value; // Inserting a pointer to the value in the files array of the directory_struct instance
	} else{
		pwd->files[i] = name; //Inserting a pointer to the value in the files array of the directory_struct instance
	}
	return 0;	
}	

int my_mkdir(char *name){
	struct directory_struct *new_dir= malloc(sizeof(struct directory_struct)); // Creating a pointer to a directory_struct instance

	int i=0;
	while(i<100 && (*pwd).directories[i] != NULL){	// Checks for next empty slot in directories array	
		i++;
	}
	// Copying the string less the '$'
	if (name[0] == '$'){
		int j;
		int name_len = strlen(name);
		char temp[name_len];
		for (j=1; j<name_len; j++){
			temp[j-1]= name[j];
		}
		temp[j-1]='\0';

		char *value =mem_get_value(temp); //Getting the value associated with the variable

		for(j=0; j<strlen(value); j++){// Checking if the value is a valid alphanumerical token
			if (value[j] == ' '){
				printf("Bad command: my_mkdir\n");
				return 0;
			}
		}

		new_dir->directory = value;// Assigneing the directory_struct instance new_dir the name
	} else {
		new_dir->directory = name; // Assigneing the directory_struct instance new_dir the name
	}
	(*pwd).directories[i] = new_dir; // Adding the directory_struct instance created to the list of directories within the pwd
	
	return 0;
}

int compare_strings(const void *s1, const void *s2){
	// Constants representing the ascii code of the variable letters they are assigned to
	int A = 65;
	int Z = 90;
	int a = 97;
	int z = 122;
	int utol_offset = 32; // The offset between the ascii code of a capital and small letter
	
	//casting the argument void pointers to char pointers
	const char *str1 = *(const char**)s1;
	const char *str2= *(const char**)s2;

	//Getting the length of the strings passed
	int s1_len = strlen(str1);
	int s2_len = strlen(str2);

	//Getting the minumuim length of the two strings
	int min_len;
	if (s1_len <= s2_len){
		min_len = s1_len;
	} else {
		min_len = s2_len;
	}

	int i = 0;
	int result=0; // Comparison result
	while(i <min_len && result ==0){
		if (A<=(int)str1[i] && (int)str1[i]<=Z && A<=(int)str2[i] && (int)str2[i]<=Z){ // If both corresponding letters in both strings are capital
			result = (int)str1[i] -(int)str2[i]; //Comparison by ascii code

		} else if (a<=(int)str1[i] && (int)str1[i]<=z && a<=(int)str2[i] && (int)str2[i]<=z){ // If both corresponding letters in both strings are small
			result = (int)str1[i] -(int)str2[i];

		} else if (A<=(int)str1[i] && (int)str1[i]<=Z && a<=(int)str2[i] && (int)str2[i]<=z){ // If the letter in the first string is capital and the other is small
			result = ((int)str1[i] + utol_offset) -(int)str2[i]; // Making the capital letter small by adding the value of the offset
			if (result ==0){ // If equal break tie in favor of the capital letter
				return -1;
			}
		} else if (a<=(int)str1[i] && (int)str1[i]<=z && A<=(int)str2[i] && (int)str2[i]<=Z){ // If the letter in the first string is small and the other is capital
			result = (int)str1[i] - ((int)str2[i] + utol_offset);
			if (result ==0){
				return 1;
			}
		} else {
			result = (int)str1[i] -(int)str2[i]; // If one of the characters in either or both strings is a number
		}
		i++;
	}
	
	return result;
}

int my_ls(){
	char *tmp[100]; //Temporary array of char pointers
	int i=0;
	while((*pwd).files[i] != NULL){// itterating over the strings in the files field of the struct directory_struct pwd
		tmp[i] =(*pwd).files[i]; //Adding the strings to the tmp array
		i++;
	}

	int j=0;
	while((*pwd).directories[j] != NULL){
		// Adding the strings representing the directories in the directories field of *pwd to tmp[]
		tmp[i] = (*pwd).directories[j]->directory;
		i++;
		j++;
	}
	// Sort tmp[] which now contains the names of the files and directories in the pwd
	qsort(tmp, i, sizeof(char*), compare_strings);
	// Print the sorted tmp array
	for(j=0; j<i; j++){
		printf("%s\n", tmp[j]);
	}
	return 0;
}

int help(){

	char help_string[] = "COMMAND			DESCRIPTION\n \
help			Displays all the commands\n \
quit			Exits / terminates the shell with “Bye!”\n \
set VAR STRING		Assigns a value to shell memory\n \
print VAR		Displays the STRING assigned to VAR\n \
run SCRIPT.TXT		Executes the file SCRIPT.TXT\n ";
	printf("%s\n", help_string);
	return 0;
}

int quit(){
	// If the ready queue is not empty then make quit_fullqueue flag true and execute quit later
	if (Ready_queue.num_PCBs != 0 & MT_in_process) { 
		quit_fullqueue =1;
		printf("%s\n", "Bye!");
		return 0;
	}
	printf("%s\n", "Bye!");
	nftw("backing_store", rmr, 64, FTW_DEPTH);
	exit(0);
}

int set(char* var, char* value){
	char *link = "=";
	char buffer[1000];
	strcpy(buffer, var);
	strcat(buffer, link);
	strcat(buffer, value);
	mem_set_value(var, value);
	return 0;

}

int print(char* var){
	char *value = mem_get_value(var);
	if(value == NULL) value = "\n";
	printf("%s\n", value); 
	return 0;
}

int run(char* script){
	int errCode = 0;
	char line[1000];
	struct PCB * script_PCB = create_PCB(script);
	if (script_PCB == NULL) return 1; 

	copyf_to_backingstore(script); // Copies the script from the current directory to the backingstore
	load2_to_framestore(script_PCB); //Loads the PCB from the backingstore to the framestore

	//Adding the PCB to the ready queue
	if (Ready_queue.head ==NULL){
		Ready_queue.head = script_PCB;
		Ready_queue.tail = script_PCB;
	}
	else{
		Ready_queue.tail->next = script_PCB;
		Ready_queue.tail = script_PCB;
	}
	RR_scheduler();
	return errCode;
}
// A methods that compaes PCBs based on the number of instructions
int compare_PCBs(const void * PCB_1, const void * PCB_2){
	const struct PCB * pcb1 = *((const struct PCB **) PCB_1);
	const struct PCB * pcb2 = *((const struct PCB **) PCB_2);
	int result = ((pcb1->num_of_instructions) - (pcb2->num_of_instructions));
	return result;
}

void frame_cleanup(struct PCB * pcb){
	int instruction_address;
	for (int i =0; i<pcb->num_of_instructions; i++){
		instruction_address = pcb->page_table[i/3]+i%3;
		shellmemory[instruction_address].var = "none";
		shellmemory[instruction_address].value = "none";
	}
}

void FCFS_scheduler(){
	int instruction_address;
	while(Ready_queue.head != NULL){
		int PC = Ready_queue.head->PC;
		int num_instructions = Ready_queue.head->num_of_instructions;
		
		for (PC; PC < num_instructions; PC++){ //Executes the instructions
			Ready_queue.head->PC = Ready_queue.head->PC+1;
			instruction_address = Ready_queue.head->page_table[PC/3]+PC%3; //Gets the memory address for the next intruction
			parseInput(shellmemory[instruction_address].value);	
		}		
		frame_cleanup(Ready_queue.head); //memory cleanup
		Ready_queue.head = Ready_queue.head->next; //Moves the queue
	}
	if (quit_fullqueue) { // if quit was called during the execution of other threads 
		quit_fullqueue=0;
		exit(0);
	}
	Ready_queue.num_PCBs =0;
	Ready_queue.tail = NULL; // Set the tail to null when the queue is empty
}

void * MT_RR_scheduler(){
	int i;
	struct PCB * current_PCB;
	int instruction_address;
	while(1){
		sem_wait(&fullqueue);
		pthread_mutex_lock(&lock);
		if(Ready_queue.head == NULL) {
			pthread_mutex_unlock(&lock);
			return NULL;
		}
		if (Ready_queue.head->executing == 0){
			Ready_queue.head->executing = 1;
			current_PCB = Ready_queue.head;
			Ready_queue.head = Ready_queue.head->next; // Updating the head of the queue
			pthread_mutex_unlock(&lock);
		} 
		
		else return NULL;

		int PC = current_PCB->PC;
		int num_instructions = current_PCB->num_of_instructions;
		
		for (i=PC; i <num_instructions && i< PC+2; i++){ //Executes the instructions
			//pthread_mutex_lock(&lock);
			current_PCB->PC = current_PCB->PC + 1;// Updating PC of the current process
			instruction_address = current_PCB->page_table[i/3]+i%3; //Gets the memory address for the next intruction
			parseInput(shellmemory[instruction_address].value);	
			//pthread_mutex_unlock(&lock);
			
		}		
		if (current_PCB->PC >= num_instructions){ // If the process is finished
			pthread_mutex_lock(&lock);
			Ready_queue.num_PCBs =Ready_queue.num_PCBs-1;
			frame_cleanup(current_PCB); //memory cleanup if process is done
			if (Ready_queue.num_PCBs == 0){
				pthread_mutex_unlock(&lock);
				sem_post(&fullqueue);
				return NULL;
			}else pthread_mutex_unlock(&lock);
			// If there is 1 more process then one of the must threads return and that thread will return when num_PCBs is 0 i.e. when done
			//if (Ready_queue.num_PCBs <=1) return NULL;
		}
		else{
			pthread_mutex_lock(&lock);
			current_PCB->executing = 0;
			Ready_queue.tail->next = current_PCB;
			Ready_queue.tail = Ready_queue.tail->next;
			current_PCB->next = NULL;
			if (Ready_queue.head == NULL) Ready_queue.head = current_PCB;
			sem_post(&fullqueue);
			pthread_mutex_unlock(&lock);
		}
	}
	return NULL;
}
void * MT_RR30_scheduler(){
	int i;
	struct PCB * current_PCB;
	int instruction_address;
	while(1){
		sem_wait(&fullqueue);
		pthread_mutex_lock(&lock);
		if(Ready_queue.head == NULL) {
			pthread_mutex_unlock(&lock);
			return NULL;
		}
		if (Ready_queue.head->executing == 0){
			Ready_queue.head->executing = 1;
			current_PCB = Ready_queue.head;
			Ready_queue.head = Ready_queue.head->next; // Updating the head of the queue
			pthread_mutex_unlock(&lock);
		} 
		
		else return NULL;

		int PC = current_PCB->PC;
		int num_instructions = current_PCB->num_of_instructions;
		
		for (i=PC; i <num_instructions && i< PC+30; i++){ //Executes the instructions
			//pthread_mutex_lock(&lock);
			current_PCB->PC = current_PCB->PC + 1;// Updating PC of the current process
			instruction_address = current_PCB->page_table[i/3]+i%3; //Gets the memory address for the next intruction
			parseInput(shellmemory[instruction_address].value);	
			//pthread_mutex_unlock(&lock);
			
		}
		if (current_PCB->PC >=  num_instructions){ // If the process is finished
			pthread_mutex_lock(&lock);
			Ready_queue.num_PCBs =Ready_queue.num_PCBs-1;
			frame_cleanup(current_PCB); //memory cleanup if process is done
			if (Ready_queue.num_PCBs == 0){
				pthread_mutex_unlock(&lock);
				sem_post(&fullqueue);
				return NULL;
			}else pthread_mutex_unlock(&lock);
			// If there is 1 more process then one of the must threads return and that thread will return when num_PCBs is 0 i.e. when done
			//if (Ready_queue.num_PCBs <=1) return NULL;
		}
		else{
			pthread_mutex_lock(&lock);
			current_PCB->executing = 0;
			Ready_queue.tail->next = current_PCB;
			Ready_queue.tail = Ready_queue.tail->next;
			current_PCB->next = NULL;
			if (Ready_queue.head == NULL) Ready_queue.head = current_PCB;
			sem_post(&fullqueue);
			pthread_mutex_unlock(&lock);
		}
	}
	//Ready_queue.tail = NULL; // Set the tail to null when the queue is empty
	return NULL;
}

void RR_scheduler(){
	int i;
	int instruction_address;
	while(Ready_queue.head != NULL){
		int PC = Ready_queue.head->PC;
		int num_instructions = Ready_queue.head->num_of_instructions;
	

		for (i=PC; i <num_instructions && i< PC+2; i++){ //Executes the instructions
			// If a page fault is detected
			//printf("-----%d\n",Ready_queue.head->page_table[i/3]);
			if (Ready_queue.head->page_table[i/3]==-1){

				load1_to_framestore(Ready_queue.head); // Load a new page to the frame store
				break;
			}
			Ready_queue.head->PC = Ready_queue.head->PC +1;// Updating PC of the current process
			instruction_address = Ready_queue.head->page_table[i/3]+i%3; //Gets the memory address for the next intruction
			parseInput(shellmemory[instruction_address].value);
			increment_LRU_counter(instruction_address); // Increment the LRU counter
		}		
		// If the process is still unfinished done
		if (i < num_instructions){
			// Moving the current PCB to the end of the queue and updating the tail
			Ready_queue.tail->next = Ready_queue.head;
			Ready_queue.tail = Ready_queue.tail->next;

			struct PCB * next = Ready_queue.head->next; //Creating a pointer to hold the address to the next process
			Ready_queue.head->next = Ready_queue.head; // Making the current process point to itself
			Ready_queue.head = next; // Updating the head of the queue
		} 
		else{
			//Ready_queue.head->PC = i; // Updating PC of the current process
			//frame_cleanup(Ready_queue.head); // Cleaning up the frame from memory
			struct PCB * next = Ready_queue.head->next;
			Ready_queue.head->next = NULL;
			Ready_queue.head = next; //Moves the queue	
		}
	}
	Ready_queue.num_PCBs = 0;
	Ready_queue.tail = NULL; // Set the tail to null when the queue is empty
}
void RR30_scheduler(){
	int i;
	int instruction_address;
	while(Ready_queue.head != NULL){
		int PC = Ready_queue.head->PC;
		int num_instructions = Ready_queue.head->num_of_instructions;
		
		for (i=PC; i <num_instructions && i< PC+30; i++){ //Executes the instructions
			Ready_queue.head->PC = Ready_queue.head->PC +1;// Updating PC of the current process
			instruction_address = Ready_queue.head->page_table[i/3]+i%3; //Gets the memory address for the next intruction
			parseInput(shellmemory[instruction_address].value);
		}		
		// If the process is still unfinished done
		if (i < num_instructions){
			// Moving the current PCB to the end of the queue and updating the tail
			Ready_queue.tail->next = Ready_queue.head;
			Ready_queue.tail = Ready_queue.tail->next;

			struct PCB * next = Ready_queue.head->next; //Creating a pointer to hold the address to the next process
			Ready_queue.head->next = Ready_queue.head; // Making the current process point to itself
			Ready_queue.head = next; // Updating the head of the queue
		} 
		else{
			frame_cleanup(Ready_queue.head); // Cleaning up the frame from memory
			struct PCB * next = Ready_queue.head->next;
			Ready_queue.head->next = NULL;
			Ready_queue.head = next; //Moves the queue	
		}
	}
	Ready_queue.num_PCBs = 0;
	Ready_queue.tail = NULL; // Set the tail to null when the queue is empty
}

 // Uses insertion sort to sort and array of PCBs based on their job score
void insertion_Sort_PCB_array(struct PCB * arr[], int n)
{
    int i, j, key;
    for (i = 1; i < n; i++)
    {
        key = arr[i]->job_score;
		struct PCB * pcb = arr[i];

        j = i - 1;
        while (j >= 0 && arr[j]->job_score > key)
        {
            arr[j + 1] = arr[j];
            j = j - 1;
        }
        arr[j + 1] = pcb;
    }
}
void AGING_scheduler(){
	// Copy the Ready_queue into an array
	struct PCB * temp = Ready_queue.head;
	struct PCB * PCB_arr[Ready_queue.num_PCBs];
	int num_PCBs = Ready_queue.num_PCBs;
	int instruction_address;
	int i = 0;
	while (temp != NULL){
		PCB_arr[i] = temp;
		i++;
		temp = temp->next;
	}
	struct PCB * head = PCB_arr[0];
	while(num_PCBs > 0){
		int PC = head->PC;
		int num_instructions = head->num_of_instructions;
		if (PC < num_instructions){
			instruction_address = head->page_table[PC/3]+PC%3; //Gets the memory address for the next intruction
			parseInput(shellmemory[instruction_address].value);	
			PC++;
			head->PC = PC;
		}
		if (PC >= num_instructions){
			frame_cleanup(head);
			for (i = 1; i<num_PCBs; i++){
				PCB_arr[i-1] = PCB_arr[i];
			}
			num_PCBs = num_PCBs - 1;

			for (i=0; i< num_PCBs; i++){
				PCB_arr[i]->job_score = PCB_arr[i]->job_score-1;
			}
			insertion_Sort_PCB_array(PCB_arr, num_PCBs);
			head = PCB_arr[0];
		}
		else{
			for (i=1; i< num_PCBs; i++){
				if (PCB_arr[i]->job_score > 0) PCB_arr[i]->job_score = PCB_arr[i]->job_score-1;
			}
			insertion_Sort_PCB_array(PCB_arr, num_PCBs);
			if (head != PCB_arr[0]){
				int head_index;
				for (i=0; i< num_PCBs; i++){
					if (PCB_arr[i] == head) head_index =i;
				}
				for (i=head_index; i< num_PCBs-1; i++){
					PCB_arr[i] = PCB_arr[i+1];
				} PCB_arr[num_PCBs-1] = head;
			}
			head = PCB_arr[0];
		}
	}
	Ready_queue.head = NULL;
	Ready_queue.tail = NULL;
	Ready_queue.num_PCBs =0;
}

int echo(char *token){
	if (token[0] == '$'){ //If token is preceeded by $
		char var_name[strlen(token)]; // Array to hold the token without the '$'
		int i;
		for (i = 1; i< strlen(token); i++){ // Extracting the string into var_name
			var_name[i-1] = token[i];
		}
		var_name[i-1] = '\0';
		char *value = mem_get_value(var_name);

		if (strcmp(value, "Variable does not exist") == 0){ //If there are no matches in shell memmory
			printf("\n");
		} else {
			printf("%s\n", value);
		}

	} else {
		printf("%s\n", token);
	}
	return 0;
}

