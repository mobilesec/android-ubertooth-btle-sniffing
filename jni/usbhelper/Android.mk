LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= usbhelper.c
LOCAL_MODULE := usbhelper
LOCAL_C_INCLUDES += jni/libusb-compat/libusb
LOCAL_SHARED_LIBRARIES := libc libusb libusb-compat
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 
include $(BUILD_SHARED_LIBRARY)
