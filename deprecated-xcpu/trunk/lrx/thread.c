#include "include/lrx.h"

stdio_thread_info *g_stdout_thread_info, *g_stderr_thread_info;

void *stdio_thread(void *info){
    stdio_thread_info *io_t_info;
    char buf[100];int x, rc;
    io_t_info = (stdio_thread_info*)info;
    debug(2, "stdio_path: ");
    debug(2, io_t_info->stdio_path);
    debug(2, "\n");
    if((x=open(io_t_info->stdio_path, O_RDONLY))<0){
            fprintf(stderr, "can not open for reading: %s\n", io_t_info->stdio_path);
    }else{
        debug(2, "about to read from:"); 
        debug(2, io_t_info->stdio_path);
        debug(2, "\n");
    while(1){
        if((rc=read(x, buf, 100))>0){
            write(io_t_info->outdes, buf, rc);
        }else{
            if(rc==-1){
                fprintf(stderr, "error in reading stdio %d\n", io_t_info->outdes);
            }
            break;
        }
    }
    }
    x>=0?close(x):0;
    debug(7, "stdio thread reading from ");
    debug(7, io_t_info->stdio_path);
    free(io_t_info->stdio_path);
    free(io_t_info);
    pthread_exit(NULL);
}

void write_error(int err){
    
    switch(err){
        case EAGAIN:
            fprintf(stderr, "Non-blocking I/O has been selected\n");
            break;
        case EBADF:
            fprintf(stderr, "fd is not a valid file descriptor or is not open for writing.\n");
            break;
        case EFAULT:
            fprintf(stderr, "buf is outside your accessible address space.\n");
            break;
        case EFBIG:
            fprintf(stderr, "An  attempt was made to write a file that "
                    "exceeds the implementation-defined maximum file size\n");
            break;
        case EINTR:
            fprintf(stderr, "The call was interrupted by a signal before any data was written.\n");
            break;
        case EINVAL:
            fprintf(stderr, "fd is attached to an object which is unsuitable for writing.\n");
            break;
        case EIO:
            fprintf(stderr, "A low-level I/O error occurred while modifying the inode.\n");
            break;
        case ENOSPC:
            fprintf(stderr, "The device containing the file referred "
                    "to by fd has no room for the data.\n");
            break;
        case EPIPE:
            fprintf(stderr, "fd is connected to a pipe or socket whose reading end is closed.\n");
            break;
        default: fprintf(stderr, "Unknown error\n");
    }
}
            

