@echo off
if not exist ea169\builds mkdir ea169\builds
if not exist work\ea169 mkdir work\ea169
python -B ea169\tools\build.py work\refs
if errorlevel 1 exit /b %errorlevel%
copy /y builds\BadgeTracker*.tsv ea169\builds\ >nul
copy /y builds\BadgeTracker.png ea169\builds\ >nul
xcopy /e /i /y builds\Ts3Emblemas ea169\builds\Ts3Emblemas >nul
rc /nologo /fo work\ea169\BadgeTracker.res native\BadgeTracker.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /O2 /MT /Inative /Iwork\imgui /Iwork\imgui\backends /Fo:work\ea169\ ea169\native\Overlay.cpp work\imgui\imgui.cpp work\imgui\imgui_draw.cpp work\imgui\imgui_tables.cpp work\imgui\imgui_widgets.cpp work\imgui\backends\imgui_impl_win32.cpp work\imgui\backends\imgui_impl_dx9.cpp work\ea169\BadgeTracker.res /link user32.lib gdi32.lib imm32.lib dwmapi.lib ole32.lib windowscodecs.lib winmm.lib /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup /OUT:ea169\builds\Ts3ConquistasPainel.exe
if errorlevel 1 exit /b %errorlevel%
python -B tools\embed_panel.py ea169\builds work\ea169\embedded
if errorlevel 1 exit /b %errorlevel%
rc /nologo /fo work\ea169\embedded\PanelAssets.res work\ea169\embedded\PanelAssets.rc
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /std:c++17 /O2 /MT /LD /Inative /Iwork\ea169\embedded /Fo:work\ea169\ ea169\native\EA169.cpp native\EmbeddedPanel.cpp work\ea169\embedded\PanelAssets.res /link user32.lib shell32.lib /OUT:ea169\builds\BadgeTracker.asi
if errorlevel 1 exit /b %errorlevel%
copy /y ea169\builds\Ts3Conquistas.package ea169\builds\BadgeTracker.package >nul
