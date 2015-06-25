LOCAL_PATH := $(call my-dir)
subdirs := $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, \
    libbtbb \
    libusb \
    libusb-compat \
    usbhelper \
    ubertooth \
))
include $(subdirs)
