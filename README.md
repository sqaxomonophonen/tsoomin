A tiny x11 mouse wheel zoomer inspired by Boomer (https://github.com/tsoding/boomer) and xbindkeys. After the program starts it's invisible (has no window), but uses `XGrabButton()` to intercept mouse wheel + Mod4 (Windows key) events, which triggers a screen shot you can zoom into. While zooming you can pan with WASD/arrows and LMB, and return to normal with [ESC] or the other mouse buttons.

![](./demo.gif)

## Building / configuring
Run `make` (depends on `cc`, `libx11`, `libXext`, `libGL`).
See top of [tsoomin.c](./tsoomin.c) for configuration options. You can override them like this:
`CFLAGS="-DZOOM_SPEED=0.1 -DMOTION_BLUR_SPEED=0.1" make clean all`
