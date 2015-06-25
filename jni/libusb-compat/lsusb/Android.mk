LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= lsusb.c
LOCAL_MODULE := lsusb_core
LOCAL_C_INCLUDES += jni/libusb-compat/libusb
LOCAL_SHARED_LIBRARIES := libc libusb libusb-compat
include $(BUILD_EXECUTABLE)
