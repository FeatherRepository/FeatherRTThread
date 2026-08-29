# pyocd PSE84 兼容补丁:BASEPTR0 地址修正 + AP 页枚举 + FLM 扇区范围安全交集
import logging
import time
log = logging.getLogger('pse84_patch')

import pyocd.coresight.discovery as disc
from pyocd.coresight.ap import APv2Address, AccessPort
from pyocd.target.pack import flm_region_builder
from pyocd.core.memory_map import MemoryRange

PSE84_BASE = 0xF0000000

_orig_find = disc.ADIv6Discovery._find_root_components
def _find_root_components(self):
    self.dp._base_addr = PSE84_BASE
    result = _orig_find(self)
    dp = self.dp
    for page in range(1, 8):
        addr = PSE84_BASE + page * 0x1000
        try:
            idr = dp.read_ap(addr + APv2Address(addr).idr_address)
        except Exception:
            continue
        if idr == 0 or APv2Address(addr) in dp.aps:
            continue
        try:
            ap = AccessPort.create(dp, APv2Address(addr))
            dp.aps[APv2Address(addr)] = ap
            log.info('PSE84 patch: 创建 %s (IDR=0x%08x)', ap.short_description, idr)
        except Exception as e:
            log.info('PSE84 patch: 页%d 创建失败 %s', page, e)
    return result
disc.ADIv6Discovery._find_root_components = _find_root_components

_orig_add_sub = flm_region_builder.FlmFlashRegionBuilder._add_flash_subregions
def _add_flash_subregions(self, region, pack_algo, page_size, algo):
    orig_iter = pack_algo.iter_sector_size_ranges
    def filtered_iter():
        for r, ss in orig_iter():
            rr = r if isinstance(r, MemoryRange) else MemoryRange(r.start, end=r.end)
            if region.contains_range(rr):
                yield r, ss
            else:
                s = max(region.start, rr.start); e = min(region.end, rr.end)
                if e >= s:
                    yield MemoryRange(s, end=e), ss
    pack_algo.iter_sector_size_ranges = filtered_iter
    try:
        return _orig_add_sub(self, region, pack_algo, page_size, algo)
    finally:
        pack_algo.iter_sector_size_ranges = orig_iter
flm_region_builder.FlmFlashRegionBuilder._add_flash_subregions = _add_flash_subregions
log.info('PSE84 pyocd 补丁已加载')

# ---------------------------------------------------------------------------
# KitProg3 PSE84 acquire (replicates OpenOCD `kitprog3 acquire_psoc`)
# ---------------------------------------------------------------------------
# OpenOCD's kitprog3 driver performs PSE84 Test-Mode acquisition with a
# KitProg3 vendor command carried over the CMSIS-DAP bulk channel
# (config_cat1d_pse84.tcl: KP3_ACQUIRE_TM_CMD). Without it the SWD DP is
# gated on a freshly power-cycled board and every DP access No-ACKs.
_KP3_ACQUIRE_TM_CMD = bytes.fromhex(
    'FE'                                   # 0xFE custom acquisition command
    '910104'                                # 0x91 set acquire params: handshake type 1, JTAG→dormant→SWD
    '85FE000108A5A5A950000000B1F0000D00A30B0000028B52400400BB80000000BD')  # 0x85 SWD sequence

def kitprog3_acquire_psoc(probe) -> bool:
    """Send the KitProg3 PSE84 Test-Mode acquire vendor command.

    `probe` is a pyocd CMSISDAPProbe whose USB link may be open or closed.
    Returns True if the command round-tripped without error.
    """
    link = probe._link
    opened_here = False
    if not link.is_open:
        link.open()
        opened_here = True
    try:
        iface = link._interface
        iface.write(_KP3_ACQUIRE_TM_CMD)
        resp = iface.read()
        log.info('KitProg3 acquire_psoc 应答: %s', resp[:8].hex() if resp else '(空)')
        return True
    except Exception as e:
        log.info('KitProg3 acquire_psoc 失败: %s', e)
        return False
    finally:
        if opened_here:
            link.close()

# PSE84 DP 唤醒序列: KP3_ACQUIRE_TM_CMD 中 0x85 命令的载荷,即 JTAG→Dormant→SWD
# 切换 + PSE84 专属握手。OpenOCD 通过 KitProg3 固件执行它;pyocd 的 DFP
# DebugPortSetup 不包含它,导致断电后的 DP 对任何访问 No-ACK。
_PSE84_WAKE_SEQ = bytes.fromhex('FE000108A5A5A950000000B1F0000D00A30B0000028B52400400BB80000000BD')

