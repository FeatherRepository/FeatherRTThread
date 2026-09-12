"""Read-only over-air FeatherTalk advertising and GATT diagnostic."""
import asyncio
import sys
import logging
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ble-validation-vendor"))
from bleak import BleakScanner, BleakClient
logging.basicConfig(level=logging.DEBUG if "--debug" in sys.argv else logging.WARNING)

async def main():
    found = {}
    def seen(device, adv):
        if "FeatherTalk" in (adv.local_name or device.name or ""):
            if device.address in found:
                return
            found[device.address] = device
            print("ADV", device.address, adv.local_name, adv.service_uuids,
                  {k: v.hex() for k,v in adv.service_data.items()}, flush=True)
    async with BleakScanner(seen, winrt={"allow_extended_advertisements": True}):
        await asyncio.sleep(10)
    if not found:
        print("NO_FEATHERTALK_ADVERTISEMENT", flush=True)
        return
    device = next(iter(found.values()))
    if "--unpair" in sys.argv:
        await BleakClient(device).unpair()
        print("REMOVED_TEST_PAIRING", flush=True)
    async with BleakClient(device, timeout=30, pair="--pair" in sys.argv, winrt={"use_cached_services": False}) as client:
        print("CONNECTED", client.is_connected, "MTU", client.mtu_size, flush=True)
        for service in client.services:
            print("SERVICE", service.uuid, flush=True)
            for char in service.characteristics:
                if char.uuid.startswith(("00002bc", "00002b51", "00002b7")):
                    print("CHAR", char.uuid, char.handle, char.properties, flush=True)
                    if "read" in char.properties:
                        value = await client.read_gatt_char(char)
                        print("VALUE", value.hex(), flush=True)

if __name__ == "__main__":
    asyncio.run(main())
