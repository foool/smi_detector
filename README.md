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
        Note: adjust the value of threshold, according to the performance of your hardware
    
   ## 3 find smi info. from /debugfs/smi_detector
    
# An alternative way to monitor the SMI is using Linux kernel tracer `hwlat_detector`

    Refer to Linux docs for more : https://www.kernel.org/doc/html/latest/trace/hwlat_detector.html
    Note: must check the option box 'kernel hacking / Tracers'
