int interpreter(char* command_args[], int args_size);
int help();
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

extern struct Ready_queue Ready_queue;
