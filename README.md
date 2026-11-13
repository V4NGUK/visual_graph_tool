# Граф зависимостей npm

Практическая работа № 2, вариант 8. Этапы 1–5 на C++17:
метаданные, рекурсивный BFS, обратные зависимости, Graphviz, SVG и ASCII-дерево.

## Сборка и тесты

Нужны Windows, C++17, CMake и [Graphviz](https://graphviz.org/download/).
Папку Graphviz `bin` добавь в `PATH` перед сборкой.
Подготовленная локальная копия в `build/tools/graphviz` также находится автоматически.

В терминале PowerShell из папки проекта:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -RunTests
```

## Запуск

```powershell
.\run.bat --package chalk --version 4.1.2 --ascii --output chalk.svg
.\run.bat --package ansi-styles --version 4.3.0 --ascii --output ansi-styles.svg
.\run.bat --package is-number --version 7.0.0 --ascii --output is-number.svg
```

Создаются SVG и соседний `.dot` с тем же именем. `--ascii` выводит дерево;
`[cycle]` обозначает цикл, `[seen]` — уже раскрытый узел.
`--direct` и `--reverse ИМЯ` выводят только текст без создания изображения.
Справка: `.\run.bat --help`.

Учитывается поле `dependencies`; выбирается наибольшая подходящая версия.
Обратные зависимости ищутся внутри графа исходного пакета.

## Материалы для защиты

1. [Устройство проекта и все параметры](docs/01_project.txt).
2. [Алгоритмы и разбор кода](docs/02_algorithms.txt).
3. [Демонстрация, сравнение с npm и вопросы](docs/03_defense.txt).
