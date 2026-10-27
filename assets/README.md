# DMIND application icon

`dmind.png` is the creator-supplied `icon1200x1200.png` logo, copied without changing its pixels, colors or transparency.

`dmind.ico` packages the same logo at 16, 20, 24, 32, 40, 48, 64, 128 and 256 pixels for the Windows executable, shortcuts and installer. To regenerate it, install Pillow and run `python scripts/make-app-icon.py`. Normal builds use the checked-in ICO and do not require Python or Pillow.
