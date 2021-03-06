# Cypress FX3 support
The Cypress FX3 chip is USB chip used in various devices. It can be reprogrammed
on the fly through the USB bus. This allows loading alternative firmware into
existing hardware.

u3bench has basic support for the FX3 chip running the cyfxbulksrcsink firmware
example from the SDK.

DISCLAIMER: Running different firmware on existing hardware might damage the
hardware. Use at own risk.

## Compiling the firmware
The fx3/bin/ directory already contains precompiled firmware and tools for
Ubuntu 20.04. If this doesn't work you'll have to compile it yourself.

To compile the firmware the FX3 SDK is required. 

Two patches are available in the fx3/ directory:

 * cyfxbulksrcsink_no_gpio.patch: Disables all GPIO of the firmware to make is
   safer to run on standard hardware.
 * cyusb_download_standalone.patch: Static link libcyusb and change config path
   to make download tools more standalone.

See SDK documentation on how to setup environment and compile source.

## Loading the firmware
To load the cyfxbulksrcsink firmware into a device first make sure that the
VID:PID of the device are in the cyusb.conf file. Then run the following
command to temporary load the firmware into the device RAM and execute it:

    # cd fx3/bin
    # ./download_fx3 -t RAM -i cyfxbulksrcsink-NO_GPIO.img

Make sure you have permissions to the target device or run as root.

When you power cycle the device it should return to its original firmware.

## Running u3bench with FX3 device
To use a FX3 device with this firmware loaded run u3bench as follows:

    # ./u3bench -T fx3

If multiple devices are connected the first one will be used unless a
device is explicitly selected with the '-D' option. For example:

    # lsusb
    ...
    Bus 002 Device 003: ID 04b4:00f1 Cypress Semiconductor Corp. FX3
    Bus 002 Device 008: ID 04b4:00f1 Cypress Semiconductor Corp. FX3
    ...
    # ./u3bench -T fx3 -D 002.003

## Troubleshooting
When running u3bench for multiple device it might happen that it runs out of
device memory. This results in a weird error. But when you run it in verbose
mode you'll see that a LibUSB fails due to lack of memory. If this happens you
can lower the transfer size with the '-l' option. Or change the BUFFER_CNT
define in the source.
