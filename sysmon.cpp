// sysmon.cpp
// Simple system monitor for Linux using /proc and ncurses.
// Features implemented (Day-wise):
// Day 1: UI layout (ncurses) and gather system data via /proc
// Day 2: Display process list with CPU and memory usage
// Day 3: Sorting by CPU, Memory, PID
// Day 4: Kill selected process (SIGTERM / SIGKILL) with confirmation
// Day 5: Real-time updates with configurable refresh interval
//
// Build:
//   g++ -std=c++17 sysmon.cpp -lncurses -o sysmon
//
// Usage:
//   ./sysmon [refresh_seconds]
//   Example: ./sysmon 2
//
// Key bindings:
//   Up/Down   : move selection
//   PageUp/Down : page scroll
//   s         : toggle sort (CPU -> MEM -> PID)
//   k         : kill selected process
//   r         : refresh immediately
//   q         : quit
//
// Note: Intended for Linux systems with /proc. Killing processes requires permissions.

#include <ncurses.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <signal.h>
#include <pwd.h>
#include <sys/stat.h>

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <iostream>
#include <cstring>

using namespace std;
using namespace std::chrono;

struct ProcTimes {
    unsigned long long utime = 0;
    unsigned long long stime = 0;
};

struct ProcInfo {
    int pid = 0;
    string user;
    string name;
    double cpu_percent = 0.0;
    size_t mem_kb = 0;
    double mem_percent = 0.0;
    ProcTimes times;
    unsigned long long total_time = 0; // utime+stime
};

enum SortMode { SORT_CPU=0, SORT_MEM=1, SORT_PID=2 };

long long get_uptime_seconds() {
    ifstream f("/proc/uptime");
    double up=0;
    if (f) f >> up;
    return (long long)up;
}

unsigned long long parse_ull(const string &s) {
    try { return stoull(s); } catch(...) { return 0ULL; }
}

bool read_meminfo(unsigned long long &mem_total_kb, unsigned long long &mem_free_kb, unsigned long long &mem_available_kb) {
    ifstream f("/proc/meminfo");
    if (!f) return false;
    string key; unsigned long long val; string unit;
    mem_total_kb = mem_free_kb = mem_available_kb = 0;
    while (f >> key >> val >> unit) {
        if (key == "MemTotal:") mem_total_kb = val;
        else if (key == "MemFree:") mem_free_kb = val;
        else if (key == "MemAvailable:") mem_available_kb = val;
    }
    return mem_total_kb>0;
}

unsigned long long total_cpu_time(const vector<unsigned long long>& vals) {
    unsigned long long sum = 0;
    for (auto v: vals) sum += v;
    return sum;
}

bool read_total_cpu(vector<unsigned long long>& fields) {
    ifstream f("/proc/stat");
    if (!f) return false;
    string line;
    getline(f, line);
    istringstream iss(line);
    string cpu;
    iss >> cpu;
    if (cpu != "cpu") return false;
    unsigned long long v;
    fields.clear();
    while (iss >> v) fields.push_back(v);
    return !fields.empty();
}

bool read_proc_times(int pid, ProcTimes &pt, unsigned long long &rss_kb, string &comm, uid_t &uid) {
    string sfn = "/proc/" + to_string(pid) + "/stat";
    ifstream f(sfn);
    if (!f) return false;
    string content;
    getline(f, content);
    size_t p1 = content.find('(');
    size_t p2 = content.rfind(')');
    if (p1==string::npos || p2==string::npos || p2<=p1) return false;
    comm = content.substr(p1+1, p2-p1-1);
    string after = content.substr(p2+2);
    istringstream iss(after);
    vector<string> toks;
    string tok;
    while (iss >> tok) toks.push_back(tok);
    if (toks.size() < 22) return false;
    unsigned long long utime = parse_ull(toks[13]);
    unsigned long long stime = parse_ull(toks[14]);
    long rss_pages = 0;
    try { rss_pages = stol(toks[21]); } catch(...) { rss_pages = 0; }
    long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
    rss_kb = (rss_pages>0) ? (rss_pages * page_size_kb) : 0;
    pt.utime = utime;
    pt.stime = stime;

    // read uid from /proc/[pid]/status
    string sfn2 = "/proc/" + to_string(pid) + "/status";
    ifstream f2(sfn2);
    uid = (uid_t)-1;
    if (f2) {
        string line;
        while (getline(f2, line)) {
            if (line.rfind("Uid:",0) == 0) {
                istringstream is(line.substr(4));
                unsigned long uid_val;
                is >> uid_val;
                uid = (uid_t)uid_val;
                break;
            }
        }
    }
    return true;
}

