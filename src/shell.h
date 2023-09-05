#define __USE_XOPEN_EXTENDED
#include <ftw.h>

int parseInput(char ui[]);
int rmr(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf);
extern int frame_size;
extern int var_mem_size;