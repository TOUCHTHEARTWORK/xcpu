/* program for creating an xcpu launcher that will launch the jobs
 * on the nodes specified on commandline using regular expressions
 */
#include "include/lrx.h"
int g_regexploc=1;
mount_nodes *g_current_m=NULL;
thread_info *g_thread_info;
pthread_tindex t_info;

int main(int argc, char **argv){
    int rc;
    pthread_tindex *t_info;
    if((rc=cmd_check(argc, argv))==1){
        return rc;
    }
    if((t_info=launch_procs(argc, argv, NULL))==NULL){
        fprintf(stderr, "0 processes launched\n");
    }
    else{
        debug(1, "thread wait started\n");
        while(1){
            t_info->index--;
            pthread_join(t_info->tids[t_info->index], NULL);
            if(t_info->index==0)
                break;
        }
        free(t_info->tids);
        debug(1, "thread wait finished\n");
    }
    debug(7, "about to cleanup\n");
    cleanup();
    return 0;
}



pthread_tindex *launch_procs(int argc, char **argv, char **env){
    char *xcpu_base, *xcpu_argv;
    struct dirent *d_entry;
    DIR *dirp;
    int rc=0, index=0, argvsize=0, ntids=0;
    pthread_t *tids;
    mount_nodes *m_nodes, *local_mounts;
    g_current_m=NULL;
    m_nodes=NULL;
    (!(xcpu_base=getenv("XCPUBASE")))?xcpu_base="/mnt/xcpu":0;
    debug(1, "xcpu_base selected: ");
    debug(1, xcpu_base );
    debug(1, "\n" );
    if(!(dirp=opendir(xcpu_base))){
        fprintf(stderr, "unable to open directory: %s\n", xcpu_base);
        return NULL;
    }
    while((d_entry=readdir(dirp))!=NULL){
        if((strcmp(d_entry->d_name, ".")==0)||(strcmp(d_entry->d_name, "..")==0))
            ;else
        if(regexec(&g_compiled_exp, d_entry->d_name, 0, NULL, 0)!=REG_NOMATCH){
            ntids++;
            debug(2, "result of matching directory entry: ");
            debug(2, d_entry->d_name);
            debug(2, " --> positive\n");
            m_nodes=(mount_nodes*)malloc(sizeof(mount_nodes));
            m_nodes->next=g_current_m;
            m_nodes->name=(char*)malloc(1+strlen(xcpu_base)+1+
                                        strlen(d_entry->d_name)+1+strlen("xcpu")+1);
            sprintf(m_nodes->name, "%s/%s/xcpu", xcpu_base, d_entry->d_name);
            g_current_m=m_nodes;
        }else{
            debug(3, "result of matching directory entry: ");
            debug(3, d_entry->d_name);
            debug(3, " --> negative\n");
        }
    }
    if(g_current_m==NULL){ /* is that an error.... no?*/
        fprintf(stdout, "\nNo matching nodes found\nexiting.....");
        return NULL; 
    }
    closedir(dirp);
    /* now combine argv's so that they could be passed on */
    /* g_regexploc will have proper value only if 
     * cmd_check is already called
     * and the location of first arg after name of binary will be
     * argv[g_regexploc+2] because usage: ./o.lrx [-D xx] regexp binary args
     */
    /* number of arguments = argc - g_regexploc - 2;*/
    index=g_regexploc+2-1; /*argv[0] i always binary name*/
    while(argv[index]){
        argvsize+=strlen(argv[index])+1;
        index++;
    }
    xcpu_argv=(char*)malloc(argvsize+1);
    index=g_regexploc+2-1;
    while(argv[index]){
        if(index==g_regexploc+2-1)
            strcpy(xcpu_argv, argv[index]);/* i dont know why strcpy 1st time?*/
        else
            strcat(xcpu_argv, argv[index]);
        strcat(xcpu_argv, " ");
        index++;
    }
    xcpu_argv[argvsize]='\0';
    debug(2, "xcpu binary: ");
    debug(2, argv[g_regexploc+1]);
    debug(2, "\n");
    debug(2, "constructed xcpu_arg: ");
    debug(2, xcpu_argv);
    debug(2, "\n");
    local_mounts=g_current_m; /* this is a linked list of mounted directories
                              * where binaries need to run
                              */
    tids=(pthread_t*)malloc(ntids*sizeof(pthread_t));
    index=0;
    while(local_mounts){
        /* dont use a shared copy 
         * give every thread its own copy since we dont know
         * when all threads will exit and when to free a shared copy
         */
        g_thread_info=(thread_info*)malloc(sizeof(thread_info));
        /*copy name first*/
        g_thread_info->local_mounts.name=(char*)malloc(strlen(local_mounts->name)+1);
        strcpy(g_thread_info->local_mounts.name, local_mounts->name);
        /*then copy binary*/
        g_thread_info->binary=(char*)malloc(strlen(argv[g_regexploc+1])+1);
        strcpy(g_thread_info->binary,argv[g_regexploc+1]);
        g_thread_info->argv=(char*)malloc(strlen(xcpu_argv)+1);
        strcpy(g_thread_info->argv, xcpu_argv);
        g_thread_info->env=env; /* we might have to copy env first and
                                 * then pass it
                                 * */
        rc=pthread_create(&tids[index], NULL, start_thread, (void*)g_thread_info);
        index++;
        /* thread_id has to be unique for every thread created if you are going to use
         * pthread_join and wait for these threads to finish
         */
        /*this thread will free the thread_info structure*/
        if(rc){
            fprintf(stderr, "pthread_create: error while creating thread %d\n", rc);
            return NULL;
        }
        local_mounts=local_mounts->next;
    }
    /* use pthrad_join here if you want to wait for threads 
     * to finish execution
     *//*
    while(1){
        index--;
        pthread_join(tids[index], NULL);
        if(index==0)
            break;
    }
    free(tids);*/
    /* remember to free tids in calling function*/
    free(xcpu_argv);
    t_info.tids=tids;
    t_info.index=index;
    return &t_info;
}

