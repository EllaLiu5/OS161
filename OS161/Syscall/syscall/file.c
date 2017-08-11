#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Add your file-related functions here ...
 */
int filetable_init (struct thread* nt){
    for (int i = 0; i < 3; ++i){
        struct vnode* vn;
        char* fname = kstrdup ("con:");
        if (vfs_open(fname, i?O_WRONLY:O_RDONLY, 0, &vn)){
            kfree (fname);
            return EINVAL;
        }
    
        void* ptr= kmalloc (sizeof (struct fdesc)); 
        if (!ptr){
            vfs_close (vn);
            kfree (fname);
            return EINVAL;
        }
        nt -> fdtable[i] = ptr;
        strcpy (nt->fdtable[i]->file_name,fname);
        kfree (fname);
        *(nt -> fdtable[i]) = (struct fdesc)
        {
            .vn = vn,
            .flags = i? O_WRONLY:O_RDONLY,
            .offset = 0,
            .refcount = 1,
            .lk = lock_create(fname)
        };
    }
    return 0;
}

int sys_open(const char *filename, int flags, mode_t mode, int *retval) {

    int result=0, index = 3;
    struct vnode *vn;
    char *kbuf;
    size_t len;
    kbuf = (char *) kmalloc(sizeof(char)*PATH_MAX);
    result = copyinstr((const_userptr_t)filename,kbuf, PATH_MAX, &len);
    if(result) {
        kfree(kbuf);
        *retval = -1;
        return EFAULT;
    }

    while (curthread->fdtable[index] != NULL ) {
        index++;
    }

    if(index >= OPEN_MAX) {
        kfree(kbuf);
        *retval = -1;
        return ENFILE;
    }

    curthread->fdtable[index] = (struct fdesc *)kmalloc(sizeof(struct fdesc*));
    if(curthread->fdtable[index] == NULL) {
        kfree(kbuf);
        *retval = -1;
        return ENFILE;
    }

    result = vfs_open(kbuf,flags,mode,&vn);
    if(result) {
        kfree(kbuf);
        kfree(curthread->fdtable[index]);
        curthread->fdtable[index] = NULL;
        *retval = -1;
        return result;
    }

    curthread->fdtable[index]->vn = vn;
    strcpy(curthread->fdtable[index]->file_name, kbuf);
    curthread->fdtable[index]->flags = flags;
    curthread->fdtable[index]->refcount = 1;
    curthread->fdtable[index]->offset = 0;
    curthread->fdtable[index]->lk= lock_create(kbuf);
    *retval = index;
    kfree(kbuf);
    return 0;
}

int sys_close(int filehandle, int *retval) {

    if(filehandle >= OPEN_MAX || filehandle < 0) {
        *retval = -1;
        return EBADF;
    }

    if(curthread->fdtable[filehandle] == NULL) {
        *retval = -1;
        return EBADF;
    }

    if(curthread->fdtable[filehandle]->vn == NULL) {
        *retval = -1;
        return EBADF;
    }

    if(curthread->fdtable[filehandle]->refcount == 1) {
        vfs_close(curthread->fdtable[filehandle]->vn);
        lock_destroy(curthread->fdtable[filehandle]->lk);
        kfree(curthread->fdtable[filehandle]);
        curthread->fdtable[filehandle] = NULL;
    } else {
        curthread->fdtable[filehandle]->refcount -= 1;
    }

    *retval = 0;
    return 0;
}

int sys_read(int filehandle, void *buf, size_t size, int *retval) {

    if(buf == NULL) {
        *retval = -1;
        return EFAULT;
    }

    if(filehandle >= OPEN_MAX || filehandle < 0) {
        *retval = -1;
        return EBADF;
    }

    if(curthread->fdtable[filehandle] == NULL) {
        *retval = -1;
        return EBADF;
    }

    if (curthread->fdtable[filehandle]->flags == O_WRONLY || curthread->fdtable[filehandle]->flags == (O_WRONLY | O_CREAT) || curthread->fdtable[filehandle]->flags == (O_WRONLY | O_EXCL) || curthread->fdtable[filehandle]->flags == (O_WRONLY | O_TRUNC) || curthread->fdtable[filehandle]->flags == (O_WRONLY | O_APPEND)) {
        *retval = -1;
        return EBADF;
    }

    struct iovec iov;
    struct uio ku;
    char *kbuf = (char*)kmalloc(size);
    if(kbuf == NULL) {
        *retval = -1;
        return EFAULT;
    }
    //check user buf
    int result = copyin((const_userptr_t)buf,kbuf,size);
    if(result) {
        kfree(kbuf);
        *retval = -1;
        return result;
    }

    lock_acquire(curthread->fdtable[filehandle]->lk);
    uio_kinit(&iov, &ku, (void*)kbuf, size ,curthread->fdtable[filehandle]->offset,UIO_READ);
    
    result = VOP_READ(curthread->fdtable[filehandle]->vn, &ku);
    if(result) {
        *retval = -1;
        kfree(kbuf);
        lock_release(curthread->fdtable[filehandle]->lk);
        return result;
    }

            
    curthread->fdtable[filehandle]->offset = ku.uio_offset;

    result = copyout((const void *)kbuf, (userptr_t)buf, size);
    if(result) {
        *retval = -1;
        kfree(kbuf);
        lock_release(curthread->fdtable[filehandle]->lk);
        return result;
    }
    *retval = size - ku.uio_resid;
    kfree(kbuf);
    lock_release(curthread->fdtable[filehandle]->lk);
    return 0;

}

