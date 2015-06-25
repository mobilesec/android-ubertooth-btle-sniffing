/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <string.h>
#include <jni.h>
#include <usb.h>
#include <libusb.h>
#include <android/log.h>
#include <errno.h>
#define LOG_TAG "USBHelper" // text for log tag 

// For keeping track of the devices, made global to handle callbacks and still
// have the device information
static struct libusb_device_handle *devh = NULL;
int ndev = 0;
int sample = 0;

jstring
Java_com_gnychis_ubertooth_Core_USBMon_initUSB( JNIEnv* env, jobject thiz )
{
  int r;
  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "entering initUSB");
  r = libusb_init(NULL);
  if(r < 0) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "failed to initialize libusb");
    return (*env)->NewStringUTF(env, "Failed to initialize libusb!");
  } else {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "successfully initialized libusb");
    return (*env)->NewStringUTF(env, "USB library successfully enabled...");
  }
}

jint
Java_com_gnychis_ubertooth_Core_USBMon_USBList( JNIEnv* env, jobject thiz )
{
  struct usb_bus *bus;

  usb_init();
  usb_find_busses();
  usb_find_devices();

  for (bus = usb_busses; bus; bus = bus->next) {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "bus: 0x%x (%s - %u)", bus,bus->dirname,bus->location);
    if (bus->root_dev) {
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "root_dev: 0x%x", bus->root_dev);
    } else {
      struct usb_device *dev;

      for (dev = bus->devices; dev; dev = dev->next) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "dev: 0x%x", dev);
      }
    }
  }
}

jint
Java_com_gnychis_ubertooth_Core_USBMon_USBcheckForDevice( JNIEnv* env, jobject thiz, jint vid, jint pid )
{
  ssize_t cnt;
  libusb_device **devs;
	libusb_device *dev;
  int i=0,ret;
  
  // Get the usb device list
  cnt = libusb_get_device_list(NULL, &devs);



//__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "device count: %d\n", cnt);
  if(cnt < 0)
    return -1;

  // Go through the devices and see if the one we are looking for exists
  ret=0;
	while ((dev = devs[i++]) != NULL) {
		struct libusb_device_descriptor desc;
		int r = libusb_get_device_descriptor(dev, &desc);
		if (r < 0) {
			return -1;
		}

//__android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "device : %d - %d %d\n", i, desc.idVendor, desc.idProduct);

    if(desc.idVendor==vid && desc.idProduct==pid) {
      ret=1;
      break;
    }
	}

  libusb_free_device_list(devs, 1);

  return ret;
}

