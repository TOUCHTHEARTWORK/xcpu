#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>

#include "bjs.h"

#define _GNU_SOURCE

extern int spc_chatty;
static Xpnodeset *nds;
static struct group *gr;
static struct passwd *pw;
struct Spuser *user;
struct Xkey *ukey;

/* TODO: adminkey can be specified through an option in bjs.conf */
char *adminkey = NULL;
int ecode;
char *ename;

static inline
int xp_to_xp_nodeset(struct Xpnode *xpnode) {	
    nds = xp_nodeset_create();
    if (!nds)
	return -1;
    
    return xp_nodeset_add(nds, xpnode);
}

static inline
int bjs_to_xp_nodeset(struct bjs_node_t *node) {
    return xp_to_xp_nodeset(node->node);
}

static int
get_user_key(char *keypath, char *key, int keysize) {
    int fd, n;
    char *s;
    
    fd = open(keypath, O_RDONLY);
    if (fd < 0)
        return -1;
    
    n = read(fd, key, keysize-1);
    if (n < 0)
        return -1;
    
    s = strchr(key, '\n');
    if (s)
        *s = '\0';
    else
        *(key+n) = '\0';
    close(fd);
    return 0;
}

    
int xcpu_add_group(struct bjs_node_t *node, int groupid) {
    gr = getgrgid(groupid);
    if (!gr) {
	sp_uerror(errno);
	goto error;
    }

    if (bjs_to_xp_nodeset(node) < 0)
	goto error;

    if (xp_group_add(nds, adminkey, gr->gr_name, groupid) < 0)
	goto error;
    
    free(nds);
    return 0;
error:
    free(nds);
    sp_rerror(&ename, &ecode);
    syslog(LOG_ERR, "xcpu_add_group: %s\n", ename);
    return -1;
}

int xcpu_del_group(struct bjs_node_t *node, int groupid) {
    gr = getgrgid(groupid);
    if (!gr) {
	sp_uerror(errno);
	goto error;
    }

    if (bjs_to_xp_nodeset(node) < 0)
	goto error;

    if (xp_group_del(nds, adminkey, gr->gr_name) < 0)
        goto error;

    free(nds);
    return 0;
error:    
    free(nds);
    sp_rerror(&ename, &ecode);
    syslog(LOG_ERR, "xcpu_del_group: %s\n", ename);
    return -1;
}

int xcpu_add_user(struct bjs_node_t *node, int userid, int groupid) {
    char ukeypath[256], userkey[4096];
    
    pw = getpwuid(userid);
    if (!pw) {
	sp_uerror(errno);
	goto error;
    }

    if (pw->pw_gid != groupid) {        
        sp_werror("User %s does not belong to group ID: %d",
                  EIO, pw->pw_name, groupid);
        goto error;
    }
    
    gr = getgrgid(groupid);
    if (!gr) {
	sp_uerror(errno);
	goto error;
    }

    snprintf(ukeypath, sizeof(ukeypath), "%s/.ssh/id_rsa.pub", pw->pw_dir);
    if (get_user_key(ukeypath, userkey, sizeof(userkey)) < 0) {
        sp_suerror("get_user_key:", errno);
        goto error;
    }    

    if (bjs_to_xp_nodeset(node) < 0)
	goto error;
	    
    if (xp_user_add(nds, adminkey, pw->pw_name, userid,
                    gr->gr_name, userkey) < 0)
        goto error;

    free(nds);
    return 0;
error:
    free(nds);
    sp_rerror(&ename, &ecode);
    syslog(LOG_ERR, "xcpu_add_user: %s\n", ename);
    return -1;
}

int xcpu_del_user(struct bjs_node_t *node, int userid) {

    pw = getpwuid(userid);
    if (!pw) {
	sp_uerror(errno);
	goto error;
    }

    if (bjs_to_xp_nodeset(node) < 0)
	goto error;
	    
    if (xp_user_del(nds, adminkey, pw->pw_name) < 0) {        
        xp_nodeset_destroy(nds);
        goto error;
    }

    free(nds);
    return 0;
error:
    free(nds);
    sp_rerror(&ename, &ecode);
    syslog(LOG_ERR, "xcpu_del_user: %s\n", ename);
    return -1;
}

int xcpu_node_flush(struct bjs_node_t *node) {
    if (!node)
        return -1;
    
    if (bjs_to_xp_nodeset(node) < 0)
        goto error;
	
    if (xp_user_flush(nds, adminkey) < 0)
        goto error;

    if (xp_group_flush(nds, adminkey) < 0)
        goto error;
    
    free(nds);
    return 0;
error:
    free(nds);
    sp_rerror(&ename, &ecode);
    fprintf(stderr, "Error: %s\n", ename);
    return -1;

}

int xcpu_proc_list(struct Xpnode *node, struct Xpproc **procs) {
    int n;
    if (xp_to_xp_nodeset(node) < 0)
	return -1;

    xp_defaultuser(&user, &ukey);
    n = xp_proc_list(nds, user, ukey, procs);
    free(nds);
    return n;
}

int xcpu_proc_kill(struct Xpproc *proc, int signal) {	
    if (!user || !ukey)
	    xp_defaultuser(&user, &ukey);
    
    return xp_proc_kill(proc, user, ukey, signal);
}
