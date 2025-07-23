Excellent! Congratulations on getting the project built successfully. That's a huge milestone.

Now for the exciting part: running it on the hardware. Here are the step-by-step instructions to get your KCU105 board synchronized.

Step 1: Set Up the Hardware and Network
Connect KCU105 to Network: Plug an SFP Ethernet cable into one of the SFP ports on your KCU105 and connect the other end to a network switch.

Connect PC to Network: Connect your Linux PC to the same network switch. It's crucial they are on the same network segment.

Connect for Serial Output: Connect the micro-USB cable from the "UART" port on the KCU105 to your computer. This is how you will see the xil_printf output.

Step 2: Set Up a PTP Master on Your Linux PC
Your KCU105 is a PTP slave; it needs a master clock to sync to. The easiest way to create one is to use your Linux PC.

Install linuxptp: If you haven't already, open a terminal on your Linux PC and run:

Bash

sudo apt-get update
sudo apt-get install linuxptp
Configure PC's IP Address: Your KCU105 has the static IP 192.168.1.10. Make sure your Linux PC's wired network connection is on the same subnet. For example, set its IP to 192.168.1.100 and netmask to 255.255.255.0.

Find Network Interface Name: In the terminal, find the name of your wired interface (e.g., eth0, enp4s0).

Bash

ip addr
Start the PTP Master: Run the following command, replacing <interface_name> with your actual interface name. The -m flag tells it to run as a master.

Bash

sudo ptp4l -i <interface_name> -m
The terminal will start printing status messages. Leave this running. Your PC is now the grandmaster clock.

Step 3: Program and Run on the KCU105
Open a Serial Terminal:

On your PC, open a terminal application like PuTTY, Tera Term, or use the built-in Vitis Serial Terminal.

Find the COM port for your KCU105 (check Device Manager on Windows or /dev/ttyUSB* on Linux).

Connect with these settings: Baud rate: 115200, Data bits: 8, Parity: None, Stop bits: 1.

Launch the Application from Vitis/SDK:

In Vitis/SDK, right-click on your application project in the Explorer panel.

Select Run As -> Launch on Hardware (Single Application Debug).

This will program the FPGA and load your .elf file onto the MicroBlaze processor. The application will start running immediately.

Step 4: Observe the Output
Switch to your serial terminal window. You should see the output from your KCU105 board.

Initialization: You'll see the startup messages we defined:

----- PTP + lwIP Server (Bare-Metal) -----
PTPd: Initializing data sets...
PTPd: Initializing clock servo
...etc...
State Transitions: The PTP state machine will start.

PTPd: State change from 0 to 3  (INITIALIZING to LISTENING)
Synchronization (The Goal!): After a few seconds, the board will receive the Announce messages from your PC master, run the BMC algorithm, and decide to become a slave. You should see:

PTPd: Synchronizing to new MASTER ...
PTPd: State change from 3 to 9  (LISTENING to SLAVE)
Servo Output: Once in the SLAVE state, the servo will start working. You will see the periodic debug prints from servo_update_clock:

PTPd: offset: -2345 ns, delay: 876 ns, drift: -293, adj: 879
PTPd: offset: -1567 ns, delay: 878 ns, drift: -488, adj: 295
...
If you see the state change to SLAVE and the offset values start converging toward zero, your implementation is working!

What if I am using a windows pc




Show thinking
That's a great point. You are correct, linuxptp is specific to Linux. For a Windows PC, you'll need a different piece of software to act as the PTP master.

The most common and reliable free tool for this is a Windows port of the PTPd daemon. Here’s how to set it up. The rest of the steps (programming the board, watching the serial terminal) will be exactly the same.

Step 2 (Windows Version): Run a PTP Master on Your Windows PC
Download PTPd for Windows:

Go to the PTPd project's release page: https://github.com/ptpd/ptpd/releases

Download the latest ptpd-x.x.x-win32.zip file (e.g., ptpd-2.3.2-win32.zip).

Extract the ZIP file to a simple location, like C:\ptpd.

Find Your PC's IP Address:

Open the Windows Command Prompt (cmd.exe).

Type ipconfig and press Enter.

Look for your "Ethernet adapter" and find its IPv4 Address (e.g., 192.168.1.100). You will need this.

Start the PTP Master:

In the same Command Prompt window, navigate to the directory where you extracted PTPd.

DOS

cd C:\ptpd
Run the following command, replacing <your_pc_ip> with the IP address you found in the previous step. The -M flag forces it into master-only mode.

DOS

ptpd.exe -M -b <your_pc_ip>
For example:

DOS

ptpd.exe -M -b 192.168.1.100
A status window for PTPd will appear, and it will start sending PTP master messages. Leave this window open.

Your Windows PC is now acting as the PTP grandmaster clock. You can now proceed with Step 3 (Program and Run on the KCU105) and Step 4 (Observe the Output) exactly as described before.
