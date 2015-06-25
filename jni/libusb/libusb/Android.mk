
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

#LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES:= \
	core.c \
	descriptor.c \
	io.c \
	sync.c \
	os/linux_usbfs.c


LOCAL_C_INCLUDES += $(LOCAL_PATH)/../android \
	$(LOCAL_PATH)/libusb \
	$(LOCAL_PATH)/libusb/os 

LOCAL_CFLAGS += -g
LOCAL_CFLAGS += -fPIC -DPIC


ifeq ($(TARGET_BUILD_TYPE),release)
	LOCAL_CFLAGS += -g
endif

LOCAL_MODULE:= libusb

LOCAL_PRELINK_MODULE := false 
include $(BUILD_SHARED_LIBRARY)
