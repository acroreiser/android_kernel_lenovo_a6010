#ifndef _UAPI_LINUX_MEMFD_H
#define _UAPI_LINUX_MEMFD_H

/* flags for memfd_create(2) (unsigned int) */
#define MFD_CLOEXEC		0x0001U
#define MFD_ALLOW_SEALING	0x0002U
/* not executable and sealed to prevent changing to executable. */
#define MFD_NOEXEC_SEAL		0x0008U
/* executable */
#define MFD_EXEC		0x0010
#endif /* _UAPI_LINUX_MEMFD_H */