void *start_thread(void *info){
    thread_info *t_info;
    char *session_clone, session_dir[255], *session_dir_path;
    int clone_des, rc=0, des1, des2/*, tdes*/, trc[2];
    char *env_path, *exec_path, *argv_path, *ctl_path;
    char character[8193];
    int i;
    pthread_t tids[2];
    trc[0]=trc[1]=0;
    t_info=(thread_info*)info;
    debug(4, "created thread with following info ->\n  ");
    debug(4, t_info->local_mounts.name);
    debug(4, "\n  ");
    debug(4, t_info->binary);
    debug(4, "\n  ");
    debug(4, t_info->argv);
    debug(4, "\n  ");
    
    session_clone=(char*)malloc(strlen(t_info->local_mounts.name)+7);
    sprintf(session_clone, "%s/clone", t_info->local_mounts.name);
    debug(4, "clone file to be opened: ");
    debug(4, session_clone);
    debug(4, "\n");
    if((clone_des=open(session_clone, O_RDONLY))<0){
            fprintf(stderr, "Error: opening clone file %s\n", session_clone);
    }
    if((rc=read(clone_des, session_dir, 255))<0){
        fprintf(stderr, "Error: reading clone file %s\n", session_clone);
    }
    else{
        session_dir[rc]='\0';
        session_dir_path=(char*)malloc(strlen(t_info->local_mounts.name)+strlen(session_dir)+2);
        sprintf(session_dir_path, "%s/%s", t_info->local_mounts.name, session_dir);
        debug(7, "Session directory path: ");
        debug(7, session_dir_path);
        debug(7, "\n");
        /* write environment if needed */
        env_path=(char*)malloc(strlen(session_dir_path)+5);
        sprintf(env_path, "%s/env", session_dir_path);
        debug(7, "env_path: ");
        debug(7, env_path);
        debug(7, "\n");
        if(t_info->env){
           if((des1=open(env_path, O_WRONLY))<0){
                fprintf(stderr, "Error: can not open %s for writing. \n", env_path);
            }else{
                i=0;
                while(t_info->env[i]){
                    if(write(des1, t_info->env[i], strlen(t_info->env[i])) == -1){
                        fprintf(stderr, "Error: writing env_variable to %s file\n", env_path);
                        write_error(errno);
                        break;
                    }else{
                        if(t_info->env[i+1]){
                        if(write(des1, "\n", 1) == -1){
                            fprintf(stderr, "Error: writing \\n %s file\n", env_path);
                            write_error(errno);
                            break;
                        }
                        }
                    }
                    i++;
                }
                close(des1);
            }
        }
        free(env_path);
        /*then copy binary*/
        exec_path=(char*)malloc(strlen(session_dir_path)+6);
        sprintf(exec_path, "%s/exec", session_dir_path);
        debug(7, "exec_path: ");
        debug(7, exec_path);
        debug(7, "\n");
        if((des1=open(exec_path, O_WRONLY))<0){
            fprintf(stderr, "Error: can not open %s for writing.\n", exec_path);
        }else
            if((des2=open(t_info->binary, O_RDONLY))<0){
                fprintf(stderr, "Error: can not open %s for reading.\n", t_info->binary);
            }else{
                while(1){
                    if((rc=read(des2, character, 8192))<=0){
                            if(close(des1)!=0){ /*?????*/
                                fprintf(stderr, "Error closing exec\n");
                            }
                            if(close(des2)!=0){
                                fprintf(stderr, "Error closing binary\n");
                            }
                            debug(7, "binary copied to exec\n");
                            break;
                    }else{
                        if(write(des1, character, rc)==-1){
                            fprintf(stderr, "Error while writing binary: %d\n", errno);
                            write_error(errno);
                            break;
                        }
                    }
                }
            }
        
        /* then write args*/
        argv_path=(char*)malloc(strlen(session_dir_path)+6);
        sprintf(argv_path, "%s/argv", session_dir_path);
        if((des1=open(argv_path, O_WRONLY))<0){
            fprintf(stderr, "Error: can not open: %s\n",argv_path);
        }else{
            write(des1, t_info->argv, strlen(t_info->argv));
            close(des1);
            debug(7, "written argv: ");
            debug(7, t_info->argv);
            debug(7, "\n");
        }
        /* then write exec into ctl file to start remote execution*/
        ctl_path=(char*)malloc(strlen(session_dir_path)+5);
        sprintf(ctl_path, "%s/ctl", session_dir_path);
        /*continuation of writing ctl*/
        if((des1=open(ctl_path, O_WRONLY))<0){
            fprintf(stderr, "Error: can not open: %s\n",ctl_path);
        }else{
            if(write(des1, "exec\n", 5)==-1){
                fprintf(stderr, "Error in writing ctl file\n");
                write_error(errno);
            }else
            debug(7, "exec written in ctl\n");
            close(des1);
        }
        
        
        /*then spawn threads for stderr and atdout*/
        g_stdout_thread_info=(stdio_thread_info*)malloc(sizeof(stdio_thread_info));
        g_stdout_thread_info->stdio_path=(char*)malloc(strlen(session_dir_path)+8);
        sprintf(g_stdout_thread_info->stdio_path, "%s/stdout", session_dir_path);
        g_stdout_thread_info->outdes=1;
        if((rc=pthread_create(&tids[0], NULL, stdio_thread, (void*)g_stdout_thread_info))==0){
            trc[0]=1;
        }else
            fprintf(stderr, "\nstdout thread creation error\n");
        g_stderr_thread_info=(stdio_thread_info*)malloc(sizeof(stdio_thread_info));
        g_stderr_thread_info->stdio_path=(char*)malloc(strlen(session_dir_path)+8);
        sprintf(g_stderr_thread_info->stdio_path, "%s/stderr", session_dir_path);
        g_stderr_thread_info->outdes=2;
        if((rc=pthread_create(&tids[1], NULL, stdio_thread, (void*)g_stderr_thread_info))==0){
            trc[1]=1;
        }else
            fprintf(stderr, "stderr thread creation error\n");
        
        free(session_dir_path);
        free(exec_path);
        free(argv_path);
        free(ctl_path);
        if(trc[0]){
            pthread_join(tids[0], NULL);
        }
        if(trc[1]){
            pthread_join(tids[1], NULL);
        }
    }
    free(session_clone);
    (clone_des>0)?close(clone_des):0;
    /* free the allocated variables after you are done*/
    free(t_info->local_mounts.name);
    free(t_info->binary);
    free(t_info->argv);
    free(t_info);
    pthread_exit(NULL);
}
