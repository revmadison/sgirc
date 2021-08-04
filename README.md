# sgirc

A super simple / barebones IRC client written as an exercise in learning Motif.
Focused on providing a native look and feel on SGIs.

Extremely early, many bugs exist, many things don't work.

Use at your own risk.
See license.txt for references to any open source included in this project.

## building

Built and tested with either MIPSPro or [RSE's](https://github.com/sgidevnet/sgug-rse) GCC by just typing `make`.

## running

`./sgirc <servername> <port> <nick>`
Currently all three arguments are required despite their (unused) presence in the settings file

## configuring

Upon running, `~/.sgirc/prefs` should be created. This is a standard JSON file with a few settings, only some of which work.
Currently you can disable the timestamps if you want more display area for text.
If you specify discordBridgeName, it will look for messages from that name and replace it with the first name mentioned. This was built specifically because I frequent a number of SGI-related IRC channels that use a discord bridge.

Normal names show up as `<nick>` while bridged names (assuming this is set) show up as `<<bridged_nick>>`

All other settings are ignored at the moment. I warned you this was early and unfinished!
