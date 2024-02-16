typedef struct Blockdev Blockdev;
typedef struct Netdev Netdev;
typedef struct Xsession Xsession;
typedef struct Xfilebuf Xfilebuf;
typedef struct Fsfid Fsfid;
typedef struct Rxfile Rxfile;
typedef struct Rxcopy Rxcopy;
typedef struct Tspawn Tspawn;

#define QBITS		24
#define QMASK		((1<<QBITS) - 1)
#define QPATH(id)	((id + 1) << 24)

enum {
	Read,
	Write,
};

struct Xfilebuf {
	int 		size;
	char*		buf;
};

enum {
	/* root level files */
	Qroot = 1,
	Qclone,
	Qarch,

	/* session level files */
	Qctl = 1,
	Qinfo,
	Qid,
	Qfs,
};

/* states */
enum {
	Initializing,
	Running,
	Finished,
	Wiped = 16,
};

/* modes */
enum {
	Normal,
	Persistent,
};

/* types supported */
enum {
	TypeNone = 0,
	TypeQemu,
	TypeXen,
};

struct Blockdev {
	int		boot;
	char*		devname;
	char*		devimage;

	Blockdev*	next;
};

struct Netdev {
	char*		id;
	char*		mac;

	Netdev*		next;
};

struct Xsession {
	int		type;
	int		refcount;
	int		sid;
	int		state;
	int		mode;

	char*		gid;
	int		lid;

	char*		ctl;
	Spreq*		ctlreq;
	int		ctlpos;

	char*		dirpath;
	int		pid;
	int		monin;
	int		monout;

	int		memsize;
	int		cpus;		/* number of cpus, used by xen */
	Blockdev*	bdevs;
	Netdev*		ndevs;
	char*		vmname;	/* name of the virtual machine for Xen */
	char*		vmimage;
	char*		cfg;	/* config file used by xen */
	
	Spfile*		sroot;
	Spfile*		fsdir;
	Xsession*	next;
};

struct Fsfid {
	char*		path;
	int		omode;
	int		fd;
	DIR*		dir;
	int		diroffset;
	char*		direntname;
	struct stat	stat;
	Xsession*	xs;
};

struct Tspawn;
struct Rxcopy;

struct Rxfile {
	Spcfsys*	fs;
	char*		name;
	u32		perm;
	int		create;	/* if 1, create, otherwise open witn Otrunc */

	char*		path;	/* if not NULL the file's content is read from the fs */
	char*		buf;	/* if path NULL, the content of the file, freed when Rxfile is destroyed */
	int		bufsize;
	int		buflen;
	Rxfile*		next;

	/* required for the copying */
	int		fd;
	Spcfid*		fid;
	Spcfd*		spcfd;
	int		pos;
};

extern Xsession *sessions;

/* ufs.c */
int ufs_clone(Spfid *fid, Spfid *newfid);
int ufs_walk(Spfid *fid, Spstr *wname, Spqid *wqid);
Spfcall *ufs_open(Spfid *fid, u8 mode);
Spfcall *ufs_create(Spfid *fid, Spstr *name, u32 perm, u8 mode, Spstr *extension);
Spfcall *ufs_read(Spfid *fid, u64 offset, u32 count, Spreq *);
Spfcall *ufs_write(Spfid *fid, u64 offset, u32 count, u8 *data, Spreq *);
Spfcall *ufs_clunk(Spfid *fid);
Spfcall *ufs_remove(Spfid *fid);
Spfcall *ufs_stat(Spfid *fid);
Spfcall *ufs_wstat(Spfid *fid, Spstat *stat);
void ufs_fiddestroy(Spfid *fid);
void ufs_attach(Spfid *nfid, Xsession *xs, Spqid *qid);

int session_incref(Xsession *xs);
void session_decref(Xsession *xs);
void ctl_execute_commands(Xsession *xs);

/* tspawn.c */
int tspawn(Xsession *xs, int maxsessions, char *dest);

/* file.c */
Rxfile *rxfile_create_from_file(Spcfsys *fs, char *name, char *path);
Rxfile *rxfile_create_from_buf(Spcfsys *fs, char *name, char *buf, int buflen);
void rxfile_destroy(Rxfile *f);
void rxfile_destroy_all(Rxfile *f);
Rxcopy *rxfile_copy_start(Rxfile *files, void (*cb)(void *), void *);
int rxfile_copy_finish(Rxcopy *c);

/* xen.c */
int monitor_command_xen(Xsession *xs, char *cmd);
int start_xen(Xsession *xs, int freeze);
int execute_command_xen(Xsession *xs, char *s);

/* qemu.c */
int monitor_command_qemu(Xsession *xs, char *cmd);
int start_qemu(Xsession *xs, int freeze);
int execute_command_qemu(Xsession *xs, char *s);
