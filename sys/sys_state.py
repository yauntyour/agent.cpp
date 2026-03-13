# -*- coding: utf-8 -*-
import os
import psutil
import platform
import datetime
import socket
from collections import namedtuple

def get_system_status():
    print("* " * 20)
    print("System Comprehensive Status Report")
    print("=" * 60)

    # 1. Basic System Information
    print("\n[1. Basic System Information]")
    print(
        f"Operating System: {platform.system()} {platform.release()} ({platform.version()})"
    )
    print(f"Architecture: {platform.machine()}")
    print(f"Processor: {platform.processor()}")
    print(f"Python Version: {platform.python_version()}")
    print(f"Hostname: {socket.gethostname()}")

    # Boot time
    boot_time = datetime.datetime.fromtimestamp(psutil.boot_time())
    print(f"System Boot Time: {boot_time.strftime('%Y-%m-%d %H:%M:%S')}")
    uptime = datetime.datetime.now() - boot_time
    print(f"Uptime: {uptime}")

    # 2. CPU Status
    print("\n[2. CPU Status]")
    cpu_count_logical = psutil.cpu_count(logical=True)
    cpu_count_physical = psutil.cpu_count(logical=False)
    cpu_freq = psutil.cpu_freq()
    cpu_percent = psutil.cpu_percent(interval=1)  # Wait 1s for accurate usage

    print(f"Physical Cores: {cpu_count_physical}")
    print(f"Logical Cores: {cpu_count_logical}")
    if cpu_freq:
        print(
            f"Current Frequency: {cpu_freq.current:.2f} MHz (Max: {cpu_freq.max:.2f} MHz)"
        )
    print(f"Total Usage: {cpu_percent}%")

    # Usage per core
    print("Usage per Core:")
    for i, per_cpu in enumerate(psutil.cpu_percent(percpu=True, interval=0.5)):
        print(f"  Core {i}: {per_cpu}%")

    # 3. Memory Status
    print("\n[3. Memory Status]")
    mem = psutil.virtual_memory()
    print(f"Total Memory: {mem.total / (1024**3):.2f} GB")
    print(f"Used: {mem.used / (1024**3):.2f} GB ({mem.percent}%)")
    print(f"Available: {mem.available / (1024**3):.2f} GB")

    # Swap
    swap = psutil.swap_memory()
    print(f"Swap Total: {swap.total / (1024**3):.2f} GB")
    print(f"Swap Usage: {swap.percent}%")

    # 4. Disk Status
    # 4. Disk Status (Fixed version - added dirty data filtering)
    print("\n[4. Disk Status]")
    try:
        partitions = psutil.disk_partitions()
        disk_count = 0

        for partition in partitions:
            mountpoint = partition.mountpoint

            # --- Key Fix Steps Start ---

            # 1. Check if path is empty or contains illegal characters (e.g., null byte \x00)
            if not mountpoint:
                continue

            # Detect null bytes or other control characters, common in WSL or some virtual drives
            if "\x00" in mountpoint or any(
                ord(c) < 32 and c not in "\n\r\t" for c in mountpoint
            ):
                print(
                    f"  [Skipped] Detected illegal character path: {repr(mountpoint)}"
                )
                continue

            # 2. Try to normalize path (sometimes relative paths or strange prefixes cause issues)
            try:
                # On Windows, ensure path is absolute and correctly formatted
                clean_path = os.path.abspath(mountpoint)

                # Extra check: If WSL path (\\wsl$) or network path, sometimes needs special handling
                # But the main issue is null bytes, which are filtered above.
            except Exception:
                print(f"  [Skipped] Failed to normalize path: {mountpoint}")
                continue

            # --- Key Fix Steps End ---

            try:
                usage = psutil.disk_usage(clean_path)
                disk_count += 1

                device = getattr(partition, "device", "N/A")
                fstype = getattr(partition, "fstype", "N/A")

                # Safe printing to prevent garbled device names
                safe_device = (
                    str(device).encode("utf-8", errors="replace").decode("utf-8")
                )
                safe_mount = (
                    str(clean_path).encode("utf-8", errors="replace").decode("utf-8")
                )
                safe_fstype = (
                    str(fstype).encode("utf-8", errors="replace").decode("utf-8")
                )

                print(f"Partition: {safe_device} (Mount Point: {safe_mount})")
                print(f"  Filesystem: {safe_fstype}")
                print(f"  Total Space: {usage.total / (1024**3):.2f} GB")
                print(f"  Used: {usage.used / (1024**3):.2f} GB ({usage.percent}%)")
                print(f"  Free: {usage.free / (1024**3):.2f} GB")
                print("-" * 40)

            except PermissionError:
                print(
                    f"  [Permission Denied] Cannot access: {clean_path} (Admin rights may be required)"
                )
            except FileNotFoundError:
                print(f"  [Not Found] Path invalid: {clean_path}")
            except OSError as e:
                # Catch specific OS errors to prevent crash
                print(f"  [OS Error] Read failed {clean_path}: {e}")
            except Exception as e:
                # Catch any other unknown errors
                print(
                    f"  [Unknown Error] Skipped {clean_path}: {type(e).__name__} - {e}"
                )

        if disk_count == 0:
            print("  No valid disk partition information read successfully.")

    except Exception as e:
        print(f"  [Critical] Error getting partition list: {e}")

    # 5. Network Status
    print("\n[5. Network Status]")
    net_io = psutil.net_io_counters()
    print(f"Bytes Sent: {net_io.bytes_sent / (1024**2):.2f} MB")
    print(f"Bytes Received: {net_io.bytes_recv / (1024**2):.2f} MB")

    # Get main IP address (try to get non-loopback IPv4)
    addrs = psutil.net_if_addrs()
    ip_address = "No valid IP found"
    for interface_name, interface_addresses in addrs.items():
        for addr in interface_addresses:
            if addr.family == socket.AF_INET and not addr.address.startswith("127."):
                ip_address = addr.address
                break
        if ip_address != "No valid IP found":
            break
    print(f"Main IP Address: {ip_address}")

    # 6. Battery Status (Valid only for laptops)
    try:
        battery = psutil.sensors_battery()
        if battery:
            print("\n[6. Battery Status]")
            print(f"Remaining Power: {battery.percent}%")
            print(f"Charging: {'Yes' if battery.power_plugged else 'No'}")
            if (
                battery.secsleft != psutil.POWER_TIME_UNLIMITED
                and battery.secsleft != psutil.POWER_TIME_UNKNOWN
            ):
                mins = battery.secsleft // 60
                print(f"Estimated Time Remaining: {mins} minutes")
    except Exception:
        pass  # Desktops usually don't have battery sensors

    # 7. Top Processes (Sorted by Memory Usage)
    print("\n[7. Top 5 Processes by Memory Usage]")
    print("\nTop 5 Processes by CPU Usage:")

    # --- A. Highest CPU Usage ---
    print("\nTop 5 Processes by CPU Usage:")
    cpu_processes = []
    for proc in psutil.process_iter(["pid", "name", "cpu_percent"]):
        try:
            # Note: First call to cpu_percent returns 0, needs interval or relies on internal cache
            # For accuracy, usually call psutil.cpu_percent() once outside loop or let process object accumulate time
            # Here we read instantaneous value, feasible for snapshot though volatile
            info = proc.info
            if info["cpu_percent"] is None:
                continue
            cpu_processes.append(info)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue

    # Sort
    top_cpu = sorted(cpu_processes, key=lambda x: x["cpu_percent"] or 0, reverse=True)[
        :5
    ]

    if not top_cpu:
        # If first read is all 0, try reading again (needs time interval)
        # In actual script, better to call psutil.cpu_percent(interval=1) at start
        print("  (Recalibrating CPU data...)")
        psutil.cpu_percent(interval=0.5)  # Wait a moment
        cpu_processes = []
        for proc in psutil.process_iter(["pid", "name", "cpu_percent"]):
            try:
                info = proc.info
                if info["cpu_percent"] is not None:
                    cpu_processes.append(info)
            except:
                continue
        top_cpu = sorted(
            cpu_processes, key=lambda x: x["cpu_percent"] or 0, reverse=True
        )[:5]

    for i, proc in enumerate(top_cpu):
        print(
            f"  {i+1}. {proc['name']:<25} (PID: {proc['pid']}) -> {proc['cpu_percent']:.1f}%"
        )
    if not top_cpu:
        print("  No significant CPU consuming processes found.")

    print("\n" + "=" * 60)
    print("Status Detection Complete")
    print("=" * 60)


if __name__ == "__main__":
    get_system_status()
