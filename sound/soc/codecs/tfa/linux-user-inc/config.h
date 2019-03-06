#ifndef __CONFIG_LINUX_USER_INC__
#define  __CONFIG_LINUX_USER_INC__

#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "NXP_I2C.h"

#define MAX_I2C_BUFFER_SIZE NXP_I2C_BufferSize()

/* dbgprint.h */
#define _ASSERT(e)

#ifdef TFA_APP
#include "dbgprint.h"
#endif

#if defined __ANDROID__ && !defined(TFA_APP)
#ifdef LOG_TAG
#	undef LOG_TAG
#endif
#	define LOG_TAG "TFA98XX"

#ifdef TFA_DEBUG
#	ifdef LOG_NDEBUG
#		undef LOG_NDEBUG
#	endif
#	define LOG_NDEBUG 0
#endif

#include <utils/Log.h>

#ifndef _ERRORMSG
#define _ERRORMSG(fmt,va...) ALOGE("ERROR %s:%d: "fmt,__func__,__LINE__, ##va)
#endif
#ifndef ERRORMSG
#define ERRORMSG(x...) _ERRORMSG(x)
#endif
#ifndef PRINT
#define PRINT(fmt,va...) ALOGV("%s:%d: "fmt,__func__,__LINE__, ##va)
#endif
#ifndef PRINT_ERROR
#define PRINT_ERROR(x...) _ERRORMSG(x)
#endif
#ifndef PRINT_ASSERT
#define PRINT_ASSERT(e)if ((e)) ALOGE("PrintAssert: %s:%d: error code:%d\n",__FUNCTION__,__LINE__, e)
#endif

#else

#ifndef _ERRORMSG
#define _ERRORMSG(fmt,va...) fprintf(stdout, "tfa98xx: ERROR %s:%s:%d: "fmt,__FILE__,__func__,__LINE__, ##va)
#endif
#ifndef ERRORMSG
#define ERRORMSG(x...) _ERRORMSG(x)
#endif
#ifndef PRINT
#define PRINT(...)	printf(__VA_ARGS__)
#endif
#ifndef PRINT_ERROR
#define PRINT_ERROR(...)	 fprintf(stdout,__VA_ARGS__)
#endif
#ifndef PRINT_ASSERT
#define PRINT_ASSERT(e)if ((e)) fprintf(stdout, "PrintAssert:%s (%s:%d) error code:%d\n",__FUNCTION__,__FILE__,__LINE__, e)
#endif

#ifndef __GNUC__
typedef int bool;
#define false 0
#define true 1
#endif

#endif

#define tfa98xx_trace_printk PRINT
#define pr_debug PRINT
#define pr_info PRINT
#define pr_err ERRORMSG

#define GFP_KERNEL 0

typedef unsigned gfpt_t;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef _Bool  bool;

enum {
	false = 0,
	true = 1
};

/* tfa/src/tfa_osal.c */
void *kmalloc(size_t size, gfpt_t flags);
void kfree(const void *ptr);

/* tfa/src/tfa_osal.c */
unsigned long msleep_interruptible(unsigned int msecs);

/* tfa/src/tfa_container_crc32.c */
uint32_t crc32_le(uint32_t crc, unsigned char const *buf, size_t len);


#endif /* __CONFIG_LINUX_USER_INC__ */

