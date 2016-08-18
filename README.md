# print-wifi-nl

Tiny utility to print the Wi-Fi IP address, SSID and signal strength in percent
of a given interface.

Useful for integrating into scripts showing this information, eg.
[i3blocks](https://github.com/vivien/i3blocks).

## But you can just parse `iwconfig` or `iw dev <interface>`?

Not if your stupid proprietary Wi-Fi driver (**cough, cough** Broadcom's wl)
only allows access to this information as root through the usual tools.

## How do I build it?

You need **libnl** and **CMake**.

```
mkdir build
cd build
cmake ..
make install

```

## How do I use it?

`print-wifi-nl wlp3s0`

If it was successful, it returns status 0 and you get a tab-separated IP
address, SSID, and signal strength in percent.

If it was unsuccessful, or the interface is down, you get status 1.

## This code looks familiar...

It's taken from the brilliant guys behind
[i3status](https://github.com/i3/i3status).

Thanks for figuring out how to solve this problem with the libnl library! I've
just taken the code concerning Wi-Fi and adjusted it for my needs. I love
[i3wm](https://i3wm.org/) but I prefer to use
[i3blocks](https://github.com/vivien/i3blocks) to feed my status bar.
