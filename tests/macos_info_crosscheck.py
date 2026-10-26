"""Compare native module results with macOS command-line utilities (read only)."""
from pathlib import Path
import plistlib
import re
import subprocess
import sys


def output(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
    values = output(str(build / "ptoolkit_system_info_tests")).splitlines()
    assert values == ["macOS", "macOS", output("sw_vers", "-productVersion"),
                      output("sw_vers", "-buildVersion")], values
    hardware = output(str(build / "ptoolkit_hardware_info_tests"))
    cpu, cores = re.search(r"^(.+), (\d+) cores$", hardware, re.M).groups()
    assert cpu == output("sysctl", "-n", "machdep.cpu.brand_string")
    assert int(cores) == int(output("sysctl", "-n", "hw.physicalcpu"))
    memory = int(re.search(r"^RAM bytes: (\d+)$", hardware, re.M)[1])
    disk = int(re.search(r"^Disk bytes: (\d+)$", hardware, re.M)[1])
    assert memory == int(output("sysctl", "-n", "hw.memsize"))
    physical = plistlib.loads(subprocess.check_output(
        ["diskutil", "list", "-plist", "internal", "physical"]))
    expected_disk = sum(item["Size"] for item in physical["AllDisksAndPartitions"])
    assert disk == expected_disk, (disk, expected_disk)
    print(f"System version, CPU, cores, RAM and physical disk match macOS utilities; disk={disk} bytes")


if __name__ == "__main__":
    main()
