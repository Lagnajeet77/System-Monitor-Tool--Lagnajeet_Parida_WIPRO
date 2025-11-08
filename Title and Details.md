
SysMon - Simple Linux System Monitor (C++, ncurses)
==================================================

What it is
----------
A single-file C++ system monitor for Linux that reads data from /proc and displays a terminal UI using ncurses.
Implements your five-day plan:
- Day 1: UI layout and system data via /proc
- Day 2: Process list with CPU and memory
- Day 3: Sorting by CPU / Memory / PID
- Day 4: Kill processes (SIGTERM / SIGKILL) from UI
- Day 5: Real-time refresh (configurable interval)

Files
-----
- sysmon.cpp     : Main program (C++17, ncurses)
- Makefile       : simple build helper
- README.md      : this file
- LICENSE        : MIT license

Build
-----
You need a Linux system with g++ and ncurses.
Install ncurses on Debian/Ubuntu:
  sudo apt-get install libncurses5-dev libncursesw5-dev

Build:
  make

Or:
  g++ -std=c++17 sysmon.cpp -lncurses -o sysmon

Usage
-----
  ./sysmon [refresh_seconds]

Examples:
  ./sysmon
  ./sysmon 1

Controls
--------
- Up / Down arrows : move selection
- PageUp / PageDown : page scroll
- s : toggle sort (CPU -> MEM -> PID)
- k : kill selected process (choose t=SIGTERM or k=SIGKILL)
- r : refresh immediately
- q : quit

Notes and safety
----------------
- This tool parses /proc (Linux only).
- Killing processes requires appropriate permissions. Do not run as root unless you understand the risks.
- The CPU percentage calculation is approximate (based on /proc stat deltas).
