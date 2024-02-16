#include "xget.h"

static time_t now(void);
static time_t future(time_t delta);
static void   removeworker(File *f, Worker *worker);
static int    do_xgetd();

int port = XBOOT_PORT;
int servicetime = 10;
int maxservicetime = 30;
int maxworkers = 10;

static void 
usage(char *name) {
	fprintf(stderr, "%s [-D debuglevel] [-p port] [-w maxworkers] file|directory\n", name);
	exit(1);
}

int
main(int argc, char **argv) 
{
	int ecode, arg, len, i;
	pid_t pid;
	char *argval, *ename;
	struct stat st;
	
	while ((arg = getopt(argc, argv, "D:p:s:w:")) != -1) {
		switch (arg) {
		case 'p':
			port = strtol(optarg, &argval, 10);
			if(*argval != '\0')
				usage(argv[0]);
			break;

		case 'D':
			debuglevel = strtol(optarg, &argval, 10);
			if(*argval != '\0')
				usage(argv[0]);
			break;

		case 's':
			maxservicetime = servicetime = strtol(optarg, &argval, 10);
			if (*argval != '\0')
				usage(argv[0]);
			break;
			
		case 'w':
			maxworkers = strtol(optarg, &argval, 10);
			if (*argval != '\0')
				usage(argv[0]);
			break;

		default:
			usage(argv[0]);
		}
	}

	/* Validate command line options */
	if (optind >= argc) {
		fprintf(stderr, "Please specify at least one filename.\n");
		usage(argv[0]);
	}


	/* Initialize 9P user/group */
	if(init_xget_usr() == -1)
		goto error;


	spc_chatty = debuglevel & Dbgclnt;
	
	/* Validate file arguments */
	if( (argc - optind) != 1) {
		fprintf(stderr, "Please specify file or directory to serve\n");
		usage(argv[0]);
	}
	
	i = argc - 1;
	if(stat(argv[i], &st) < 0)
		sp_uerror(errno);
	
	len = strlen(argv[i]) + 1;
	xgetpath = (char *) malloc(len * sizeof(char));
	snprintf(xgetpath, len, "%s", argv[i]);
	if (xgetpath[len-2] == '/')
		xgetpath[len-2] = '\0';
	
	fprintf(stderr, "server set path to %s\n", xgetpath);
	if (S_ISDIR(st.st_mode))
		recursive = 1;
	else
		recursive = 0;
		
	if (localfileread(root, xgetpath) < 0)
		goto error;
	
	
	/* Prepare server */
	if(init_server(0) == -1)
	  goto error;

	
	if(!debuglevel && !spc_chatty) {
		/* daemonize */
	  	close(0);
		close(1);
		close(2);

		pid = fork();
		if (pid < 0) {
			fprintf(stderr, "cannot fork\n");
			return -1;
		}

		if (pid != 0) {
			/* parent */
			return 0;
		}

		/* child */
		setsid();
		chdir("/");
	}

	/* Do the actual work */
	if(do_xgetd() == -1)
		goto error;
	
 error:
	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s\n", ename);
	if (xgetpath)
		free(xgetpath);
	
	return -1;
}       

static
int do_xgetd() {
	int ecode;
	char* ename;

	/* Go into server loop */
	while(1) {
		sp_rerror(&ename, &ecode);
		if (ecode != 0)
			break;

		sp_poll_once();
	}
	
	/* Should never get here */
	return -1;
}

static 
void removeworker(File *f, Worker *worker) {
	Worker *cur;

	if(!worker)
		return;

	/* In the case where a pointer was not set to null, this will ensure
	   that we do not try to free a dangling pointer
	*/
	for(cur = f->firstworker; cur; cur = cur->next) {
		if(cur == worker)
			break; 	/* Continue with delete */
	}
	if(!cur)
		return; /* Not found in queue, already removed */

	debug(Dbgfn, "worker %p is done\n", worker, worker->ip, worker->port);
	if (worker == f->firstworker)
		f->firstworker = worker->next;

	if (worker == f->lastworker)
		f->lastworker = worker->prev;

	if (worker == f->nextworker)
		f->nextworker = worker->next;

	if (worker->next)
		worker->next->prev = worker->prev;
	if (worker->prev)
		worker->prev->next = worker->next;

	f->numworkers--;
	free(worker->ip);
	free(worker);
}


static
time_t now(void) {
	struct timeval t;
	gettimeofday(&t, 0);
	return t.tv_sec;
}

static
time_t future(time_t delta) {
	return now() + delta;
}


