#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <errno.h>
#include <pthread.h>

int 
getgrouplist (const char *name, int baseid, gid_t *groups, int *ngroups) 
{ 
    struct group *g; 
    int n = 0; 
    int i; 
    int ret; 
    int countonly = 0; 
 
    if(!groups) 
        countonly = 1; 
    else  
        *groups++ = baseid; 
    n++; 
 
    setgrent(); 
    while ((g = getgrent()) != NULL) { 
        for (i = 0; g->gr_mem[i]; i++) { 
            if (strcmp(name, g->gr_mem[0]) == 0) { 
                    n++; 
                /* if we're not counting the groups make sure we don't
 * overflow */ 
                /* still, we must return the total number of groups found,
 * so keep counting */ 
                if(!countonly && n <= *ngroups) 
                        *groups++ = g->gr_gid; 
                } 
        } 
    } 
    endgrent (); 
 
    ret = n; 
    if(countonly || n > *ngroups) 
        ret = -1; 
    *ngroups = n; 
 
    return (ret); 
} 
