#!/bin/bash
for file in /data/data/$1/lib/*.so; do 
  bn=$(basename $file)
  rm /system/lib/$bn
  ln -s $file /system/lib/$bn
done
