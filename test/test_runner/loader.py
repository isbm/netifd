import os
from typing import List

SUPPORTED_JSON_RESULT_PREFIXES = [
    "ipaddr4",
    "ipaddr6",
    "iplink",
    "iproute4",
    "iproute6",
    "sysctl4",
    "sysctl6"
]
NETWORK_CONFIG_NAME = "network"

class TestException(Exception):
    pass

class UnknownFileError(TestException):
    pass

class MissingFileError(TestException):
    pass

class IPAddress():
    address: str
    netmask: int
    isIPv6: bool

    def __init__(self, cdir: str) -> None:
        addr, mask = cdir.split('/', 1)
        self.address = addr
        self.netmask = int(mask)
        self.isIPv6 = ":" in addr

    def __repr__(self) -> str:
        return f"{self.address}/{self.netmask}"

class InterfaceFile():
    name: str
    interface: str
    path: str
    type: str
    version: int

    def __init__(self, path: str) -> None:
        self.name = os.path.splitext(os.path.basename(path))[0]
        self.path = path
        base,_ = os.path.splitext(os.path.basename(path))
        if base == "nameservers":
            self.type = "nameservers"
            self.interface = None
            self.version = 0
        else:
            self.type, self.interface = base.split("_", 1)
            self.version = 6 if self.type.endswith("6") else 4

class DHCPConfigFile(InterfaceFile):
    addr: IPAddress
    def __init__(self, path: str) -> None:
        super().__init__(path)

        self.addr = None
        with open(path) as f:
            for line in f:
                if line.startswith("#addr"):
                    self.addr = IPAddress(line[5:].strip())
                    break

class TestSuite():
    _path: str
    _name: str
    validation_files: List[InterfaceFile]
    waitfor_interfaces: List[str]
    dhcp_config: List[DHCPConfigFile]

    def __init__(self, testdir: str) -> None:
        self._path = testdir
        self._name = os.path.basename(testdir)[len("test_"):]
        self.validation_files = []
        self.waitfor_interfaces = []
        self.dhcp_config = []
        self._load()

    def _load(self):
        files = os.listdir(self._path)
        if NETWORK_CONFIG_NAME not in files:
            raise MissingFileError(f"File {NETWORK_CONFIG_NAME} is missing in test '{self._name}'")
        files.remove(NETWORK_CONFIG_NAME)

        for entry in files:
            fullpath = os.path.join(self._path, entry)
            if (entry.startswith("dhcpd4_") or entry.startswith("dhcpd6_")) and entry.endswith(".conf"):
                dhcp = DHCPConfigFile(fullpath)
                self.dhcp_config.append(dhcp)
            elif entry == "waitfor":
                with open(fullpath) as f:
                    self.waitfor_interfaces = [e.strip() for e in f.readlines() if e.strip() and not e.strip().startswith('#')]
            elif entry == "nameservers":
                self.validation_files.append(InterfaceFile(fullpath))
            elif entry.endswith(".json") and entry.split("_")[0] in SUPPORTED_JSON_RESULT_PREFIXES:
                self.validation_files.append(InterfaceFile(fullpath))
            else:
                raise UnknownFileError(f"Don't know what to do with file '{entry}' in test '{self._name}'")

    @property
    def network_config(self) -> str:
        return os.path.join(self._path, NETWORK_CONFIG_NAME)

    @property
    def name(self) -> str:
        return self._name

    def __repr__(self) -> str:
        return f"TestSuite({self._name})"