from pyocd.coresight.dap import DebugPort as _DebugPort
_orig_dp_connect = _DebugPort._connect
def _dp_connect_with_acquire(self, *args, **kwargs):
    try:
        self._probe.swj_sequence(len(_PSE84_WAKE_SEQ) * 8,
                                 int.from_bytes(_PSE84_WAKE_SEQ, 'little'))
        time.sleep(0.05)
        log.info('PSE84 DP 唤醒序列已发送')
    except Exception as e:
        log.info('DP 唤醒序列发送失败: %s', e)
    return _orig_dp_connect(self, *args, **kwargs)
_DebugPort._connect = _dp_connect_with_acquire
log.info('KitProg3 acquire/PSE84 唤醒补丁已挂载')

# ---------------------------------------------------------------------------
# 按地址自适应 HNONSEC (PSE84 总线安全属性)
# ---------------------------------------------------------------------------
# 同一颗 CM33 的 AHB5-AP 需要访问两种安全属性的内存:
#   - NS SRAM 0x20000000-0x2FFFFFFF (含 RRAM/SMIF_S 算法 RAM 0x24000000) → HNONSEC=1
#   - NS SMIF flash 0x60000000-0x6FFFFFFF → HNONSEC=1
#   - Secure SRAM 0x30000000+ (SMIF 算法 RAM 0x34008100)、SMIF_S 0x70000000+ → HNONSEC=0
#   - 核心调试寄存器 0xE0000000+ → 两者皆可
# 固定任一设置都会在对侧访问时 FAULT 并以粘滞错误污染后续传输。
def _pse84_hnonsec_for(addr: int) -> int:
    if 0x20000000 <= addr < 0x30000000 or 0x60000000 <= addr < 0x70000000:
        return 1
    return 0

from pyocd.coresight.ap import MEM_AP as _MEM_AP
_wrap_calls = [0]
def _wrap_mem_method(name):
    _orig = getattr(_MEM_AP, name)
    def wrapper(self, addr, *args, **kwargs):
        _wrap_calls[0] += 1
        if _wrap_calls[0] <= 3:
            log.info('HNONSEC 包装命中: %s addr=%#x (call#%d)', name, addr, _wrap_calls[0])
        try:
            if self.hnonsec != _pse84_hnonsec_for(addr):
                self.hnonsec = _pse84_hnonsec_for(addr)
        except AttributeError:
            pass
        return _orig(self, addr, *args, **kwargs)
    wrapper.__name__ = name
    setattr(_MEM_AP, name, wrapper)
for _m in ('read_memory', 'write_memory', 'read_memory_block32', 'write_memory_block32'):
    _wrap_mem_method(_m)
log.info('PSE84 地址自适应 HNONSEC 补丁已挂载')

# ---------------------------------------------------------------------------
# 修复 pack 生成的 flash 区域扇区参数
# ---------------------------------------------------------------------------
# PSE84_SMIF_S.FLM 与 PSE84_SMIF.FLM 是同一算法(扇区范围基于 0x60000000),
# 与 SMIF_S 区域(0x70000000+)无交集 → pyocd 不建子区域且父区域 sector_size=0,
# get_sector_info 对齐时除零。按同类区域补齐:
#   SMIF_S (0x70..) ≡ SMIF (0x60..):  sector 0x40000, page 0x1000
#   RRAM_S  (0x32..) ≡ RRAM  (0x22..): sector 0x400,   page 0x400
def fix_flash_sector_sizes(target) -> None:
    fixed = 0
    for r in target.memory_map.regions:
        if not r.is_flash:
            continue
        if getattr(r, 'sector_size', 0):
            continue
        params = None
        if 0x70000000 <= r.start < 0x80000000:
            params = (0x40000, 0x1000)
        elif 0x32000000 <= r.start < 0x33000000:
            params = (0x400, 0x400)
        if params:
            sector, page = params
            r._attributes['blocksize'] = sector
            r._attributes['sector_size'] = sector
            r._attributes['page_size'] = page
            r._attributes['erase_sector_weight'] = max(1, sector // 1024)
            r._attributes['program_page_weight'] = max(1, page // 8)
            fixed += 1
            log.info('扇区参数修复: %s (%#x) sector=%#x page=%#x', r.name, r.start, sector, page)
    if fixed:
        log.info('共修复 %d 个 flash 区域的扇区参数', fixed)
