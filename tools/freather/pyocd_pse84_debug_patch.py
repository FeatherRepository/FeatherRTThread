"""ASCII-only pyOCD compatibility patch for non-invasive PSE84 debugging."""
import logging
import time

import pyocd.coresight.discovery as disc
from pyocd.coresight.ap import APv2Address, AccessPort, MEM_AP
from pyocd.coresight.dap import DebugPort

log = logging.getLogger("pse84_debug_patch")
PSE84_BASE = 0xF0000000


_orig_find_root_components = disc.ADIv6Discovery._find_root_components


def _find_root_components(self):
    self.dp._base_addr = PSE84_BASE
    result = _orig_find_root_components(self)
    for page in range(1, 8):
        addr = PSE84_BASE + page * 0x1000
        try:
            idr = self.dp.read_ap(addr + APv2Address(addr).idr_address)
        except Exception:
            continue
        if idr == 0 or APv2Address(addr) in self.dp.aps:
            continue
        try:
            self.dp.aps[APv2Address(addr)] = AccessPort.create(
                self.dp, APv2Address(addr)
            )
        except Exception:
            continue
    return result


disc.ADIv6Discovery._find_root_components = _find_root_components

_kp3_wake_sequence = bytes.fromhex(
    "FE000108A5A5A950000000B1F0000D00A30B0000028B52400400BB80000000BD"
)
_orig_dp_connect = DebugPort._connect


def _dp_connect(self, *args, **kwargs):
    try:
        self._probe.swj_sequence(
            len(_kp3_wake_sequence) * 8,
            int.from_bytes(_kp3_wake_sequence, "little"),
        )
        time.sleep(0.05)
    except Exception as error:
        log.debug("wake sequence failed: %s", error)
    return _orig_dp_connect(self, *args, **kwargs)


DebugPort._connect = _dp_connect


def _hnonsec_for(address):
    # Non-secure SRAM, non-secure peripherals, and non-secure SMIF.
    return int(
        (0x20000000 <= address < 0x30000000)
        or (0x40000000 <= address < 0x60000000)
        or (0x60000000 <= address < 0x70000000)
    )


def _wrap_memory_method(name):
    original = getattr(MEM_AP, name)

    def wrapper(self, address, *args, **kwargs):
        try:
            required = _hnonsec_for(address)
            if self.hnonsec != required:
                self.hnonsec = required
        except AttributeError:
            pass
        return original(self, address, *args, **kwargs)

    wrapper.__name__ = name
    setattr(MEM_AP, name, wrapper)


for _method in (
    "read_memory",
    "write_memory",
    "read_memory_block32",
    "write_memory_block32",
):
    _wrap_memory_method(_method)
