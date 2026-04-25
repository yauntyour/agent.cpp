# -*- coding: utf-8 -*-
"""
System Status Report
----------------------
Prints a comprehensive system report.
Optional dependency: psutil (for detailed metrics).
If psutil is not installed, only basic information from the standard library is shown.
"""

import os
import platform
import datetime
import socket
import sys


def _try_import_psutil():
    """Attempt to import psutil; return module or None."""
    try:
        import psutil

        return psutil
    except ImportError:
        return None


def print_basic_info():
    """Print basic system information using standard library."""
    print("\n[1. Basic System Information]")
    print(
        f"Operating System: {platform.system()} {platform.release()} ({platform.version()})"
    )
    print(f"Architecture: {platform.machine()}")
    print(f"Processor: {platform.processor()}")
    print(f"Python Version: {platform.python_version()}")
    print(f"Hostname: {socket.gethostname()}")

    # Boot time is not available without psutil; we skip it.
    print("(Install psutil for boot time / uptime details)")


def print_cpu_info(psutil_module):
    """Print CPU info if psutil is available, otherwise just logical count."""
    print("\n[2. CPU Status]")
    if psutil_module is None:
        print(f"Logical CPUs: {os.cpu_count()}")
        print("(Install psutil for CPU usage, frequency and per-core details)")
        return

    cpu_count_logical = psutil_module.cpu_count(logical=True)
    cpu_count_physical = psutil_module.cpu_count(logical=False)
    cpu_freq = psutil_module.cpu_freq()
    # Use a one-second wait to get accurate overall usage
    cpu_percent = psutil_module.cpu_percent(interval=1)

    print(f"Physical Cores: {cpu_count_physical}")
    print(f"Logical Cores: {cpu_count_logical}")
    if cpu_freq:
        print(
            f"Current Frequency: {cpu_freq.current:.2f} MHz (Max: {cpu_freq.max:.2f} MHz)"
        )
    print(f"Total Usage: {cpu_percent}%")

    print("Usage per Core:")
    for i, per_cpu in enumerate(psutil_module.cpu_percent(percpu=True, interval=0.5)):
        print(f"  Core {i}: {per_cpu}%")


def print_memory_info(psutil_module):
    """Print memory and swap info if psutil is available."""
    print("\n[3. Memory Status]")
    if psutil_module is None:
        print("(Install psutil for memory details)")
        return

    mem = psutil_module.virtual_memory()
    print(f"Total Memory: {mem.total / (1024**3):.2f} GB")
    print(f"Used: {mem.used / (1024**3):.2f} GB ({mem.percent}%)")
    print(f"Available: {mem.available / (1024**3):.2f} GB")

    swap = psutil_module.swap_memory()
    print(f"Swap Total: {swap.total / (1024**3):.2f} GB")
    print(f"Swap Usage: {swap.percent}%")


def print_disk_info(psutil_module):
    """Print disk partitions and usage with robust path filtering."""
    print("\n[4. Disk Status]")
    if psutil_module is None:
        print("(Install psutil for disk details)")
        return

    try:
        partitions = psutil_module.disk_partitions()
    except Exception as e:
        print(f"  [Critical] Error listing partitions: {e}")
        return

    disk_count = 0
    for part in partitions:
        mountpoint = part.mountpoint

        # Filter out empty or bad mountpoints
        if not mountpoint:
            continue
        # Detect null bytes or control characters (common in WSL or virtual drives)
        if "\x00" in mountpoint or any(
            ord(c) < 32 and c not in "\n\r\t" for c in mountpoint
        ):
            print(f"  [Skipped] Illegal characters in mountpoint: {repr(mountpoint)}")
            continue

        try:
            clean_path = os.path.abspath(mountpoint)
        except Exception:
            print(f"  [Skipped] Failed to normalise path: {mountpoint}")
            continue

        try:
            usage = psutil_module.disk_usage(clean_path)
            disk_count += 1

            device = getattr(part, "device", "N/A")
            fstype = getattr(part, "fstype", "N/A")

            # Ensure safe encoding for printing
            safe_device = str(device).encode("utf-8", errors="replace").decode("utf-8")
            safe_mount = (
                str(clean_path).encode("utf-8", errors="replace").decode("utf-8")
            )
            safe_fstype = str(fstype).encode("utf-8", errors="replace").decode("utf-8")

            print(f"Partition: {safe_device} (Mount Point: {safe_mount})")
            print(f"  Filesystem: {safe_fstype}")
            print(f"  Total Space: {usage.total / (1024**3):.2f} GB")
            print(f"  Used: {usage.used / (1024**3):.2f} GB ({usage.percent}%)")
            print(f"  Free: {usage.free / (1024**3):.2f} GB")
            print("-" * 40)

        except PermissionError:
            print(f"  [Permission Denied] {clean_path}")
        except FileNotFoundError:
            print(f"  [Not Found] {clean_path}")
        except OSError as e:
            print(f"  [OS Error] {clean_path}: {e}")
        except Exception as e:
            print(f"  [Unknown Error] {clean_path}: {type(e).__name__} - {e}")

    if disk_count == 0:
        print("  No valid disk partition information read successfully.")


