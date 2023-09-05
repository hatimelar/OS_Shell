void mem_init();
char *mem_get_value(char *var);
int mem_set_value(char *var, char *value);
void mem_set_value_at_index(char *var_in, char *value_in, int index);
int find_contiguous_memory(int entries);
void mem_cleanup(int start, int end);
void intialize_directory();
int find_empty_frame();
void increment_LRU_counter(int address);
struct directory_struct{
	char *directory;
	char *files[100];
	struct directory_struct *directories[100];
	struct directory_struct *parent;
};
extern struct directory_struct *pwd;
struct memory_struct{
	char *var;
	char *value;
};
extern struct memory_struct shellmemory[1000];
extern int variable_store;