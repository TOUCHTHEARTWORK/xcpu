enum {
	/* root level files */
	Qroot = 1,
	Qctl,
	Qdata,
	Qavail,
	Qredir,
	Qlog,
	Qmax,
	
	Bufsize = 4192,

	Dbgfn	= 1,
	Dbgfs	= 1<<1,
	Dbgclnt	= 1<<2,
};

/* xbootfs.c */
static void	usage(char *name);
static void	debug(int level, char *fmt, ...);
static void	fsinit(void);
static int	data_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	redir_read(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int 	avail_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static int	avail_wstat(Spfile* file, Spstat* stat);
static void	avail_closefid(Spfilefid *fid);
static Spfile	*dir_next(Spfile *dir, Spfile *prevchild);
static Spfile	*dir_first(Spfile *dir);
static Spfcall *xflush(Spreq *req);
static int	log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);
static void	connopen(Spconn *conn);
static void	connclose(Spconn *conn);