def print_network_info(psutil_module):
    """Print network I/O and main IP address."""
    print("\n[5. Network Status]")
    if psutil_module is None:
        # Basic IP retrieval without psutil (limited but possible)
        try:
            hostname = socket.gethostname()
            main_ip = socket.gethostbyname(hostname)
            print(f"Main IP (by hostname): {main_ip}")
        except Exception:
            print("Main IP: Could not determine")
        print("(Install psutil for detailed network I/O and interface info)")
        return

    net_io = psutil_module.net_io_counters()
    print(f"Bytes Sent: {net_io.bytes_sent / (1024**2):.2f} MB")
    print(f"Bytes Received: {net_io.bytes_recv / (1024**2):.2f} MB")

    # Find main non-loopback IPv4
    ip_address = "No valid IP found"
    addrs = psutil_module.net_if_addrs()
    for interface_name, interface_addresses in addrs.items():
        for addr in interface_addresses:
            if addr.family == socket.AF_INET and not addr.address.startswith("127."):
                ip_address = addr.address
                break
        if ip_address != "No valid IP found":
            break
    print(f"Main IP Address: {ip_address}")


def print_battery_info(psutil_module):
    """Print battery status if available (laptops)."""
    if psutil_module is None:
        return
    try:
        battery = psutil_module.sensors_battery()
        if battery:
            print("\n[6. Battery Status]")
            print(f"Remaining Power: {battery.percent}%")
            print(f"Charging: {'Yes' if battery.power_plugged else 'No'}")
            if battery.secsleft not in (
                psutil_module.POWER_TIME_UNLIMITED,
                psutil_module.POWER_TIME_UNKNOWN,
            ):
                mins = battery.secsleft // 60
                print(f"Estimated Time Remaining: {mins} minutes")
    except Exception:
        pass


def print_top_processes(psutil_module):
    """Show top 5 processes by memory and CPU, if psutil is available."""
    print("\n[7. Top 5 Processes]")
    if psutil_module is None:
        print("(Install psutil for process information)")
        return

    # CPU snapshot (we already waited 1 sec earlier; reuse that baseline)
    try:
        # Give time for process cpu_percent to accumulate by calling cpu_percent() once first
        psutil_module.cpu_percent(interval=0.5)  # dummy call to refresh
    except Exception:
        pass

    # Collect processes
    proc_list = []
    for proc in psutil_module.process_iter(
        ["pid", "name", "memory_percent", "cpu_percent"]
    ):
        try:
            info = proc.info
            if info["memory_percent"] is not None and info["cpu_percent"] is not None:
                proc_list.append(info)
        except (psutil_module.NoSuchProcess, psutil_module.AccessDenied):
            continue

    # Top memory
    top_mem = sorted(proc_list, key=lambda x: x["memory_percent"] or 0, reverse=True)[
        :5
    ]
    print("\nTop 5 Processes by Memory Usage:")
    for i, p in enumerate(top_mem):
        print(
            f"  {i+1}. {p['name']:<25} (PID: {p['pid']}) -> {p['memory_percent']:.1f}% memory"
        )

    # Top CPU
    top_cpu = sorted(proc_list, key=lambda x: x["cpu_percent"] or 0, reverse=True)[:5]
    print("\nTop 5 Processes by CPU Usage:")
    for i, p in enumerate(top_cpu):
        print(
            f"  {i+1}. {p['name']:<25} (PID: {p['pid']}) -> {p['cpu_percent']:.1f}% CPU"
        )

    if not top_mem and not top_cpu:
        print("  No process information could be collected.")


def get_system_status():
    """Orchestrator: print full system status report."""
    psutil_module = _try_import_psutil()

    print("* " * 20)
    print("System Comprehensive Status Report")
    print("=" * 60)

    if psutil_module is None:
        print("[Notice] psutil not installed – limited information shown.")
        print("         To see full details, run: pip install psutil\n")

    print_basic_info()
    if psutil_module:
        boot_time = datetime.datetime.fromtimestamp(psutil_module.boot_time())
        print(f"System Boot Time: {boot_time.strftime('%Y-%m-%d %H:%M:%S')}")
        uptime = datetime.datetime.now() - boot_time
        print(f"Uptime: {uptime}")

    print_cpu_info(psutil_module)
    print_memory_info(psutil_module)
    print_disk_info(psutil_module)
    print_network_info(psutil_module)
    print_battery_info(psutil_module)
    print_top_processes(psutil_module)

    print("\n" + "=" * 60)
    print("Status Detection Complete")
    print("=" * 60)


if __name__ == "__main__":
    try:
        get_system_status()
    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    except Exception as e:
        print(f"\nAn unexpected error occurred: {e}", file=sys.stderr)
        sys.exit(1)
