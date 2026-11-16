"""Build the example dataset for the standalone preview."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def main():
    rows = json.loads((ROOT / "preview-catalog.json").read_text(encoding="utf-8"))
    packet = bytearray(f"0 {len(rows)} ".encode("ascii"))
    sample_progress = {
        "de02690e-4eb9-41d3-a08b-c2c1ad4ef57d": "8/15/children",
        "10 por 100": "3/10/count",
        "10.000 Simoleons": "7500/10000/count",
    }
    for index, (name, description, pack, medal, guid) in enumerate(rows):
        milestone = sample_progress.get(guid) or sample_progress.get(name)
        packet.extend(b"1 " if index % 3 == 0 and not milestone else b"0 ")
        if milestone:
            medal += f", Milestone:{milestone}\nSim da fam\u00edlia de exemplo"
        for value in (name, description, f"{pack}, {medal}", guid):
            encoded = value.encode("utf-8")
            packet.extend(f"{len(encoded)} ".encode("ascii"))
            packet.extend(encoded)
    (ROOT / "builds/BadgeTracker-Previa.dat").write_bytes(packet)


if __name__ == "__main__":
    main()