int sys_write(int filehandle, const void *buf, size_t size, int *retval) {
    if(buf == NULL) {
        *retval = -1;
        return EFAULT;
    }
    if(filehandle >= OPEN_MAX || filehandle < 0) {
        *retval = -1;
        return EBADF;
    }

    if(curthread->fdtable[filehandle] == NULL) {
        *retval = -1;
        return EBADF;
    }

    if (curthread->fdtable[filehandle]->flags == O_RDONLY || curthread->fdtable[filehandle]->flags == (O_RDONLY | O_CREAT) || curthread->fdtable[filehandle]->flags == (O_RDONLY | O_EXCL) || curthread->fdtable[filehandle]->flags == (O_RDONLY | O_TRUNC) || curthread->fdtable[filehandle]->flags == (O_RDONLY | O_APPEND)) {
        *retval = -1;
        return EBADF;
    }


    struct iovec iov;
    struct uio ku;
    char *kbuf = (char*)kmalloc(size);
    if(kbuf == NULL) {
        *retval = -1;
        return EINVAL;
    }

    lock_acquire(curthread->fdtable[filehandle]->lk);

    int result = copyin((const_userptr_t)buf,kbuf,size);
    if(result) {
        kfree(kbuf);
        lock_release(curthread->fdtable[filehandle]->lk);
        *retval = -1;
        return result;
    }

    uio_kinit(&iov, &ku, kbuf, size ,curthread->fdtable[filehandle]->offset,UIO_WRITE);

    result = VOP_WRITE(curthread->fdtable[filehandle]->vn, &ku);
    if(result) {
        kfree(kbuf);
        lock_release(curthread->fdtable[filehandle]->lk);
        *retval = -1;
        return result;
    }

    curthread->fdtable[filehandle]->offset = ku.uio_offset;

    *retval = size - ku.uio_resid;

    kfree(kbuf);
    lock_release(curthread->fdtable[filehandle]->lk);
    return 0;
}

int sys_lseek(int filehandle, off_t pos, int whence, int *retval, int *retval1) {

    if(filehandle >= OPEN_MAX || filehandle < 0) {
        *retval = -1;
        return EBADF;
    }

    if(curthread->fdtable[filehandle] == NULL) {
        *retval = -1;
        return EBADF;
    }

    int result = 0;
    off_t offset;
    struct stat statbuf;

    lock_acquire(curthread->fdtable[filehandle]->lk);

    if(!VOP_ISSEEKABLE(curthread->fdtable[filehandle]->vn)){
        lock_release(curthread->fdtable[filehandle]->lk);
        *retval = -1;
        return ESPIPE;
    }
    switch(whence){
        case SEEK_SET:
            offset = pos;
            break;

        case SEEK_CUR:
            offset = curthread->fdtable[filehandle]->offset + pos;
            break;

        case SEEK_END:
            result = VOP_STAT(curthread->fdtable[filehandle]->vn, &statbuf);
            if(result) {
                lock_release(curthread->fdtable[filehandle]->lk);
                *retval = -1;
                return result;
            }
            offset = statbuf.st_size + pos;
            break;
        
        default:
            lock_release(curthread->fdtable[filehandle]->lk);
            *retval = -1;
            return EINVAL;
            break;
    }

    if(offset < (off_t)0) {
        *retval = -1;
        lock_release(curthread->fdtable[filehandle]->lk);
        return EINVAL;
    }
    curthread->fdtable[filehandle]->offset = offset;
    *retval = (uint32_t)((offset & 0xFFFFFFFF00000000) >> 32);
    *retval1 = (uint32_t)(offset & 0xFFFFFFFF);
    lock_release(curthread->fdtable[filehandle]->lk);
    return 0;
}

int sys_dup2(int fd, int new_fd, int* retval){
    int err = check_fd(fd, O_RDONLY, retval);
    if(err) return err;
    err = check_fd(new_fd,-2, retval);
    if(err) return err;
    
    if(fd == new_fd){
        *retval = new_fd;
        return 0;
    }

    if(curthread->fdtable[new_fd] != NULL){
        sys_close(new_fd, retval);
        return EBADF;
    }   

    lock_acquire(curthread->fdtable[fd]->lk);
    curthread->fdtable[new_fd] = (struct fdesc *)kmalloc(sizeof(struct fdesc*));
    strcpy(curthread->fdtable[new_fd]->file_name, curthread->fdtable[fd]->file_name);
    curthread->fdtable[new_fd]->offset = curthread->fdtable[fd]->offset;
    curthread->fdtable[new_fd]->lk = lock_create("new_fd");
    curthread->fdtable[new_fd]->vn = curthread->fdtable[fd]->vn;
    curthread->fdtable[new_fd]->flags = curthread->fdtable[fd]->flags;
    curthread->fdtable[fd]->refcount += 1;
    curthread->fdtable[new_fd]->refcount = curthread->fdtable[fd]->refcount;
    *retval = new_fd;
    lock_release(curthread->fdtable[fd]->lk);
    return 0;   
}

int check_fd(int fd, int mode, int* retval){

    if(fd<0||fd>=OPEN_MAX){
        *retval = -1;
        return EBADF;
    }
    if(mode == -2){ //for dup2 fd2
        return 0;
    }
    
    if(curthread->fdtable[fd] == NULL){
        *retval = -1;
        return EBADF;
    }
    if(mode == -1) //for close
        return 0;
    if(curthread->fdtable[fd]->flags == O_RDWR)
        return 0;
    if(mode != curthread->fdtable[fd]->flags){
        *retval = -1;
        return EINVAL;
    }
    return 0;
        
}
