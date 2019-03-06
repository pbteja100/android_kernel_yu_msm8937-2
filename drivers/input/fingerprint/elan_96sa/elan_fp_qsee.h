#ifndef _LINUX_ELAN_FP_H
#define _LINUX_ELAN_FP_H

#define ELAN_IOCTLID               0x80
#define ID_IOCTL_INIT                   _IOW(ELAN_IOCTLID, 0,  int) // To Get Raw Image (14->8)
#define ID_IOCTL_READ_REGISTER          _IOW(ELAN_IOCTLID, 2,  int)
#define ID_IOCTL_WRITE_REGISTER         _IOW(ELAN_IOCTLID, 3,  int)
#define ID_IOCTL_RESET                  _IOW(ELAN_IOCTLID, 6,  int)
//#define ID_IOCTL_GET_RAW_IMAGE          _IOW(ELAN_IOCTLID, 10, int) // To Get Raw Image (Original)
#define IOCTL_READ_KEY_STATUS    _IOW(ELAN_IOCTLID, 10, int)
#define IOCTL_WRITE_KEY_STATUS    _IOW(ELAN_IOCTLID, 11, int)
#define ID_IOCTL_STATUS                 _IOW(ELAN_IOCTLID, 12, int)
#define ID_IOCTL_SET_AUTO_RAW_IMAGE     _IOW(ELAN_IOCTLID, 13, int)
#define ID_IOCTL_GET_AUTO_RAW_IMAGE     _IOW(ELAN_IOCTLID, 14, int)
#define ID_IOCTL_READ_CMD               _IOW(ELAN_IOCTLID, 15, int) // General read cmd
#define ID_IOCTL_WRITE_CMD              _IOW(ELAN_IOCTLID, 16, int) // General write cmd
#define ID_IOCTL_IOIRQ_STATUS           _IOW(ELAN_IOCTLID, 17, int) // Use INT to read buffer
#define ID_IOCTL_SPI_STATUS             _IOW(ELAN_IOCTLID, 18, int) // UPdate SPI Speed & CS delay
#define ID_IOCTL_SIG_PID                _IOW(ELAN_IOCTLID, 19, int) // WOE signal event to pid
#define ID_IOCTL_POLL_INIT              _IOW(ELAN_IOCTLID, 20, int)
#define ID_IOCTL_READ_ALL               _IOW(ELAN_IOCTLID, 21, int) // added v1.441 In IRQ, read all image data, not only one raw.
#define ID_IOCTL_INPUT_KEYCODE          _IOW(ELAN_IOCTLID, 22, int)
#define ID_IOCTL_POLL_EXIT              _IOW(ELAN_IOCTLID, 23, int)
#define ID_IOCTL_READ_FACTORY_STATUS    _IOW(ELAN_IOCTLID, 26, int)
#define ID_IOCTL_WRITE_FACTORY_STATUS   _IOW(ELAN_IOCTLID, 27, int)
#define ID_IOCTL_WAKE_LOCK_UNLOCK		_IOW(ELAN_IOCTLID, 41, int)
#define ID_IOCTL_EN_IRQ                 _IOW(ELAN_IOCTLID, 55, int)
#define ID_IOCTL_DIS_IRQ                _IOW(ELAN_IOCTLID, 66, int)
#define ID_IOCTL_POWER_SET              _IOW(ELAN_IOCTLID, 77, int)

#define ID_IOCTL_REQUSEST_IRQ     _IOW(ELAN_IOCTLID, 88, int)
#define ID_IOCTL_CHIP_DETECT_OK     _IOW(ELAN_IOCTLID, 89, int)

// release info.
#define ID_IOCTL_SET_VERSION	_IOW(ELAN_IOCTLID, 100, int)
#define ID_IOCTL_GET_VERSION	_IOW(ELAN_IOCTLID, 101, int)

//read power status
#define ID_IOCTL_GET_SCREEN_STATUS	_IOW(ELAN_IOCTLID, 102, int)


#define CUSTOMER_IOCTLID                0xD0 //For customer define

#endif /* _LINUX_ELAN_FP_H */

