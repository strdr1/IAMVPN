; ─────────────────────────────────────────────────────────────────────────
;  Inno Setup скрипт установщика IAMVPN.
;  Упаковывает собранное приложение (build/) со всеми движками в setup.exe.
; ─────────────────────────────────────────────────────────────────────────

#define AppName "IAMVPN"
#define AppVersion "1.1.0"
#define AppPublisher "IAMVPN"
#define AppExe "IAMVPN.exe"

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
; Папка установки по умолчанию: Program Files\IAMVPN
DefaultDirName={autopf}\IAMVPN
DefaultGroupName=IAMVPN
DisableProgramGroupPage=yes
; Итоговый файл установщика кладём в installer\output
OutputDir=output
OutputBaseFilename=IAMVPN-Setup
Compression=lzma2/max
SolidCompression=yes
; Приложению нужны админ-права (TUN, WinDivert) — ставим в Program Files
PrivilegesRequired=admin
; Иконка самого установщика и uninstall
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\{#AppExe}
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Автообновление: если IAMVPN.exe запущен — закрыть его (force) и
; перезапустить после установки. AppId фиксируем, чтобы новая версия
; ставилась поверх старой (а не в отдельную папку).
AppId={{576A8418-8CC5-4E4F-BEDD-DE3D7EA979F1}
CloseApplications=force
CloseApplicationsFilter=*.exe
RestartApplications=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "Создать ярлык на рабочем столе"; GroupDescription: "Дополнительно:"
Name: "autostart"; Description: "Запускать при старте Windows"; GroupDescription: "Дополнительно:"; Flags: unchecked

[Files]
; Копируем ВСЁ содержимое build/ (exe + Qt dll + qml + engine с движками)
Source: "..\build\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; Меню Пуск
Name: "{group}\IAMVPN"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppExe}"
Name: "{group}\Удалить IAMVPN"; Filename: "{uninstallexe}"
; Рабочий стол (по галке)
Name: "{autodesktop}\IAMVPN"; Filename: "{app}\{#AppExe}"; IconFilename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Автозапуск (по галке) — от текущего пользователя
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "IAMVPN"; ValueData: """{app}\{#AppExe}"""; Flags: uninsdeletevalue; Tasks: autostart

[Run]
; ── 1. Обычная установка: галка "Запустить" в финальном диалоге. ─────────
;      Пропускается в /VERYSILENT (skipifsilent), там работает запись ниже.
; shellexec + runasoriginaluser — чтобы UAC корректно поднял права
; (приложению нужен admin-манифест), иначе CreateProcess падает с 740.
Filename: "{app}\{#AppExe}"; Description: "Запустить IAMVPN"; Flags: postinstall skipifsilent shellexec runasoriginaluser nowait

; ── 2. Тихая установка (автообновление из приложения): запуск без галки. ─
; Срабатывает только при /VERYSILENT — стандартный setup не задваивает.
; ВАЖНО: используем shellexec — он идёт через ShellExecuteEx, который
; ЧИТАЕТ манифест requireAdministrator и покажет UAC, если elevation не
; наследуется (бывает в Inno-режиме installmode admin). Без shellexec
; CreateProcess игнорирует манифест → exe запускается с filtered-токеном
; и sing-box падает: "configure tun interface: Access is denied".
Filename: "{app}\{#AppExe}"; Flags: shellexec nowait; Check: WizardSilent

[UninstallDelete]
; Чистим за собой движок/кэш при удалении
Type: filesandordirs; Name: "{app}\engine"
