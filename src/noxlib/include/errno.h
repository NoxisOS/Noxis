/**
 * @file    noxlib/include/errno.h
 * @brief   Error number declarations
 */
#ifndef _NOXLIB_ERRNO_H
#define _NOXLIB_ERRNO_H

/* errno is a global integer set by failing syscall wrappers. */
extern int errno;

#define EPERM     1   /* Operation not permitted        */
#define ENOENT    2   /* No such file or directory      */
#define ESRCH     3   /* No such process                */
#define EINTR     4   /* Interrupted system call        */
#define EIO       5   /* I/O error                      */
#define ENOMEM   12   /* Out of memory                  */
#define EACCES   13   /* Permission denied              */
#define EFAULT   14   /* Bad address                    */
#define EBUSY    16   /* Device or resource busy        */
#define EEXIST   17   /* File exists                    */
#define ENODEV   19   /* No such device                 */
#define ENOTDIR  20   /* Not a directory                */
#define EISDIR   21   /* Is a directory                 */
#define EINVAL   22   /* Invalid argument               */
#define ENFILE   23   /* File table overflow            */
#define EMFILE   24   /* Too many open files            */
#define ENOSPC   28   /* No space left on device        */
#define EPIPE    32   /* Broken pipe                    */

#endif /* _NOXLIB_ERRNO_H */
