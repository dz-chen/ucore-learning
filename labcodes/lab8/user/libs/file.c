#include <defs.h>
#include <string.h>
#include <syscall.h>
#include <stdio.h>
#include <stat.h>
#include <error.h>
#include <unistd.h>

/****************************************************************************
 *                  文件操作的系统调用用户层接口
 * 1.调用的sys_xxx位于./syscall.c,这里边再间接调用内核中的syscall.c中的系统调用
 * **************************************************************************/


// path是文件路径,open_flags是打开方式(只读、只写、读写)
int open(const char *path, uint32_t open_flags) {
    return sys_open(path, open_flags);
}

// 关闭文件
int close(int fd) {
    return sys_close(fd);
}


// 从文件fd读取len个字节到内存base处, 返回实际读取的字节数
int read(int fd, void *base, size_t len) {
    return sys_read(fd, base, len);
}


int write(int fd, void *base, size_t len) {
    return sys_write(fd, base, len);
}

int seek(int fd, off_t pos, int whence) {
    return sys_seek(fd, pos, whence);
}

int fstat(int fd, struct stat *stat) {
    return sys_fstat(fd, stat);
}

int fsync(int fd) {
    return sys_fsync(fd);
}

int dup2(int fd1, int fd2) {
    return sys_dup(fd1, fd2);
}

static char transmode(struct stat *stat) {
    uint32_t mode = stat->st_mode;
    if (S_ISREG(mode)) return 'r';
    if (S_ISDIR(mode)) return 'd';
    if (S_ISLNK(mode)) return 'l';
    if (S_ISCHR(mode)) return 'c';
    if (S_ISBLK(mode)) return 'b';
    return '-';
}

void print_stat(const char *name, int fd, struct stat *stat) {
    cprintf("[%03d] %s\n", fd, name);
    cprintf("    mode    : %c\n", transmode(stat));
    cprintf("    links   : %lu\n", stat->st_nlinks);
    cprintf("    blocks  : %lu\n", stat->st_blocks);
    cprintf("    size    : %lu\n", stat->st_size);
}

