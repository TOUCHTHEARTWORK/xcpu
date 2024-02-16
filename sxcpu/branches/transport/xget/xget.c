#include "xget.h"

static void    xget_cleanup();
static void    file_cleanup(File*);
static int     netfileread(Spfile *parent, char* fname);
static int     netmount(char *address, Spuser *user, int port);
static int     do_xget();

Spcfsys *masterfs, *redirfs;
char *netaddress = 0;
char *outname;
int servicetime = 10;
int maxservicetime = 30;
int maxretries = 1;
int port = XBOOT_PORT;
int servicetimedone = 0;
int fcount = 0;


static void 
usage(char *name) {
        fprintf(stderr, "%s [-D debuglevel] [-p port] [-s servicetime] <-n netaddr> <src file | src dir> [src file | src dir] ... dest\n", name);
	exit(1);
}

int
main(int argc, char **argv) 
{
	int fileargs, srvport, ecode, i;
	char arg, *argval, *ename;
	struct stat st;
	masterfs = redirfs = NULL;
	files = NULL;
	
	while ((arg = getopt(argc, argv, "D:p:n:s:r:")) != -1) {
		switch (arg) {
		case 'p':
			srvport = strtol(optarg, &argval, 10);
			if(*argval != '\0')
				usage(argv[0]);
			break;

		case 'D':
			debuglevel = strtol(optarg, &argval, 10);
			if(*argval != '\0')
				usage(argv[0]);
			break;

		case 'n':
			netaddress = optarg;
			break;

		case 's':
			maxservicetime = servicetime = strtol(optarg, &argval, 10);
			if (*argval != '\0')
				usage(argv[0]);
			break;
			
		case 'r':
			maxretries = strtol(optarg, &argval, 10);
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

	if(!netaddress) {
		fprintf(stderr, "Must specify server ip address.\n");
		usage(argv[0]);
	}


	/* Initialize 9P user/group */
	if(init_xget_usr() == -1)
		goto error;


	spc_chatty = debuglevel & Dbgclnt;

	/* Validate file arguments */
	if( (fileargs = argc - optind) < 1) {
		fprintf(stderr, "Please specify at least a destination\n");
		usage(argv[0]);
	}
	
	outname = argv[argc - 1];
	if(stat(outname, &st) != 0) {
	  sp_uerror(errno);
	}
	
	if(!S_ISDIR(st.st_mode) && fileargs > 2) {
		fprintf(stderr, "Plural sources and nondirectory destination.\n");
		usage(argv[0]);
	}
	
	
	/* Contact master */
	if(netmount(netaddress, user, srvport) < 0)
		goto error;
	

	/* Prepare file download callbacks */
	if(fileargs == 1) {
		/* Prepare callbacks for recursive download */
		if(netfileread(root, NULL) < 0)
			goto error;
	} else {
		/* Prepare callback for each request file */
		for(i = optind, fcount = 0; i < argc - 1; i++) {
			fcount++;
			if(netfileread(root, argv[i]) < 0)
				goto error;
		}
	}
	
	
	/* If client will re-serve files, prepare server here */
	if(servicetime) {
		if(init_server(0) == -1)
			goto error;
	}
	
	/* Do the actual work */
	if(do_xget() == -1)
		goto error;

	/* Close out all files, clean up */
	xget_cleanup();

	return 0;

 error:
 	sp_rerror(&ename, &ecode);
	fprintf(stderr, "%s\n", ename);
	
	/* Close out all files, clean up */
	xget_cleanup();

	return -1;
}

static
int do_xget() {
	int ecode;
	char* ename;
  
	/* Sit in while loop, processing callbacks (downloading/sharing files) */
	while(servicetime || fcount > 0) {
		if(servicetimedone) {
			debug(Dbgfn, "done downloading and listening, exiting cleanly\n");
			break;
		}
	    
		sp_rerror(&ename, &ecode);
		if(ecode != 0) 
			return -1;
		
		sp_poll_once();
	}
	
	/* TODO: for all files, do md5 sum */

	return 0;
}

static
void xget_cleanup() {
	File *cur, *del;

	if(masterfs && masterfs != redirfs)
		spc_umount(masterfs);
	if(redirfs)
		spc_umount(redirfs);
	
	for(cur = files; cur != NULL; ) {
		del = cur;
		cur = cur->next;
		file_cleanup(del);
	}
}

static
void file_cleanup(File *f) {
	if(!f)
		return;

	spc_close(f->datafid);
	spc_close(f->availfid);
	free(f);
}

static int 
netfileread(Spfile *parent, char* fname) {
	if(!fname)
		fprintf(stderr, "Debugging: downloading full tree\n");
	else
		fprintf(stderr, "Debugging: downloading %s\n", fname);
	return 0;
}

static int
netmount(char *address, Spuser *user, int port)
{
	if ( !(masterfs = spc_netmount(address, user, port, NULL, NULL))) {
		fprintf(stderr, "Unable to mount master at %s,%d\n", address, port);
		return -1;
	}

	logfid = spc_open(masterfs, "log", Owrite);
	if (!logfid) {
		spc_umount(masterfs);
		masterfs = NULL;
		return -1;
	}

	return 0;
}

