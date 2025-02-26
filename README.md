[![GitHub All Releases](https://img.shields.io/github/downloads/espotek-org/Labrador/total?logo=github&style=for-the-badge)](https://github.com/espotek-org/Labrador/releases/latest)

![GitHub Downloads (specific asset, specific tag)](https://img.shields.io/github/downloads/espotek-org/Labrador/continuous-win32/Labrador_Installer.exe)

![GitHub Downloads (specific asset, specific tag)](https://img.shields.io/github/downloads/espotek-org/Labrador/continuous-Linux/EspoTek_Labrador-ca7ac83-x86_64.AppImage)

![GitHub Downloads (specific asset, specific tag)](https://img.shields.io/github/downloads/espotek-org/Labrador/continuous-macOS/Labrador.dmg)

# What is it?
The [EspoTek Labrador](http://espotek.com/labrador) is an open-source board that turns your PC, Raspberry Pi or Android Smartphone and into a full-featured electronics lab bench, complete with oscilloscope, signal generator and more.

This repo hosts all of the software and hardware that makes Labrador possible.

# Tutorial
If you're new to Labrador or oscilloscopes in general, I strongly recommend checking out the fantastic tutorial series produced by Lief Koepsel:   
https://www.wellys.com/posts/courses_electronics/  
It features well-written, rich articles as well as video content that explains everything more clearly than I ever could!  

# Getting Started
To download binary (executable) versions of the software, go to:  
https://github.com/espotek-org/labrador/releases

For the documentation, please visit:  
https://github.com/espotek-org/labrador/wiki 

# Raspberry Pi Build
***Please note that the 32-bit version of Raspbian version 9 (Stretch) or later is required to install this software.***

To install Labrador on the Raspberry Pi, open a terminal and paste the following command:  
`wget -O /tmp/labrador_bootstrap_pi https://raw.githubusercontent.com/espotek-org/Labrador/master/labrador_bootstrap_pi && sudo chmod +x /tmp/labrador_bootstrap_pi && sudo /tmp/labrador_bootstrap_pi`

This will automatically download, compile and install the latest version of the Labrador software from source.  The whole process will take around 20-30 minutes, so don't forget to pack a snack!  
After running it, a desktop entry will appear for the Labrador software (under Education), and running the `labrador` command from the terminal will launch the software interface.

# Additional Extras
There are community contributed 3D printable cases available at Thingiverse, courtesy of SpaceBex and Bostwickenator:
* https://www.thingiverse.com/thing:3188243
* https://www.thingiverse.com/thing:4705392

* Dave Messink has designed [a case that can be laser cut from 3mm plywood](https://github.com/espotek-org/Labrador/files/13813693/Re__Labrador_Case.1.zip)  
The binding posts and cables he used are from Amazon.  
https://www.amazon.com/dp/B07YKYP8MN?psc=1&ref=ppx_yo2ov_dt_b_product_details  
https://www.amazon.com/dp/B08KZGPTLM?ref=ppx_yo2ov_dt_b_product_details&th=1  
https://www.amazon.com/dp/B08KZGPTLM?ref=ppx_yo2ov_dt_b_product_details&th=1  
![20231217_120235](https://github.com/espotek-org/Labrador/assets/22040436/7245c645-ce89-41ae-a505-a47f29ab8875)
![20231217_120248](https://github.com/espotek-org/Labrador/assets/22040436/7ac3882c-1c8f-4fad-9f9a-03112eef8ff8)

# Building from Source
If you're looking to build from source but don't know where to start, Qt Creator is the easiest way to get your toes wet!  
https://www.qt.io/download-open-source/  
When installing, make sure you tick the box to install Qt 5.15 or later.

Once it's installed, open Desktop_Interface/Labrador.pro, then Clean All -> Run qmake -> Build All.

If you're on Linux (including Raspberry Pi), then you can also build the software from source by cloning the repo, cd'ing to the Desktop_Interface directory then running:  
```
qmake
make
sudo make install
```
Then, to launch, just type `labrador` into the terminal.

On Macos, additional steps may be required.  See issue https://github.com/espotek-org/Labrador/issues/238

To build the AVR software, I use Atmel Studio 7.  Just load up the .atsln and push F7.  You can use `avr-gcc` if you don't want to install a full IDE.

The PCB files can be edited in KiCAD 5.0 or later.

# Licence
All Dekstop software files are licenced under GNU GPL v3.  https://www.gnu.org/licenses/gpl.html

All Microcontroller software files, with the exception of those provided by Atmel, are licenced under the 3-Clause BSD License.  https://opensource.org/licenses/BSD-3-Clause

All hardware files (schematics, PCB) are licenced under Creative Commons 4.0 (CC BY-NC-SA).  https://creativecommons.org/licenses/by-nc-sa/4.0/

# Collaboration
If you want to submit a Pull Request, bug report or feature request please feel free to do so here at GitHub.  
If you just want to say hello and remind me that people are actually using my product (or if you just don't want to make a GitHub account), please email admin@espotek.com

Thanks to all.  
~Chris
