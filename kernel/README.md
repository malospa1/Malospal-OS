# Сборка ядра Malospal OS

Автор проекта — malospal. Ядро написано на C17 и ассемблере x86-64,
загружается через Limine. Все команды выполняются из корня репозитория.

## Инструменты

- Git.
- Python 3.9 или новее.
- [Zig 0.15.2](https://ziglang.org/download/0.15.2/release-notes.html), используется как C-компилятор и линкер.
- [QEMU](https://www.qemu.org/download/) с `qemu-system-x86_64` для запуска и тестов.
- pycdlib 1.14.0 из `kernel/requirements.txt` для создания ISO.

Добавь Git, Python, Zig и QEMU в `PATH`. На Windows вместо изменения `PATH`
можно указать пути через переменные окружения:

```powershell
$env:MALOSPAL_ZIG = 'C:\Tools\zig\zig.exe'
$env:MALOSPAL_QEMU = 'C:\Tools\qemu\qemu-system-x86_64.exe'
$env:MALOSPAL_PYTHON = 'C:\Tools\Python\python.exe'
```

Это примеры путей: нужно указать расположение установленных инструментов.
`MALOSPAL_PYTHON` используется PowerShell-обёрткой; Python-сценарий запускается
выбранным интерпретатором напрямую. Git можно выбрать через `MALOSPAL_GIT`.

Сборщик также распознаёт локальные инструменты в игнорируемой папке:

```text
kernel/.cache/
    zig-x86_64-windows-0.15.2/zig.exe
    qemu/qemu-system-x86_64.exe
    qemu/share/edk2-x86_64-code.fd
    qemu/share/edk2-i386-vars.fd
```

Установи библиотеку для ISO:

```powershell
python -m pip install -r kernel/requirements.txt
```

При первой сборке скачивается Limine `v8.7.0-binary`, commit
`aad3edd370955449717a334f0289dee10e2c5f01`. Сборщик проверяет commit и отсутствие
изменений в его отслеживаемых файлах. Кэш и готовые образы не входят в Git.

## Основные команды

```powershell
python kernel/tools/build.py build
python kernel/tools/build.py run
python kernel/tools/build.py run --uefi
python kernel/tools/build.py check
```

В Windows доступны `run-kernel.cmd` и PowerShell-обёртка:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\kernel\build.ps1 build
powershell -NoProfile -ExecutionPolicy Bypass -File .\kernel\build.ps1 check
```

Параметр ExecutionPolicy действует только на запущенный процесс PowerShell.
При разрешённом запуске сценариев достаточно `.\kernel\build.ps1 build`.

Для BIOS используется обычный запуск. Параметр `--uefi` ищет прошивки
`share/edk2-x86_64-code.fd` и `share/edk2-i386-vars.fd` рядом с QEMU.
Если они установлены отдельно:

```powershell
python kernel/tools/build.py smoke --firmware C:\Tools\OVMF_CODE.fd --firmware-vars C:\Tools\OVMF_VARS.fd
```

Нужна согласованная пара CODE/VARS. Изменения VARS записываются во временный
слой QEMU. Сеть гостя отключена. Для остановки закрой окно QEMU или нажми Ctrl+C.

## Результаты сборки

- `kernel/build/normal/kernel.elf` — ядро с отладочной информацией.
- `kernel/build/normal/malospal-os.iso` — загрузочный CD/DVD-образ BIOS/UEFI.
- `kernel/build/normal/usb-uefi/` — файлы для существующей FAT32-флешки UEFI.

В ISO и комплект USB включаются сведения об авторе и лицензии загрузчика и PCG.
Пути к локальной папке проекта заменяются относительными в отладочной информации.

## Отдельные проверки

| Команда после `python kernel/tools/build.py` | Проверка |
| --- | --- |
| `unit` | Тесты логики на основной системе |
| `smoke --uefi` | Загрузка и выход через тестовое устройство QEMU |
| `fault --exception page-fault` | Обработка выбранного исключения |
| `memory`, `virtual`, `timer` | Память и прерывания таймера |
| `console`, `screen` | Последовательная консоль и реальные пиксели экрана |
| `keyboard`, `usb` | Ввод PS/2 и USB |
| `game`, `slots` | Управление играми, изображение и возврат в консоль |
| `check` | Весь набор: 30 групп тестов и 63 запуска QEMU |

Журналы и снимки экрана сохраняются в `kernel/build/`. Проверки запускают
и завершают свои экземпляры QEMU. Гостевые тестовые ядра собираются отдельно
от обычного ядра.

Сборка и тестирование выпуска описаны в [docs/testing.md](../docs/testing.md).
Сценарий поддерживает Linux, но текущий выпуск проверялся в Windows.

Для путей с кириллицей сборщик использует короткие пути Windows при обращении
к QEMU. Если короткие имена отключены на диске, размести проект в папке
с латинским именем, например `C:\Projects\Malospal-OS`.

[Архитектура](../docs/architecture.md) · [USB](../docs/usb-keyboard.md) ·
[Сторонние компоненты](../THIRD_PARTY.md).