string username_from_uid(uid_t uid) {
    struct passwd *pw = getpwuid(uid);
    if (pw) return string(pw->pw_name);
    return to_string((unsigned)uid);
}

vector<int> list_pids() {
    vector<int> pids;
    DIR* d = opendir("/proc");
    if (!d) return pids;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            string name = entry->d_name;
            bool all_digits = !name.empty() && all_of(name.begin(), name.end(), ::isdigit);
            if (all_digits) pids.push_back(stoi(name));
        }
    }
    closedir(d);
    sort(pids.begin(), pids.end());
    return pids;
}

void collect_processes(map<int, ProcInfo>& procs, unsigned long long mem_total_kb) {
    vector<int> pids = list_pids();
    procs.clear();
    for (int pid : pids) {
        ProcInfo pi;
        pi.pid = pid;
        ProcTimes pt;
        unsigned long long rss_kb = 0;
        string comm;
        uid_t uid;
        if (!read_proc_times(pid, pt, rss_kb, comm, uid)) continue;
        pi.name = comm;
        pi.times = pt;
        pi.total_time = pt.utime + pt.stime;
        pi.mem_kb = (size_t)rss_kb;
        pi.user = username_from_uid(uid);
        if (mem_total_kb>0) {
            pi.mem_percent = (100.0 * (double)pi.mem_kb) / (double)mem_total_kb;
        } else pi.mem_percent = 0.0;
        procs[pid] = pi;
    }
}

void update_cpu_percent(const map<int, ProcInfo>& oldp, map<int, ProcInfo>& newp, unsigned long long old_total_cpu, unsigned long long new_total_cpu) {
    unsigned long long total_delta = new_total_cpu - old_total_cpu;
    if (total_delta == 0) total_delta = 1;
    for (auto &kv : newp) {
        int pid = kv.first;
        ProcInfo &npi = kv.second;
        auto it = oldp.find(pid);
        unsigned long long old_total_proc = 0;
        if (it != oldp.end()) old_total_proc = it->second.total_time;
        unsigned long long delta_proc = 0;
        if (npi.total_time >= old_total_proc) delta_proc = npi.total_time - old_total_proc;
        double pct = 100.0 * (double)delta_proc / (double)total_delta;
        npi.cpu_percent = pct;
    }
}

string human_kb(size_t kb) {
    if (kb > 1024ULL*1024ULL) {
        double gb = (double)kb / (1024.0*1024.0);
        char buf[64]; snprintf(buf, sizeof(buf), "%.2fGB", gb);
        return string(buf);
    } else if (kb > 1024) {
        double mb = (double)kb / 1024.0;
        char buf[64]; snprintf(buf, sizeof(buf), "%.1fMB", mb);
        return string(buf);
    } else {
        return to_string(kb) + "KB";
    }
}

void draw_header(WINDOW* win, unsigned long long mem_total_kb, double total_cpu_percent, int refresh_sec, SortMode sort_mode) {
    werase(win);
    int w = getmaxx(win);
    string left = " SysMon - Press 'q' to quit | 's' sort | 'k' kill | 'r' refresh ";
    mvwprintw(win, 0, 1, "%s", left.c_str());
    mvwprintw(win, 0, max(1, w - 50), "Refresh: %ds | Sort: %s", refresh_sec,
              (sort_mode==SORT_CPU?"CPU":(sort_mode==SORT_MEM?"MEM":"PID")));
    mvwprintw(win, 0, max(1, w - 100), "CPU: %.2f%%", total_cpu_percent);
    unsigned long long mem_total = mem_total_kb;
    if (mem_total) {
        unsigned long long mem_free, mem_avail;
        read_meminfo(mem_total, mem_free, mem_avail);
        unsigned long long used = mem_total - mem_avail;
        double mempct = 100.0 * (double)used / (double)mem_total;
        mvwprintw(win, 0, max(1, w - 34), "Mem: %lluMB (%.2f%%)", mem_total/1024, mempct);
    }
    box(win, 0,0);
    wrefresh(win);
}

