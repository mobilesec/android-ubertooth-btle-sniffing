LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= ubertooth.c ubertooth-rx.c ubertooth_control.c
LOCAL_MODULE := ubertooth_rx
LOCAL_C_INCLUDES += jni/libusb jni/libbtbb
LOCAL_SHARED_LIBRARIES := libc libusb libbtbb
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= ubertooth.c ubertooth-util.c ubertooth_control.c
LOCAL_MODULE := ubertooth_util
LOCAL_C_INCLUDES += jni/libusb jni/libbtbb
LOCAL_SHARED_LIBRARIES := libc libusb libbtbb
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= ubertooth.c ubertooth-btle.c ubertooth_control.c
LOCAL_MODULE := ubertooth_btle
LOCAL_C_INCLUDES += jni/libusb jni/libbtbb
LOCAL_SHARED_LIBRARIES := libc libusb libbtbb
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= ubertooth.c ubertooth_helper.c ubertooth_control.c
LOCAL_MODULE := ubertooth
LOCAL_C_INCLUDES += jni/libusb jni/libbtbb
LOCAL_SHARED_LIBRARIES := libc libusb libbtbb
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog 
include $(BUILD_SHARED_LIBRARY)
