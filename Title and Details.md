
SysMon - Simple Linux System Monitor (C++, ncurses)
==================================================


Lagnajeet Parida-2241016197_BATCH-8

TASKS GIVEN:

A single-file C++ system monitor for Linux that reads data from /proc and displays a terminal UI using ncurses. Implements your five-day plan:

Day 1: UI layout and system data via /proc
Day 2: Process list with CPU and memory
Day 3: Sorting by CPU / Memory / PID
Day 4: Kill processes (SIGTERM / SIGKILL) from UI
Day 5: Real-time refresh (configurable interval)
How I Built It:

Installed Ubantu and compiled all the codes in the command propmt Installed ncurses on Ubuntu

Controls Added:

1- Up / Down arrows : move selection

2- PageUp / PageDown : page scroll

3- s : toggle sort (CPU -> MEM -> PID)

4- k : kill selected process (choose t=SIGTERM or k=SIGKILL)

5- r : refresh immediately

6- q : quit

