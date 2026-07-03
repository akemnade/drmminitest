# Simple drm test tools

These tools are minimalistic, so they can fit into any
minimal initrd, minimal deps. E.g. with OpenEmbedded,
a static link can be down to around 50KB.

## qrdrm <string>

displays a qrcode. Idea is to automatically check whether display
is working by reading that qrcode

## drmtouch [invert] [mt]

test touchscreens, paint points. mt enables multitouch.
