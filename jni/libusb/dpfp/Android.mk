LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CFLAGS += -g
LOCAL_SRC_FILES:= dpfp.c
LOCAL_MODULE := dpfp
LOCAL_C_INCLUDES += jni/libusb
LOCAL_SHARED_LIBRARIES := libc libusb
include $(BUILD_EXECUTABLE)
