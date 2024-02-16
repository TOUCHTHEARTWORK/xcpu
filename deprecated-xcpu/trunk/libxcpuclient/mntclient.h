/*
 * function definitions for the xcpuclient library
 */

enum
{
	/* this is for I/O. You should NEVER use this for stack variables unless your stack is big enough. */
	Bufsize = 8168,
	/* this constant should be large enough to hold typical xcpu names
	  * such as the clone file output, arch, uname, etc. 
	  * Since xcpu names are a 32-bit string in hex, 
	  * and arch and uname are pretty small, 32 should be fine. 
	  */
	Namesize = 32,
};

typedef struct Exe Exe;
typedef struct Xcpuio Xcpuio;

struct Exe
{
	char *mnt;
	char *session;
	char *binary;
	int pflag;
	int sin, sout, serr;
};

struct Xcpuio 
{
	Channel *chan;		/* report here when done */
	char *mnt;		/* mounted root of fs */
	char *session;		/* session id */
	char *name;		/* file */
	int fd;			/* local file */
	int pflag;
};


String *getarch(char *);
int copy(int, int);
void copybinary(char *, char *, char *);
int copybio(char *, int r, int, int);
void catfile(void *);
int writestring(char *, char *, char *);
String *rxclone(char *);