void draw_processes(WINDOW* win, const vector<ProcInfo>& procs, int selected, int page_offset) {
    werase(win);
    int rows, cols;
    getmaxyx(win, rows, cols);
    mvwprintw(win, 0, 1, "%5s %-10s %6s %8s %8s %6s", "PID", "USER", "%CPU", "MEM(%)", "RSS", "NAME");
    for (int c=1; c<cols-1; ++c) mvwaddch(win, 1, c, ACS_HLINE);
    int maxlines = rows - 3;
    for (int i = 0; i < maxlines; ++i) {
        int idx = page_offset + i;
        if (idx >= (int)procs.size()) break;
        const ProcInfo &p = procs[idx];
        int y = i + 2;
        if (idx == selected) {
            wattron(win, A_REVERSE);
        }
        mvwprintw(win, y, 1, "%5d %-10.10s %6.2f %8.2f %8s %6.30s", p.pid, p.user.c_str(), p.cpu_percent, p.mem_percent, human_kb(p.mem_kb).c_str(), p.name.c_str());
        if (idx == selected) {
            wattroff(win, A_REVERSE);
        }
    }
    box(win, 0,0);
    wrefresh(win);
}

void sort_processes(vector<ProcInfo>& vec, SortMode mode) {
    if (mode == SORT_CPU) {
        sort(vec.begin(), vec.end(), [](const ProcInfo &a, const ProcInfo &b) {
            if (a.cpu_percent == b.cpu_percent) return a.pid < b.pid;
            return a.cpu_percent > b.cpu_percent;
        });
    } else if (mode == SORT_MEM) {
        sort(vec.begin(), vec.end(), [](const ProcInfo &a, const ProcInfo &b) {
            if (a.mem_percent == b.mem_percent) return a.pid < b.pid;
            return a.mem_percent > b.mem_percent;
        });
    } else {
        sort(vec.begin(), vec.end(), [](const ProcInfo &a, const ProcInfo &b) {
            return a.pid < b.pid;
        });
    }
}

