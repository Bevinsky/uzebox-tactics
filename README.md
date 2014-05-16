uzebox-tactics
==============

A TBS two player game for the Uzebox game console

Compilation Instructions for Windows
====================================

* Install latest WinAVR, add C:\WinAVR-20100110\bin to Path.

* Install MinGW, add C:\MinGW\bin to Path.

* Install GNU Make for Windows, add C:\Program Files (x86)\GnuWin32\bin\ to Path.

* Run Make in the 'default' directory.

Getting Cmder Working With Make
-------------------------------

* Rename C:\Program Files (x86)\cmder\cmder\vendor\msysgit\bin\sh.exe to sh.exe.backup

Cmder automatically adds msysgit to PATH and sh.exe gets in the way of Make's shell detection.
