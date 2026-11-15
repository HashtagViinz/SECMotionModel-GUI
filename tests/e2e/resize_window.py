#!/usr/bin/env python3
"""Resize a SECMotionModel GUI window through X11.

Used by scripts/window_resize_test.sh to exercise the swapchain/ImGui resize
path without a physical display. Linux/X11 only.
"""

from __future__ import annotations

import argparse
import ctypes
import time

x11 = ctypes.CDLL("libX11.so.6")
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
x11.XDefaultRootWindow.restype = ctypes.c_ulong
x11.XQueryTree.argtypes = [
    ctypes.c_void_p,
    ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
    ctypes.POINTER(ctypes.c_uint),
]
x11.XGetWindowProperty.argtypes = [
    ctypes.c_void_p,
    ctypes.c_ulong,
    ctypes.c_ulong,
    ctypes.c_long,
    ctypes.c_long,
    ctypes.c_int,
    ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
]
x11.XInternAtom.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
x11.XInternAtom.restype = ctypes.c_ulong
x11.XGetGeometry.argtypes = [
    ctypes.c_void_p,
    ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_ulong),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_int),
    ctypes.POINTER(ctypes.c_uint),
    ctypes.POINTER(ctypes.c_uint),
    ctypes.POINTER(ctypes.c_uint),
    ctypes.POINTER(ctypes.c_uint),
]
x11.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XFetchName.argtypes = [
    ctypes.c_void_p,
    ctypes.c_ulong,
    ctypes.POINTER(ctypes.c_char_p),
]
x11.XFree.argtypes = [ctypes.c_void_p]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("display", nargs="?", default=":0")
    parser.add_argument("pid", type=int)
    parser.add_argument("width", type=int, nargs="?", default=1200)
    parser.add_argument("height", type=int, nargs="?", default=680)
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--delay", type=float, default=0.35)
    parser.add_argument("--wait", type=float, default=5.0)
    return parser.parse_args()


class X11:
    def __init__(self, display: str) -> None:
        self.display = x11.XOpenDisplay(display.encode())
        if not self.display:
            raise SystemExit(f"cannot open display {display}")
        self.root = x11.XDefaultRootWindow(self.display)
        self.pid_atom = x11.XInternAtom(self.display, b"_NET_WM_PID", 0)

    def window_pid(self, window: int) -> int | None:
        actual = ctypes.c_ulong()
        fmt = ctypes.c_int()
        count = ctypes.c_ulong()
        after = ctypes.c_ulong()
        data = ctypes.POINTER(ctypes.c_ubyte)()
        status = x11.XGetWindowProperty(
            self.display,
            window,
            self.pid_atom,
            0,
            1,
            0,
            0,
            ctypes.byref(actual),
            ctypes.byref(fmt),
            ctypes.byref(count),
            ctypes.byref(after),
            ctypes.byref(data),
        )
        if status == 0 and data:
            value = ctypes.cast(data, ctypes.POINTER(ctypes.c_ulong))[0]
            x11.XFree(data)
            return int(value)
        return None

    def children(self, window: int) -> list[int]:
        root_return = ctypes.c_ulong()
        parent_return = ctypes.c_ulong()
        children = ctypes.POINTER(ctypes.c_ulong)()
        count = ctypes.c_uint()
        if not x11.XQueryTree(
            self.display,
            window,
            ctypes.byref(root_return),
            ctypes.byref(parent_return),
            ctypes.byref(children),
            ctypes.byref(count),
        ):
            return []
        result = [children[index] for index in range(count.value)]
        if children:
            x11.XFree(children)
        return result

    def title(self, window: int) -> str:
        name = ctypes.c_char_p()
        if x11.XFetchName(self.display, window, ctypes.byref(name)) and name.value:
            label = name.value.decode(errors="replace")
            x11.XFree(name)
            return label
        return ""

    def find(self, window: int, pid: int) -> int | None:
        if self.window_pid(window) == pid and "SECMotionModel GUI" in self.title(window):
            return window
        for child in self.children(window):
            found = self.find(child, pid)
            if found:
                return found
        return None

    def size(self, window: int) -> tuple[int, int]:
        root = ctypes.c_ulong()
        x = ctypes.c_int()
        y = ctypes.c_int()
        width = ctypes.c_uint()
        height = ctypes.c_uint()
        border = ctypes.c_uint()
        depth = ctypes.c_uint()
        x11.XGetGeometry(
            self.display,
            window,
            ctypes.byref(root),
            ctypes.byref(x),
            ctypes.byref(y),
            ctypes.byref(width),
            ctypes.byref(height),
            ctypes.byref(border),
            ctypes.byref(depth),
        )
        return width.value, height.value

    def resize(self, window: int, width: int, height: int) -> None:
        x11.XResizeWindow(self.display, window, width, height)
        x11.XFlush(self.display)


def main() -> int:
    args = parse_args()
    xdisplay = X11(args.display)
    window = None
    deadline = time.monotonic() + args.wait
    while window is None and time.monotonic() < deadline:
        window = xdisplay.find(xdisplay.root, args.pid)
        if window is None:
            time.sleep(0.1)
    if not window:
        raise SystemExit(f"window for pid {args.pid} not found")
    print("initial size:", xdisplay.size(window))
    for index in range(args.repeat):
        # Alternate between the requested size and the initial 1280x720 to
        # exercise both grow and shrink paths while frames are in flight.
        size = (args.width, args.height) if index % 2 == 0 else (1280, 720)
        xdisplay.resize(window, *size)
        time.sleep(args.delay)
        print(f"resize {index + 1}:", xdisplay.size(window))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
