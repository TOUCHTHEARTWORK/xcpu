#include "xget.h"

static int log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req);

Spdirops root_ops = {
	.first = dir_first,
	.next = dir_next,
};

Spfileops log_ops = {
	.write = log_write,
};

static int
log_write(Spfilefid *fid, u64 offset, u32 count, u8 *data, Spreq *req)
{
	fprintf(stderr, "Client %s: %.*s", fid->fid->conn->address, count, data);
	return count;
}

Spfile*
dir_first(Spfile *dir)
{
	if (recursive)
		dir_update(dir);
       
	spfile_incref(dir->dirfirst);
	return dir->dirfirst;
}

Spfile*
dir_next(Spfile *dir, Spfile *prevchild)
{
	spfile_incref(prevchild->next);
	return prevchild->next;
}

/* Clean up this function */
static Spfile *
create_file(Spfile *parent, char *name, u32 mode, u64 qpath, void *ops, 
	Spuser *usr, void *aux)
{
	Spfile *ret;

	ret = spfile_alloc(parent, name, mode, qpath, ops, aux);
	if (!ret)
		return NULL;

	if (parent) {
		if (parent->dirlast) {
			parent->dirlast->next = ret;
			ret->prev = parent->dirlast;
		} else
			parent->dirfirst = ret;

		parent->dirlast = ret;
		if (!usr)
			usr = parent->uid;
	}

	if (!usr)
		usr = user;

	//	ret->atime = ret->mtime = time(NULL);
	ret->atime = ret->mtime = 0;
	ret->uid = ret->muid = usr;
	ret->gid = usr->dfltgroup;
	spfile_incref(ret);
	return ret;
}


void fsinit()
{
	root = spfile_alloc(NULL, "", 0555 | Dmdir, Qroot, &root_ops, NULL);
	root->parent = root;
	spfile_incref(root);
	root->atime = root->mtime = time(NULL);
	root->uid = root->muid = user;
	root->gid = user->dfltgroup;
	create_file(root, "log", 0222, Qlog, &log_ops, NULL, NULL);
}

int
localfileread(Spfile *parent, char *filename) {
	char *name;
	struct stat st;

	if (stat(filename, &st) < 0) {
		sp_uerror(errno);
		return -1;
	}

	/* Get the basename of the filename */
	if ((name = strrchr(filename, '/'))) 
		name++;
	else 
		name = filename;	

	/* lots more */
	return 0;
}

void
dir_remove(Spfile *dir)
{
	Spfile *f;
	Spdirops *dops;
  
	dops = dir->ops;
	for(f = dops->first(dir); f != NULL; f = dops->next(dir, f))
		if (f->mode & Dmdir)
			dir_remove(f);
		else
			spfile_decref(f);
	
	spfile_decref(dir);
}

int
fullfilename(Spfile *file, char** fullname) 
{
	int len, retlen;
	
	if(!file)
		return 0;
	len = strlen(file->name);
	if(len <= 0) {
		*fullname[0]='/';
		return 1;
	}

	retlen = fullfilename(file->parent, fullname);
	snprintf(*(fullname)+retlen, len + 2,"%s/", file->name);
	return(retlen + len + 1);
}

