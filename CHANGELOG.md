# Changelog

## v0.3.0

- Added a dedicated EA App 1.69.47 edition including the 32-bit ASI loader dinput8.dll. The Steam download remains v0.2.0.
- Read completed achievement history independently of active challenges. After signing into EA in-game, allow synchronization, wait about 30 seconds in Live Mode, then close and reopen the panel. A restart is normally unnecessary.
- Replaced repeated unavailable-progress labels with a warning icon beside volume in the EA App panel. Hover or click for English or Brazilian Portuguese guidance.
- Corrected EA exports with compact records and terminated blocks. Reduced checks to every 30 seconds and export only changed snapshots.
- Bring an existing panel window back to the foreground when the shortcut is pressed again.

**Updating the EA App edition:** Close the game and replace both BadgeTracker.asi in Game/Bin and BadgeTracker.package in Mods/Packages. The download includes dinput8.dll. Use it if you need a loader and check an existing loader before replacing files.

**Validation:** Opening, reopening and some completed achievements were confirmed in the author's EA App installation. Native reader tests covered 265 records, Unicode, block terminators, invalid input and reopening a test window. The newest warning interface was checked in the preview and awaits in-game confirmation.

**EA limitations:** Individual progress counters need validation. Completion notifications and live updates to open pinned cards remain pending. The transport currently retains its marked ScriptError XML exports.

## v0.2.0

- Added Manual Unlock for incomplete achievements that fail to unlock after you meet their requirements. The unlock is saved locally in Badge Tracker. You can stay signed into EA, and your EA account and online achievements are unaffected.
- Added Highest Progress and Lowest Progress sorting. Both views show incomplete achievements with a progress bar and compare the percentage of each goal. Name sorting restores the regular list.
- Reorganized the sidebar and achievement cards. Badges are larger, Gold and Platinum cards have a rarity glow, and pinning and sound controls now use icons.

**Updating from v0.1.0:** Close the game and replace the old ASI in `Game/Bin` with `BadgeTracker.asi`. Keep your existing package and remove any renamed copy of the old ASI.
