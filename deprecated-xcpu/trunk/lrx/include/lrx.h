#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <dirent.h> 
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

struct mount_nodes{
    char *name;
    struct mount_nodes *next;
};
typedef struct mount_nodes mount_nodes;

struct thread_info{
    mount_nodes local_mounts;/* can have only *name */
    char *binary;
    char *argv;
    char **env;
};
typedef struct thread_info thread_info;

struct stdio_thread_info{
    char *stdio_path;
    int outdes;
};
typedef struct stdio_thread_info stdio_thread_info;

struct pthread_tindex{
    pthread_t *tids;
    int index;
};
typedef struct pthread_tindex pthread_tindex;

extern int errno;
regex_t g_compiled_exp;
int g_dbval;
char **g_environ;

pthread_tindex *launch_procs(int, char **, char**);
int cmd_check(int, char **);
void cleanup();
void debug(int, char *);
void *start_thread(void *);
void *stdio_thread(void *);
int check_exp(char *);
