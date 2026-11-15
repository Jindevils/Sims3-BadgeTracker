"""Build the resource manifest for the self-contained Badge Tracker ASI."""
from hashlib import sha256
from pathlib import Path
import sys

def main(builds: Path, generated: Path) -> None:
    names = ["Ts3ConquistasPainel.exe", "BadgeTracker.png",
             "BadgeTracker-pt-BR.tsv", "BadgeTracker-en-US.tsv"]
    names += [p.relative_to(builds).as_posix()
              for p in sorted((builds / "Ts3Emblemas").glob("*.png"))]
    digest = sha256()
    resources = []
    entries = []
    for number, name in enumerate(names, 200):
        path = builds / name
        digest.update(name.encode("utf-8") + b"\0" + sha256(path.read_bytes()).digest())
        resources.append(f'{number} RCDATA "{path.resolve().as_posix()}"')
        windows_name = name.replace("/", "\\\\")
        entries.append(f'    {{{number}, L"{windows_name}"}},')
    generated.mkdir(parents=True, exist_ok=True)
    (generated / "PanelAssets.rc").write_text("\n".join(resources) + "\n", encoding="utf-8-sig")
    header = [
        f'constexpr wchar_t assetVersion[] = L"{digest.hexdigest()[:20]}";',
        'constexpr EmbeddedAsset embeddedAssets[] = {', *entries, '};'
    ]
    (generated / "PanelAssets.inc").write_text("\n".join(header) + "\n", encoding="ascii")
    print(f"Embedded assets: {len(names)}")

if __name__ == "__main__":
    main(*map(Path, sys.argv[1:]))
