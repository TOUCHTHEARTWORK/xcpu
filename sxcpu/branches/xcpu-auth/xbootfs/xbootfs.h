enum {
	/* root level files */
	Qroot = 1,
	Qctl,
	Qdata,
	Qavail,
	Qredir,
	Qmax,
	
	Bufsize = 4192,

	Dbgfn	= 1,
	Dbgfs	= 1<<1,
	Dbgcfg	= 1<<2,
	Dbgthr	= 1<<3,
	Dbgloop	= 1<<3,
	Dbgrd	= 1<<4,
	Dbgwr	= 1<<5,
};

/* xbootfs.c */
static void	usage(char *name);
static void	debug(int level, char *fmt, ...);
//static void	read_config(void);
static void	fsinit(void);
static int	ctl_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
//static int 	ctl_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	data_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int 	avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	avail_wstat(Spfile* file, Spstat* stat);
static Spfile	*dir_next(Spfile *dir, Spfile *prevchild);
static Spfile	*dir_first(Spfile *dir);
