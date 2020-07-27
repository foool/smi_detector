# smi_detector
SMI (System Management Interrupt) detector, detect SMI info.

# Note: Copy from https://lwn.net/Articles/316622/ and makes two minor changes:
    1. use *msleep_interruptible* instead of *wake_up_interruptible* & add header file linux/delay.h
    2. use *stop_machine* instead of *stop_machine_run*
    
# How to use
   
   ## 1 make sure you mount the debugfs
    1.1 mkdir /debugfs
    1.2 mount -t debugfs none /debugfs
    
    
   ## 2 make and insmod
    2.1. make
    2.2. insmod ./smi_detector.ko enabled=1 threshold=100
    
   ## 3 find smi info from /debugfs/smi_detector
    
