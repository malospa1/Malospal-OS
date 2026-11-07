# Сторонние компоненты

Автор Malospal OS — malospal. У использованных компонентов остаются свои авторы
и условия распространения.

| Компонент | Автор и лицензия | Использование |
| --- | --- | --- |
| Limine v8.7.0-binary | Limine project, BSD-2-Clause | Загрузка ядра; commit `aad3edd370955449717a334f0289dee10e2c5f01` |
| font8x8 | Daniel Hepper, на основе шрифта Marcel Sondaar; Public Domain | Шрифт консоли, таблица сделана константной |
| SDL Snake example | SDL contributors; Public Domain | Адаптирована логика игры; SDL в ядро не входит |
| PCG basic C | Melissa O'Neill, Apache-2.0 | Генератор символов в слотах; заголовок адаптирован для freestanding-сборки |

Исходники и сведения об изменениях находятся в `kernel/vendor/`.
Загрузчик скачивается при сборке из https://github.com/limine-bootloader/limine.
Его файл LICENSE включается в ISO и комплект для USB как `limine-license.txt`.
Лицензия PCG включается как `pcg-license.txt`, описание изменений — `pcg-info.txt`.

Для сборки используются Git, Python, Zig и pycdlib; для проверки — QEMU и
прошивки из его комплекта. Эти инструменты в репозиторий не включены.
В `legacy/buildroot/` сохранён отдельный старый прототип на Linux и Buildroot.
