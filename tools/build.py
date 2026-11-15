from __future__ import annotations

from pathlib import Path
import struct
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
OUT_DLL = ROOT / "builds" / "Ts3Conquistas.dll"
OUT_PACKAGE = ROOT / "builds" / "Ts3Conquistas.package"
CSC = Path(r"C:\Windows\Microsoft.NET\Framework\v3.5\csc.exe")


def fnv1_64_lower(text: str) -> int:
    value = 0xCBF29CE484222325
    for byte in text.lower().encode("utf-8"):
        value = (value * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        value ^= byte
    return value


def u32(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def u64_parts(value: int) -> tuple[int, int]:
    return (value >> 32) & 0xFFFFFFFF, value & 0xFFFFFFFF


def counted_ascii(text: str) -> bytes:
    data = text.encode("ascii")
    return u32(len(data)) + data


def compile_dll(core: Path) -> None:
    refs = [
        core / "mscorlib.dll",
        core / "System.dll",
        core / "System.Xml.dll",
        core / "ScriptCore.dll",
        core / "SimIFace.dll",
        core / "Sims3GameplayObjects.dll",
        core / "Sims3GameplaySystems.dll",
        core / "Sims3Metadata.dll",
        core / "UI.dll",
    ]
    cmd = [
        str(CSC),
        "/noconfig",
        "/nostdlib+",
        "/target:library",
        "/platform:x86",
        "/optimize+",
        "/out:" + str(OUT_DLL),
    ]
    cmd.extend("/reference:" + str(ref) for ref in refs)
    cmd.append(str(ROOT / "src" / "Bootstrap.cs"))
    subprocess.check_call(cmd)


def build_s3sa_payload(dll: bytes) -> bytes:
    blocks = (len(dll) + 511) // 512
    header = bytes.fromhex("01 9f f7 c4 2b") + bytes(64) + struct.pack("<H", blocks)
    return header + bytes(8 * blocks) + dll.ljust(512 * blocks, b"\0")


def build_assembly_table(class_hash: int, class_name: str, assembly_hash: int, assembly_name: str) -> bytes:
    class_hi, class_lo = u64_parts(class_hash)
    assembly_hi, assembly_lo = u64_parts(assembly_hash)
    payload = bytearray()
    payload += u32(1)
    payload += u32(2)
    payload += u32(class_lo) + u32(class_hi)
    payload += counted_ascii(class_name)
    payload += u32(assembly_lo) + u32(assembly_hi)
    payload += counted_ascii(assembly_name)
    return bytes(payload)


def write_dbpf(resources: list[tuple[int, int, int, int, bytes]], package: Path) -> None:
    offset = 0x60
    body = bytearray()
    entries = []
    for resource_type, group, instance_hi, instance_lo, data in resources:
        body.extend(data)
        entries.append((resource_type, group, instance_hi, instance_lo, offset, len(data), len(data), 0))
        offset += len(data)

    index_offset = offset
    index = bytearray()
    index += u32(2)
    index += u32(0)
    for resource_type, group, instance_hi, instance_lo, data_offset, file_size, mem_size, compression in entries:
        index += u32(resource_type)
        index += u32(instance_hi)
        index += u32(instance_lo)
        index += u32(data_offset)
        index += u32(file_size | 0x80000000)
        index += u32(mem_size)
        index += struct.pack("<H", compression)
        index += struct.pack("<H", 1)

    header = bytearray(0x60)
    header[0:4] = b"DBPF"
    header[4:8] = u32(2)
    header[0x24:0x28] = u32(len(resources))
    header[0x2C:0x30] = u32(len(index))
    header[0x3C:0x40] = u32(3)
    header[0x40:0x44] = u32(index_offset)

    package.parent.mkdir(parents=True, exist_ok=True)
    package.write_bytes(bytes(header) + bytes(body) + bytes(index))


def main() -> int:
    compile_dll(Path(sys.argv[1]))
    class_name = "Ts3Conquistas.Bootstrap"
    assembly_name = "Assembly_Ts3Conquistas"
    class_hash = fnv1_64_lower(class_name)
    assembly_hash = fnv1_64_lower(assembly_name)
    class_hi, class_lo = u64_parts(class_hash)
    assembly_hi, assembly_lo = u64_parts(assembly_hash)

    xml = b'<?xml version="1.0" encoding="utf-8"?>\r\n<base>\r\n\t<Current_Tuning>\r\n\t\t<init value="False"></init>\r\n\t</Current_Tuning>\r\n</base>\r\n'
    assembly_table = build_assembly_table(class_hash, class_name, assembly_hash, assembly_name)
    s3sa = build_s3sa_payload(OUT_DLL.read_bytes())

    resources = [
        (0x0333406C, 0, class_hi, class_lo, xml),
        (0x0166038C, 0, 0, 0, assembly_table),
        (0x073FAA07, 0, assembly_hi, assembly_lo, s3sa),
    ]
    write_dbpf(resources, OUT_PACKAGE)
    print(OUT_PACKAGE)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())



