LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

#LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES:= \
	core.c \
	


LOCAL_C_INCLUDES += $(LOCAL_PATH)/../android \
	$(LOCAL_PATH)/libusb \
	external/libusb-1.0.0/libusb

LOCAL_CFLAGS +=  -g
LOCAL_CFLAGS += -fPIC -DPIC


LOCAL_SHARED_LIBRARIES := libusb


ifeq ($(TARGET_BUILD_TYPE),release)
	LOCAL_CFLAGS += -g
endif

LOCAL_MODULE:= libusb-compat

LOCAL_PRELINK_MODULE := false 
include $(BUILD_SHARED_LIBRARY)

