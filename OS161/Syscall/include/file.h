/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>
#include <types.h>
#include <thread.h>

/*
 * Put your function declarations and data types here ...
 */
/* File Descriptor Structure */
struct fdesc{
	char file_name[__NAME_MAX];
	struct vnode *vn; //   - Reference to the underlying file 'object'
	off_t offset;     //      - Offset into the file
	struct lock *lk;   //- Some form of synchronization
	int flags;      // - Flags with which the file was opened
	int refcount; //- Reference count
};

int sys_open(const char *filename, int flags,mode_t mode, int *retval);
int sys_close(int filehandle, int *retval);
int sys_write(int filehandle, const void *buf, size_t size, int *retval);
int sys_read(int filehandle, void *buf, size_t size, int *retval);
int sys_lseek(int filehandle, off_t pos, int whence, int *retval, int *retval1);
int filetable_init(struct thread*);
int sys_dup2(int fd, int new_fd, int* retval);
int check_fd(int fd, int mode, int* retval);
#endif /* _FILE_H_ */
