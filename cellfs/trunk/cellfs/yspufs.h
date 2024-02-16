/*
 * Copyright (C) 2005 by Latchesar Ionkov <lucho@ionkov.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * LATCHESAR IONKOV AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define MFILE_SIZE(p)	*((u64 *)(p))
#define MFILE_MTIME(p)	*(u32 *)((u8 *)(p) + 8)
#define MFILE_FLAGS(p)	*(u32 *)((u8 *)(p) + 12)
#define MFILE_DPTR(p)	*(u64 *)((u8 *)(p) + 16)
#define MFILE_DSIZE(p)	*(u64 *)((u8 *)(p) + 32)

int register_dev(int, Spsrv *);
Spsrv *rootfs_init(int debuglevel);
void rootfs_loop(void);
int ramfs_init();
int npfs_init();
int ufs2_init();
int logfs_init();
int pipefs_init();
