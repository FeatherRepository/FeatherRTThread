# -*- coding: utf-8 -*-
"""PyOCD PSE84 flash driver.

Drives pyOCD to program the Edgi-Talk (PSoC Edge E84) merged image, working
around three pyOCD 0.45.1 gaps on this target:

1. KitProg3 acquire: satisfied by the DFP debug sequences executed under
   connect mode `under-reset` (option pack.debug_sequences.enable).
2. PSE84_SMIF / PSE84_SMIF_S FLAs are marked default="0" in the DFP, so pyOCD
   omits the external flash regions. Use the patched pack
   cmsis-packs/Infineon.PSE8xxx_DFP.1.1.0-smif-default.pack where the SMIF
   algorithms are default="1".
3. pyOCD generic ADIv6 discovery quirks on PSE84 (DP BASEPTR0 encoding, AP
   page enumeration, FLM sector-range intersection) are patched at runtime by
   pyocd_pse84_patch.py in this directory.
"""
import argparse
import logging
import sys

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('image', help='Final merged image (build/rtthread.hex)')
    parser.add_argument('--pack', required=True, help='Patched PSE84 CMSIS pack')
    parser.add_argument('--uid', required=True, help='KitProg3 probe unique id')
    parser.add_argument('--target', default='pse846gps2dbzc4a')
    parser.add_argument('--frequency', type=int, default=1_000_000, help='SWD frequency in Hz')
    parser.add_argument('--no-reset', action='store_true', help='Do not reset after programming')
    parser.add_argument('--smart-flash', action='store_true',
                        help='Skip unchanged sectors (default: force erase+program)')
    parser.add_argument('-v', '--verbose', action='count', default=0)
    args = parser.parse_args()

    logging.basicConfig(level=logging.DEBUG if args.verbose >= 2 else (logging.INFO if args.verbose else logging.WARNING),
                        format='%(levelname)s %(name)s: %(message)s')

    # Apply PSE84 compatibility patches before pyOCD touches the target.
    import pyocd_pse84_patch  # noqa: F401  (side effects required)

    from pyocd.core.session import Session
    from pyocd.flash.file_programmer import FileProgrammer
    from pyocd.probe.aggregator import DebugProbeAggregator

    probe = DebugProbeAggregator.get_probe_with_id(args.uid)
    if probe is None:
        print(f'ERROR: probe {args.uid} not found; run `pyocd list` to check.', file=sys.stderr)
        return 2

    session = Session(probe,
                      target_override=args.target,
                      pack=[args.pack],
                      connect_mode='halt',
                      options={
                          'pack.debug_sequences.enable': True,
                          'primary_core': 0,
                          'smart_flash': bool(args.smart_flash),
                          'trust_crc': False,
                          'cmsis_dap.deferred_transfers': False,
                          'cmsis_dap.limit_packets': True,
                      })
    program_ok = False
    try:
        with session:
            session.open()
            print(f'目标初始化成功: cores={ {k: type(v).__name__ for k, v in session.target.cores.items()} }')
            import pyocd_pse84_patch
            pyocd_pse84_patch.fix_flash_sector_sizes(session.target)
            # PSE84 安全属性自适应: CM33 AP 默认 secure (算法加载到安全 SRAM),
            # CM55 AP 必须非安全访问 (tcl: "CM55 AP always Non-Secure")。
            # 逐核探测可用的 HNONSEC 设置。
            for cid, core in session.target.cores.items():
                ap = getattr(core, 'ap', None)
                if ap is None or not hasattr(ap, 'hnonsec'):
                    continue
                chosen = None
                for hn in (0, 1):
                    try:
                        ap.hnonsec = hn
                        ap.read32(0xE000EDF0)  # DHCSR 探测
                        chosen = hn
                        break
                    except Exception:
                        continue
                if chosen is None:
                    print(f'core {cid}: 警告 DHCSR 探测失败, 保持默认安全属性')
                else:
                    print(f'core {cid}: {ap.short_description} HNONSEC -> {chosen}')
            programmer = FileProgrammer(session,
                                        chip_erase='sector',
                                        smart_flash=bool(args.smart_flash))
            programmer.program(args.image)
            print(f'烧录完成: {args.image}')
            program_ok = True
            if not args.no_reset:
                try:
                    session.target.reset()
                    print('目标已复位运行')
                except Exception as e:
                    # RT-Thread 空闲即进入 deep sleep,DP 可能已休眠;芯片复位可
                    # 通过 OpenOCD (init; reset run) 或板上复位键完成。
                    print(f'警告: 芯片复位未完成({type(e).__name__}), '
                          f'可用 OpenOCD 复位或按板上复位键启动新固件')
    except Exception as e:
        import traceback
        print(f'ERROR: {type(e).__name__}: {e}', file=sys.stderr)
        traceback.print_exc()
        return 1
    return 0 if program_ok else 1

if __name__ == '__main__':
    sys.exit(main())
