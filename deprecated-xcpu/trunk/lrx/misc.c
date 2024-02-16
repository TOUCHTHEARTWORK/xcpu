#include "include/lrx.h"

int check_for_xcpu(){
    return 1;
}

/* function lrx() is only meant to be called from ompi library
 * Only one thread at a time will be spawned from function lrx()
 * function lrx() will return thread_id of that thread and then
 * ompi will wait for that thread to complete
 * */

int lrx(int argc, char **argv, char **env){/* only for library purpose*/
    int rc;
    pthread_tindex *t_info;
    if((rc=cmd_check(argc, argv))==1){
        return 0;
    }
    if((t_info=launch_procs(argc, argv, env))==NULL){
        fprintf(stderr, "lrx: 0 processes launched\n");
        return 0;
    }
    else{
        cleanup();
        t_info->index--;
        rc=t_info->tids[t_info->index];
        free(t_info->tids);
        return rc;
    }
        /*
        while(1){
            t_info->index--;
            pthread_join(t_info->tids[t_info->index], NULL);
            if(t_info->index==0)
                break;
        }
        */
    return 0;/* can never be called*/
}

/* this function is here so that, if a job fails to start
 * all the other jobs started can be kkilled
 * */

int kill_started_jobs(){
    return 1;
}

int check_exp(char *exp){
    if(regcomp(&g_compiled_exp, exp, REG_EXTENDED|REG_NOSUB)){
        fprintf(stderr, "Invlid regular expression: %s\n", exp);
        return 1;
    }
    /*regfree(&g_compiled_exp);*/
    return 0; /* now dont forget to call regfree at the end*/
}

void debug(int val, char *str){
    (val<=g_dbval)?fprintf(stdout, "%s",str):0;
}
