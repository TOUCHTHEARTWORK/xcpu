enum {
	/* root level files */
	Qroot = 1,
	Qctl,
	Qprocs,
	Qstats,
	Qnotify,
	Qmax,
	
	Bufsize = 4192,

	Dbgfn	= 1,
	Dbgfs	= 1<<1,
	Dbgcfg	= 1<<2,
	Dbgthr	= 1<<3,
	Dbgloop	= 1<<4,
	Dbgrd	= 1<<5,
	Dbgwr	= 1<<6,
};

typedef struct Node Node;

struct Node
{
	pthread_t	thread;
	pthread_mutex_t mux;
	char 	*name;	/* mounted as */
	char 	*addr;	/* dial string */
	char 	*arch;	/* os/cpu architecture */
	char 	*status;/* as reported by the status file */
	char 	*oldstatus;	/* for callbacks: know what the previous status was */
	char 	*ip;	/* ip address */
	int 	port;	/* used in mounting */

	int		err;	/* indicates an irrecoverable error with a particular node */
	time_t 	ts;
	char 	*ename;
	int 	ecode;
	Npcfsys *fs;
	Npcfid 	*fid;
	Node 	*next;
};

/* util.c */
Npcfsys * xp_node_mount(char *addr, Npuser *user, Xkey *key);