int cmd_check(int argc, char **argv){
    char *temp_exp;
    int rc=0;
    g_regexploc=1;
    if(argc>=3){
        if(argv[1][0]=='-'){
            switch(argv[1][1]){
                case 'D': /* for debugging*/
                    g_regexploc+=2;
                    if(argc<5){
                        fprintf(stderr, "usage: o.lrx [-D debuglevel"
                                "] nodes binary [argv0 argv1 ...]\n");
                        rc=1;
                    }
                    g_dbval=atoi(argv[2]);
                    debug(1, "debug level selected: ");
                    debug(1, argv[2]);
                    debug(1, "\n");
                    break;
                default: /* unspecified option*/
                    fprintf(stderr, "usage: o.lrx [-D debuglevel"
                            "] nodes binary [argv0 argv1 ...]\n");
                    return 1;
                    break;
            }
        }
    }else{
        fprintf(stderr, "usage: o.lrx [-D debuglevel"
                "] nodes binary [argv0 argv1 ...]\n");
        rc=1;
    }
    if(!rc){/*check for regular expression*/
        temp_exp=(char*)malloc(strlen(argv[g_regexploc])+3);
        sprintf(temp_exp, "^%s$", argv[g_regexploc]);
        rc=check_exp(temp_exp);
        free(temp_exp);
    }
    return rc;
}

void free_mount(mount_nodes *g_current_m){
    if(g_current_m){
        free_mount(g_current_m->next);
        debug(6, "freeing up directory entry: ");
        debug(6, g_current_m->name);
        debug(6, "\n");
        free(g_current_m->name);
        free(g_current_m);
    }
}

void cleanup(){
    debug(6, "cleaning up........\n");
    regfree(&g_compiled_exp);
    free_mount(g_current_m);
}