bool confirm_kill(WINDOW* win, int pid) {
    int rows, cols; getmaxyx(win, rows, cols);
    string msg = "Send SIGTERM or SIGKILL to PID " + to_string(pid) + "? (t=TERM / k=KILL / c=cancel)";
    WINDOW* dlg = newwin(5, min((int)msg.size()+4, cols-4), (rows-5)/2, (cols - (int)msg.size()-4)/2);
    box(dlg, 0,0);
    mvwprintw(dlg, 2, 2, "%s", msg.c_str());
    wrefresh(dlg);
    nodelay(stdscr, FALSE);
    int ch = wgetch(dlg);
    nodelay(stdscr, TRUE);
    bool do_kill = false;
    int sig = 0;
    if (ch == 't' || ch == 'T') { do_kill = true; sig = SIGTERM; }
    else if (ch == 'k' || ch == 'K') { do_kill = true; sig = SIGKILL; }
    delwin(dlg);
    if (do_kill) {
        if (kill(pid, sig) == 0) {
            // success dialog
            WINDOW* ok = newwin(3, 40, (rows-3)/2, (cols-40)/2);
            box(ok,0,0);
            mvwprintw(ok,1,2, "Signal %d sent to PID %d", sig, pid);
            wrefresh(ok);
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            delwin(ok);
            return true;
        } else {
            WINDOW* err = newwin(5, 60, (rows-5)/2, max(1,(cols-60)/2));
            box(err,0,0);
            mvwprintw(err,1,2, "Failed to send signal %d to PID %d: %s", sig, pid, strerror(errno));
            mvwprintw(err,3,2, "Press any key...");
            wrefresh(err);
            nodelay(stdscr, FALSE);
            wgetch(err);
            nodelay(stdscr, TRUE);
            delwin(err);
            return false;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    int refresh_sec = 2;
    if (argc >= 2) {
        try { refresh_sec = stoi(argv[1]); if (refresh_sec < 1) refresh_sec = 1; } catch(...) { refresh_sec = 2; }
    }

    // initialize ncurses
    initscr();
    noecho();
    cbreak();
    curs_set(0);
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // non-blocking input
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    // create windows: header (3 lines) and body (rest)
    int header_h = 3;
    WINDOW* header = newwin(header_h, cols, 0, 0);
    WINDOW* body = newwin(rows - header_h, cols, header_h, 0);

    SortMode sort_mode = SORT_CPU;
    int selected = 0;
    int page_offset = 0;

    map<int, ProcInfo> old_procs;
    map<int, ProcInfo> cur_procs;

    vector<unsigned long long> old_cpu_fields, cur_cpu_fields;
    read_total_cpu(old_cpu_fields);
    unsigned long long old_total_cpu = total_cpu_time(old_cpu_fields);

    unsigned long long mem_total_kb = 0, mem_free_kb=0, mem_available_kb=0;
    read_meminfo(mem_total_kb, mem_free_kb, mem_available_kb);

    double total_cpu_percent = 0.0;

    bool running = true;
    auto last_refresh = steady_clock::now() - seconds(refresh_sec);

    while (running) {
        // handle input
        int ch = getch();
        if (ch != ERR) {
            if (ch == 'q' || ch == 'Q') { running = false; break; }
            else if (ch == KEY_UP) { if (selected > 0) selected--; if (selected < page_offset) page_offset = selected; }
            else if (ch == KEY_DOWN) { selected++; }
            else if (ch == KEY_NPAGE) { // page down
                int body_rows = getmaxy(body) - 3;
                selected += max(1, body_rows);
            }
            else if (ch == KEY_PPAGE) { int body_rows = getmaxy(body) - 3; selected -= max(1, body_rows); if (selected < 0) selected = 0; }
            else if (ch == 's' || ch == 'S') {
                if (sort_mode == SORT_CPU) sort_mode = SORT_MEM;
                else if (sort_mode == SORT_MEM) sort_mode = SORT_PID;
                else sort_mode = SORT_CPU;
            }
            else if (ch == 'r' || ch == 'R') {
                last_refresh = steady_clock::now() - seconds(refresh_sec); // force immediate refresh in next loop
            }
            else if (ch == 'k' || ch == 'K') {
                // perform kill on selected process if valid
                // find selected pid from current vector
                // We'll build a vector view below; so set a flag to trigger kill after refresh to avoid race
                // For simplicity, attempt kill immediately if we have cur_procs
                vector<ProcInfo> pv;
                for (auto &kv : cur_procs) pv.push_back(kv.second);
                sort_processes(pv, sort_mode);
                if (selected >= 0 && selected < (int)pv.size()) {
                    int pid = pv[selected].pid;
                    confirm_kill(stdscr, pid);
                    // after kill, force refresh
                    last_refresh = steady_clock::now() - seconds(refresh_sec);
                }
            }
        }

        // refresh periodically
        auto now = steady_clock::now();
        if (now - last_refresh >= seconds(refresh_sec)) {
            // read CPU totals
            read_total_cpu(cur_cpu_fields);
            unsigned long long cur_total_cpu = total_cpu_time(cur_cpu_fields);

            // read meminfo
            read_meminfo(mem_total_kb, mem_free_kb, mem_available_kb);

            // collect processes
            collect_processes(cur_procs, mem_total_kb);

            // compute per-process cpu percent
            update_cpu_percent(old_procs, cur_procs, old_total_cpu, cur_total_cpu);

            // compute approximate total_cpu_percent as (1 - idle_delta/total_delta)*100
            // idle is field 3 (idle) + 4 (iowait) in /proc/stat fields, but we used total sum - approximate:
            unsigned long long old_idle = 0, cur_idle = 0;
            if (old_cpu_fields.size() >= 4) old_idle = old_cpu_fields[3] + (old_cpu_fields.size() > 4 ? old_cpu_fields[4] : 0);
            if (cur_cpu_fields.size() >= 4) cur_idle = cur_cpu_fields[3] + (cur_cpu_fields.size() > 4 ? cur_cpu_fields[4] : 0);
            unsigned long long idle_delta = (cur_idle - old_idle);
            unsigned long long total_delta = (cur_total_cpu - old_total_cpu);
            if (total_delta == 0) total_delta = 1;
            total_cpu_percent = 100.0 * (1.0 - ((double)idle_delta / (double)total_delta));

            // prepare vector view
            vector<ProcInfo> pv;
            pv.reserve(cur_procs.size());
            for (auto &kv : cur_procs) pv.push_back(kv.second);

            // compute mem_percent updated (in case mem_total changed)
            for (auto &p : pv) {
                if (mem_total_kb > 0) p.mem_percent = 100.0 * (double)p.mem_kb / (double)mem_total_kb;
                else p.mem_percent = 0.0;
            }

            // sort
            sort_processes(pv, sort_mode);

            // adjust selected bounds
            if (selected >= (int)pv.size()) selected = max(0, (int)pv.size()-1);
            if (selected < 0) selected = 0;

            // adjust page offset to keep selected visible
            int body_rows = getmaxy(body) - 3;
            if (body_rows < 1) body_rows = 1;
            if (selected < page_offset) page_offset = selected;
            else if (selected >= page_offset + body_rows) page_offset = selected - body_rows + 1;

            // draw header & processes
            draw_header(header, mem_total_kb, total_cpu_percent, refresh_sec, sort_mode);
            draw_processes(body, pv, selected, page_offset);

            // swap old<->cur
            old_procs = cur_procs;
            old_cpu_fields = cur_cpu_fields;
            old_total_cpu = total_cpu_time(old_cpu_fields);

            last_refresh = now;
        }

        // small sleep to avoid busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // cleanup
    delwin(header);
    delwin(body);
    endwin();
    return 0;
}
