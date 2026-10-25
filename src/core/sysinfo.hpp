#pragma once
#include "core/common.hpp"

inline std::string get_system_status()
{
    std::ostringstream oss;
    oss.precision(1);
    oss << std::fixed;

    // ----- CPU 使用率 -----
    {
        // 静态变量保存上一次 CPU 时间
        static bool cpuInitialized = false;
        static unsigned long long prevTotal = 0, prevIdle = 0;
        unsigned long long totalTime = 0, idleTime = 0;
        bool valid = false;

#ifdef _WIN32
        FILETIME idleFt, kernelFt, userFt;
        if (GetSystemTimes(&idleFt, &kernelFt, &userFt))
        {
            auto to_ull = [](const FILETIME &ft) -> unsigned long long
            {
                return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
            };
            auto idle = to_ull(idleFt);
            auto kernel = to_ull(kernelFt);
            auto user = to_ull(userFt);
            // kernel 时间中已包含 idle
            totalTime = kernel + user;
            idleTime = idle;
            valid = true;
        }

#elif __APPLE__
        processor_info_array_t infoArray;
        mach_msg_type_number_t infoCount;
        natural_t cpuCount;
        if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                                &cpuCount, &infoArray, &infoCount) == KERN_SUCCESS)
        {
            long long totalUser = 0, totalSystem = 0, totalIdle = 0, totalNice = 0;
            for (natural_t i = 0; i < cpuCount; ++i)
            {
                auto base = i * CPU_STATE_MAX;
                totalUser += infoArray[base + CPU_STATE_USER];
                totalSystem += infoArray[base + CPU_STATE_SYSTEM];
                totalIdle += infoArray[base + CPU_STATE_IDLE];
                totalNice += infoArray[base + CPU_STATE_NICE];
            }
            vm_deallocate(mach_task_self(), (vm_address_t)infoArray,
                          infoCount * sizeof(*infoArray));
            totalTime = totalUser + totalSystem + totalIdle + totalNice;
            idleTime = totalIdle;
            valid = true;
        }

#elif __linux__
        std::ifstream statFile("/proc/stat");
        if (statFile.is_open())
        {
            std::string line;
            if (std::getline(statFile, line))
            {
                // 期望格式: cpu  user nice system idle iowait irq softirq ...
                char cpu[8];
                unsigned long long user, nice, sys, idle, iowait, irq, softirq;
                int matched = sscanf(line.c_str(), "%7s %llu %llu %llu %llu %llu %llu %llu",
                                     cpu, &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
                if (matched >= 8 && strcmp(cpu, "cpu") == 0)
                {
                    totalTime = user + nice + sys + idle + iowait + irq + softirq;
                    idleTime = idle + iowait; // iowait 也可视为空闲
                    valid = true;
                }
            }
        }
#endif

        if (valid)
        {
            if (cpuInitialized)
            {
                unsigned long long deltaTotal = totalTime - prevTotal;
                unsigned long long deltaIdle = idleTime - prevIdle;
                if (deltaTotal > 0)
                {
                    double usage = (1.0 - (double)deltaIdle / deltaTotal) * 100.0;
                    oss << "CPU Usage: " << usage << "%\n";
                }
                else
                {
                    oss << "CPU Usage: N/A\n";
                }
            }
            else
            {
                oss << "CPU Usage: N/A (first call)\n";
            }
            prevTotal = totalTime;
            prevIdle = idleTime;
            cpuInitialized = true;
        }
        else
        {
            oss << "CPU Usage: unavailable\n";
        }
    }

    // ----- 内存信息 -----
    {
        unsigned long long totalMem = 0, availMem = 0;

#ifdef _WIN32
        MEMORYSTATUSEX memStatus;
        memStatus.dwLength = sizeof(memStatus);
        if (GlobalMemoryStatusEx(&memStatus))
        {
            totalMem = memStatus.ullTotalPhys;
            availMem = memStatus.ullAvailPhys;
        }

#elif __APPLE__
        // 总内存
        int64_t memsize = 0;
        size_t size = sizeof(memsize);
        sysctlbyname("hw.memsize", &memsize, &size, NULL, 0);
        totalMem = memsize;

        // 可用内存: free + inactive 页
        vm_statistics64_data_t vmStat;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info64_t)&vmStat, &count) == KERN_SUCCESS)
        {
            long pageSize = sysconf(_SC_PAGESIZE);
            availMem = (vmStat.free_count + vmStat.inactive_count) * pageSize;
        }

#elif __linux__
        std::ifstream meminfo("/proc/meminfo");
        if (meminfo.is_open())
        {
            std::string line;
            while (std::getline(meminfo, line))
            {
                if (line.rfind("MemTotal:", 0) == 0)
                {
                    sscanf(line.c_str(), "MemTotal: %llu kB", &totalMem);
                    totalMem *= 1024;
                }
                else if (line.rfind("MemAvailable:", 0) == 0)
                {
                    sscanf(line.c_str(), "MemAvailable: %llu kB", &availMem);
                    availMem *= 1024;
                }
            }
        }
#endif

        if (totalMem > 0)
        {
            unsigned long long usedMem = totalMem - availMem;
            double usedPercent = (double)usedMem / totalMem * 100.0;

            auto toMB = [](unsigned long long bytes) -> double
            {
                return bytes / (1024.0 * 1024.0);
            };

            oss << "Memory: " << toMB(usedMem) << " MB / " << toMB(totalMem)
                << " MB (" << usedPercent << "%)";
        }
        else
        {
            oss << "Memory: unavailable";
        }
    }

    return oss.str();
}