/* Clean up this function */
int
dir_update(Spfile *dir)
{
	char lname[NAME_MAX + 1];
	Spfile *f;
	struct stat st;
	struct dirent *de;
	DIR*  dirstr;
	int namelen, len, found;
	typedef struct Spfut Spfut;
	struct Spfut {
		Spfile *f;
		Spfut *next;
	};

	Spfut *sputhead, *sputcur, *sputprev;

	sputhead = sputcur = sputprev = NULL;
	namelen = strlen(xgetpath) + 1;

	if(strlen(dir->name)) {
		if(!namebuf) 
			namebuf = (char*) malloc(NAME_MAX + 1);
		
		fprintf(stderr,"Calling fullfilename on %s\n", dir->name);
		if( (len = fullfilename(dir,&namebuf)) <= 0 || len > NAME_MAX) {
			printf("Failed\n");
			namebuf[0] = '\0';
			return -1;
		}
		namelen += len;
		snprintf(lname,namelen+1,"%s%s", xgetpath, namebuf);
		if (lname[namelen - 2] == '/') 
			lname[namelen - 2] = '\0';
		namebuf[0] = '\0';
	}
	else
		strncpy(lname,xgetpath,namelen);

	printf("***Full file name: %s***\n", lname);
	
	if (stat(lname, &st) < 0) {
		fprintf(stderr, "File or directory removed\n");
		//remove file from 9p fs
	}

	if (st.st_mtime == dir->mtime)
		fprintf(stderr,"Directory or file not changed\n");
	else {
		fprintf(stderr, "Directory or file changed\n");

		if (S_ISREG(st.st_mode)) {
			int fd;
			File *file;
			char *data;

			//reload file
			file = (File *) dir->aux;
			if (file->data)
				 munmap(file->data, file->datasize);

			if( (fd = open(lname, O_RDONLY)) < 0) 
				goto error;
			
			data = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED | MAP_FILE, fd, 0);
			if (data == (void *) -1)
				goto error;
			
			close(fd);
			file->datasize = file->datalen = st.st_size;
			dir->mtime = st.st_mtime;
		}
		
		else if (S_ISDIR(st.st_mode)) {
			/* Capture 9P directory entries.  Anything not removed from
			 * local directory entry matches will need to be pruned from
			 * the 9P directory.
			 */
			for(f = dir->dirfirst; f != NULL; f = f->next) {
				if(f->name[0] == '.') {
					continue;
				}
				if (!strcmp(f->name, ""))
					continue;

				sputcur = (Spfut *) malloc(sizeof(Spfut));
				if(!sputhead)
					sputhead = sputcur;
				if(sputprev)
					sputprev->next = sputcur;
				sputcur->f = f;
				sputprev = sputcur;
			}

			dirstr = opendir(lname);
			while ((de = readdir(dirstr))) {
				found = 0;

				len = strlen(de->d_name);
				if(namelen + len + 2 > NAME_MAX) 
					break;
				
				snprintf(lname+namelen,len + 2,"/%s",de->d_name);
				if (stat(lname,&st) < 0)
					goto error;

				/* Ignore hidden directories */
				if(de->d_name[0] == '.')
					continue;
				
				for(sputcur = sputhead, sputprev = NULL; sputcur != NULL; sputcur = sputcur->next) {
					fprintf(stderr, "walked to %p\n", sputcur);
					if (strcmp(de->d_name, sputcur->f->name) == 0) {
						fprintf(stderr, "9P file match %s\n", 
							sputcur->f->name);
						found = 1;
						if(sputcur == sputhead) 
							sputhead = sputcur->next;
						else
							sputprev->next = sputcur->next;
						free(sputcur);
						break;
					}
					sputprev = sputcur;
				}
			
				if(!found) {
					if(S_ISREG(st.st_mode)) {
						//	if(localfileread(dir, name, 0) < 0) {
						//	fprintf(stderr, "Error doing localfileread\n");
						//	continue;
						//}
						fprintf(stderr, "New file found, doing localfileread\n");
					}
					else if(S_ISDIR(st.st_mode)) {
						//spfile_create(dir)
						fprintf(stderr, "New dir found, doing localfileread\n");
					}
				}
			}
			
			lname[namelen] = '\0';
			for(sputcur = sputhead, sputprev = NULL; sputcur != NULL; ) {
				fprintf(stderr, "Stale file found, removing %s\n", sputcur->f->name);
				//dir_remove(sputcur);
				sputprev = sputcur;
				sputcur = sputcur->next;
				free(sputprev);
			}
			dir->mtime = st.st_mtime;
		}
	}
	return 0;
	
 error:
	sp_uerror(errno);
	return -1;
}
