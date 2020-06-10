Intro
==

A library and a daemon for reading external Oregon wireless sensors (RF protocol v2.1) on Raspberry Pi via cc1101-based 433MHz radio.
Tested with a THN122N and an RPI v1. 

**NOTE**: For the moment only THN122N/THN132N sensors are decoded, since I don't have access to other sensors to test with. 


SW requirements
==
* wiringPi library

HW requirements and connection
== 
Apart from a Raspberry Pi (obviously) you need a cc1101-based 433MHz radio module. One can be bought quite cheaply on Aliexpress

Check cc1101_oregon.h for Pin description. Keep in mind the difference between wiringPi GPIO numbers, Broadcom GPIO numbers, and 
physical pin numbers on the P1 connector.

	CC1101 Vdd = 3.3V  
	CC1101 max. digital voltage level = 3.3V (not 5V tolerant)
	
	CC1101<->Raspi
	
	Vdd    -    3.3V (P1-17)  
	SI     -    MOSI (P1-19)  
	SO     -    MISO (P1-21)  
	CSn    -    CE0  (P1-24)  
	SCLK   -    SCLK (P1-23)  
	GDO2   -    GPIO25 (P1-22)  
	GDO0   -    not used here  
	GND    -    GND  (P1-25)  

Description
==

Practically all sources I found on the web that read Oregon wireless sensors, don't make use of the extended processing capabilities 
of cc1101. They use it in RAW mode instead, moving all decoding load to the main CPU, which is not optimal. 
This prompted me to create a solution that uses the packet-handling and data-buffering hardware support in cc1101.

The utility can be run as a daemon and in user mode. The daemon listens to transmissions from Oregon sensors and collects data/statistics.
In user mode collected data can be read from the daemon. User mode can also be used to test proper interaction between the components of the 
system (RPi communication with the cc1101, successful Rx of Oregon signal, etc.).
 


Installation and usage
==

To build the software, use:

	make

Then, do a test to see if signal from the Oregon sensor is being received OK, by using `oregon_read -t`. You can add a `-d` option 
as well (with which also a specific debug level can be specified) for additional details/verbosity.
If everything is fine, you should get a result like:

	$ sudo ./build/oregon_read -t
	Test mode
	Mode: ASK/OOK Oregon-specific (no HW manchester)
	Frequency: 433.92 MHz
	RF Channel: 0
	
	Rx @ 33.912 s:
	Oregon pkt (bad/all) # 0 / 1
	RSSI min [dBm]: -69  LQI max: 54
	sensor ID: 0xEC40
	sensor chan: 1
	roll code: 0x87
	batt_low: 0
	cksum_ok: 1
	temperature [degC]: 18.3
	
	Rx @ 72.908 s:
	Oregon pkt (bad/all) # 0 / 2
	RSSI min [dBm]: -70  LQI max: 55
	sensor ID: 0xEC40
	sensor chan: 1
	roll code: 0x87
	batt_low: 0
	cksum_ok: 1
	temperature [degC]: 18.3
	
	^C
	=== Oregon Rx statistics ===
	Bad/Total received Oregon packets:          0 / 2
	Errors: brst1/brst2/pktlen/buffmatch:       0 / 0 / 0 / 0
	Min/Max time between good packets [s]:      39 / 39
	Min/Average/Max RSSI (good packets) [dBm]: -70 / -69 / -69
	Max/Average/Min LQI (good packets):         55 / 49 / 43


Finally, when you have confirmed that reception and decoding are stable, you can install the daemon with:

	sudo make install
	
Start the daemon with

	sudo service oregon_cc1101 start 	

You can verify the daemon has started and show its state and some Rx statistics with:

	/opt/vc/bin/oregon_read -V
	
Getting the latest sensor info received by the daemon is done with:
	
	/opt/vc/bin/oregon_read -o
	
or, if you need the temperature in bare format, with: 		

	/opt/vc/bin/oregon_read -b
	
Run `oregon_read -h` to see other options.

Acknowledgements
==

I used these sources of information:
* [Oregon Scientific RF Protocols info](http://www.osengr.org/WxShield/Downloads/OregonScientific-RF-Protocols.pdf)
* <http://ygorshko.blogspot.sk/2015/04/finally-set-up-c1101-to-receive-oregon.html>
* [CC1101 datasheet](https://www.ti.com/lit/gpn/cc1101)
* [DN022 -- CC110x CC111x OOK ASK Register Settings](https://www.ti.com/lit/pdf/swra215)

The software uses some code from:
* <https://github.com/SpaceTeddy/CC1101>

Ideas about some register settings came from <https://gist.github.com/gabonator/5867aa2eaa8448dda719a4d3a2181f48>

I also used [SmartRF studio](https://www.ti.com/tool/smartrftm-studio) to tune up the register settings.



------
* Created on: 27 Apr. 2020
*     Author: Ivaylo Haratcherev
*    Version: 1.3
