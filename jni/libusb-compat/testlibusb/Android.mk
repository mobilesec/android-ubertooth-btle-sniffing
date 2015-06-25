LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CFLAGS += -g
LOCAL_SRC_FILES:= testlibusb.c
LOCAL_MODULE := testlibusb
LOCAL_C_INCLUDES += jni/libusb-compat/libusb jni/libusb/libusb
LOCAL_SHARED_LIBRARIES := libc libusb libusb-compat
include $(BUILD_EXECUTABLE)
