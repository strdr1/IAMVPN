# PyInstaller entry point for the macOS build of the Telegram proxy:
# the same CLI the Windows build runs as `python -m proxy.tg_ws_proxy`.
#
# Without the `cryptography` package, proxy/_aes.py falls back to ctypes and
# ctypes.util.find_library("crypto"), which on macOS returns the unversioned
# /usr/lib/libcrypto.dylib; macOS aborts any process that loads it. Point the
# lookup at the OpenSSL that PyInstaller already bundles for the _ssl module.
import ctypes.util
import glob
import os
import sys

_bundle = getattr(sys, "_MEIPASS", None)
if _bundle:
    _found = sorted(glob.glob(os.path.join(_bundle, "**", "libcrypto*.dylib"), recursive=True))
    if _found:
        _orig_find_library = ctypes.util.find_library

        def _find_library(name):
            if name == "crypto":
                return _found[0]
            return _orig_find_library(name)

        ctypes.util.find_library = _find_library

from proxy.tg_ws_proxy import main  # noqa: E402

if __name__ == "__main__":
    main()
