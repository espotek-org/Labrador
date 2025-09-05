[![GitHub All Releases](https://img.shields.io/github/downloads/espotek-org/Labrador/total?logo=github&style=for-the-badge)](https://github.com/espotek-org/Labrador/releases/latest)

![GitHub Downloads (specific asset, specific tag)](https://img.shields.io/github/downloads/espotek-org/Labrador/continuous-win32/Labrador_Installer.exe)

![GitHub Downloads (specific asset, specific tag)](https://img.shields.io/github/downloads/espotek-org/Labrador/continuous-Linux/EspoTek_Labrador-ca7ac83-x86_64.AppImage)

![GitHub Downloads (specific asset, specific tag)](https://img.shields.io/github/downloads/espotek-org/Labrador/continuous-macOS/Labrador.dmg)

# What is it?
The [EspoTek Labrador](http://espotek.com/labrador) is an open-source board that turns your PC, Raspberry Pi, or Android Smartphone into a full-featured electronics lab bench, complete with oscilloscope, signal generator, and more.

This repo hosts all of the software and hardware that makes Labrador possible.

# Tutorial
If you're new to Labrador or oscilloscopes in general, I strongly recommend checking out the fantastic [tutorial series](https://www.wellys.com/posts/courses_electronics/) produced by Lief Koepsel.  It features well-written, rich articles as well as video content that explains everything more clearly than I ever could!

# Getting Started
Binary (executable) versions of the software are available for [download](https://github.com/espotek-org/Labrador/releases) for several platforms.

For the documentation, please visit the [wiki](https://github.com/espotek-org/Labrador/wiki).

## Building from Source
If you're looking to build from source but don't know where to start, Qt Creator is the easiest way to get your toes wet!  
https://www.qt.io/download-open-source/  
When installing, make sure you tick the box to install Qt 5.15 or later.

Once it's installed, open `Desktop_Interface/Labrador.pro`, then Clean All -> Run `qmake` -> Build All.

If you're on Linux (including Raspberry Pi), then you can also build the software from source by cloning the repo, `cd`ing to the `Desktop_Interface` directory, then running:  
```
QT_SELECT=qt5 qmake
make
sudo make install
sudo ldconfig
```
Then, to launch, just type `labrador` into the terminal.  Additional packages may be required, please see the full [build instructions](https://github.com/espotek-org/Labrador/wiki/Building-from-source) on the wiki.

On macOS, additional steps may be required.  See issue https://github.com/espotek-org/Labrador/issues/238

To build the AVR software, I use Atmel Studio 7.  Just load up the .atsln and push F7.  You can use `avr-gcc` if you don't want to install a full IDE.

The PCB files can be edited in KiCAD 5.0 or later.

# Extras
There are community contributed 3D printable cases available at Thingiverse, courtesy of SpaceBex and Bostwickenator:
* https://www.thingiverse.com/thing:3188243
* https://www.thingiverse.com/thing:4705392

Dave Messink has designed [a case that can be laser cut from 3mm plywood](https://github.com/espotek-org/Labrador/files/13813693/Re__Labrador_Case.1.zip).  The [binding posts](https://www.amazon.com/dp/B07YKYP8MN) and [cables](https://www.amazon.com/dp/B08KZGPTLM) he used are from Amazon.  
![Top view](https://github.com/espotek-org/Labrador/assets/22040436/7245c645-ce89-41ae-a505-a47f29ab8875)
![Bottom view](https://github.com/espotek-org/Labrador/assets/22040436/7ac3882c-1c8f-4fad-9f9a-03112eef8ff8)

# Licence
All Desktop software files are licenced under [GNU GPL v3](https://www.gnu.org/licenses/gpl.html).

All Microcontroller software files, with the exception of those provided by Atmel, are licenced under the [3-Clause BSD License](https://opensource.org/licenses/BSD-3-Clause).

All hardware files (schematics, PCB) are licenced under [Creative Commons 4.0 (CC BY-NC-SA)](https://creativecommons.org/licenses/by-nc-sa/4.0/).

# Collaboration
If you want to submit a Pull Request, bug report, or feature request, please feel free to do so here at GitHub.  
If you just want to say hello and remind me that people are actually using my product (or if you just don't want to make a GitHub account), please email admin@espotek.com

Thanks to all.  
~Chris
