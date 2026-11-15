@echo off
if not exist work mkdir work
rc /nologo /fo work\BadgeTracker.res "native\BadgeTracker.rc"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /O2 /MT /Iwork\imgui /Iwork\imgui\backends /Fo:work\ "native/Overlay.cpp" work\imgui\imgui.cpp work\imgui\imgui_draw.cpp work\imgui\imgui_tables.cpp work\imgui\imgui_widgets.cpp work\imgui\backends\imgui_impl_win32.cpp work\imgui\backends\imgui_impl_dx9.cpp work\BadgeTracker.res /link user32.lib gdi32.lib imm32.lib dwmapi.lib ole32.lib windowscodecs.lib winmm.lib /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /OUT:"builds/Ts3ConquistasPainel.exe"
if errorlevel 1 exit /b %errorlevel%
python -B "tools/embed_panel.py" "builds" work\embedded
if errorlevel 1 exit /b %errorlevel%
rc /nologo /fo work\embedded\PanelAssets.res work\embedded\PanelAssets.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /O2 /MT /LD /Iwork\embedded /Fo:work\ "native/Shortcut.cpp" "native/PanelBridge.cpp" "native/EmbeddedPanel.cpp" work\embedded\PanelAssets.res /link user32.lib /OUT:"builds/Ts3Conquistas.asi"
