"""Extract the Steam 1.67 managed build references without running the game."""
import struct
import sys
from pathlib import Path
from dbpf import resources

PACKAGES = {
    "scripts.package": {
        0xC356DF69B70ADD42: "SimIFace",
        0x78CF6CF5304D0C4F: "ScriptCore",
        0x600F9EA1DDC99FB1: "Sims3Metadata",
    },
    "simcore.package": {
        0x28EE9D383A73463E: "mscorlib",
        0x342EE04373CF1E1C: "System",
        0x6AC101133051BEF1: "System.Xml",
    },
    "gameplay.package": {
        0x03D6C8D903CE868C: "Sims3GameplaySystems",
        0xB9C90FDC6793BC0A: "Sims3GameplayObjects",
        0xF7C3ADE896D4E765: "UI",
    },
}

def decode(blob):
    version = blob[0]
    if version not in (1, 2):
        raise ValueError(f"Unsupported S3SA version {version}")
    count_pos = 69
    if version == 2:
        count_pos += 4 + 2 * struct.unpack_from("<I", blob, 1)[0]
    count, = struct.unpack_from("<H", blob, count_pos)
    table = blob[count_pos + 2:count_pos + 2 + count * 8]
    if len(table) != count * 8 or not count:
        raise ValueError("Invalid S3SA block table")
    seed = sum(value for (value,) in struct.iter_unpack("<Q", table)) & (len(table) - 1)
    source = count_pos + 2 + len(table)
    decoded = bytearray(count * 512)
    for block in range(count):
        if table[block * 8] & 1:
            continue
        encrypted = blob[source:source + 512]
        if len(encrypted) != 512:
            raise ValueError("Truncated S3SA block")
        for offset, value in enumerate(encrypted):
            decoded[block * 512 + offset] = value ^ table[seed]
            seed = (seed + value) % len(table)
        source += 512
    if decoded[:2] != b"MZ":
        raise ValueError("Decoded assembly has no PE header")
    return decoded

def main(game, destination):
    destination.mkdir(parents=True, exist_ok=True)
    for package, expected in PACKAGES.items():
        found = set()
        for _, group, instance, data in resources(game / "Game/Bin" / package, {0x073FAA07}):
            if group == 0 and instance in expected:
                name = expected[instance]
                (destination / (name + ".dll")).write_bytes(decode(data))
                found.add(instance)
                print(f"{package} {instance:016X} {name}.dll")
        if missing := expected.keys() - found:
            raise ValueError(f"{package} missing {', '.join(expected[i] for i in sorted(missing))}")

if __name__ == "__main__":
    main(*map(Path, sys.argv[1:]))
