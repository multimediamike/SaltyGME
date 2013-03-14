# Introduction
SaltyGME is a chiptune player for Google Chrome's Native Client (NaCl).

SaltyGME started as a Native Client frontend for the
[Game Music Emu library](http://www.slack.net/~ant/libs/audio.html). The 
player was then expanded to include multiple multiple chiptune
player backends.

The NaCl player is [available as a Chrome browser extension](https://chrome.google.com/webstore/detail/leooadmebmmjogbfhdcbfldndllfkhpg) in the Chrome Web Store.
It works under Windows, Mac OS X, and Linux. [Visit the Game Music Appreciation](http://gamemusic.multimedia.cx/)
website for a searchable database of video game music.

# Development
Set up the Native Client development environment per the instructions at
[Google's website for NaCl development](https://developers.google.com/native-client/).
Presently, the player supports version 25 of the Pepper API.

After installing the NaCl development environment, build the player with:

NACL_SDK_ROOT=/path/to/nacl_sdk/pepper_25 make

There is also a testbench command line program that uses the same infrastructure
as the NaCl module, but is easier to debug between Linux, printf and gdb. The
program currently only runs on Linux because it outputs audio using Linux's
PulseAudio API. To compile this program:

make -f Makefile.linux-pulse

This will create the executable 'pulse-testbench'.

# Credits
Mike Melanson (mike -at- multimedia.cx) wrote the SaltyGME player.

Shay Green (gblargg -at- gmail.com) authored [Game Music Emu](http://www.slack.net/~ant/libs/audio.html).

[Audio Overload SDK](http://rbelmont.mameworld.info/?page_id=221) by R. Belmont and Richard Bannister.

[Vio2sf](http://www.zophar.net/utilities/2sf/vio2sf.html) written by ???

[XZ Embedded](http://tukaani.org/xz/embedded.html) by Lasse Collin
