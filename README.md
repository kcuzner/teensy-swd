# Teensy SWD
## Kevin Cuzner

No, this isn't using SWD on a Teensy 3.1 (which would be nice)...it's making
the Teensy into a SWD dongle!

**NOTE: This project is in progress**

### Why?

So, I have a chicken-and-the-egg problem. I assembled one of my [kl2-dev board]
(https://github.com/kcuzner/kl2-dev) prototypes and then realized that I needed
some way to load the initial bootloader on to it. I had realized this before,
but I never got around to actually doing something about it until now.

### What?

This project is the host and firmware for a USB to SWD adaptor. The objective
is that the host program should be able to take an intel hex or elf file and
issue the appropriate SWD commands via USB to program that program into the
device, so long as it lives in RAM.

Alternately, I could execute the flash write commands directly from SWD, but
that could be super super slow. I imagine that I will have some sort of
bootstrap program which writes up to 1K (or some amount) to the flash from some
data that I have previously loaded via SWD into a specific location in RAM.
The SWD will then be used to poll another specific memory value to see when the
program has finished writing its flash.

