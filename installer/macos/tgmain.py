# PyInstaller entry point for the macOS build of the Telegram proxy:
# the same CLI the Windows build runs as `python -m proxy.tg_ws_proxy`.
from proxy.tg_ws_proxy import main

if __name__ == "__main__":
    main()
