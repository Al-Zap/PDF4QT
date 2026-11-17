# Working on this fork

This repository is a personal Windows PDF app built on [PDF4QT](https://github.com/JakubMelka/PDF4QT) by Jakub Melka (MIT). The goal of v1 is to beat Kofax Power PDF at three jobs: viewing plan sets, measuring on them (with fixture counts), and filling spec sheets, plus combining/organizing pages and markup. Everything else stays available but tucked under an Advanced menu.

The upstream [CLAUDE.md](CLAUDE.md) and [AGENTS.md](AGENTS.md) still apply: build after every change, run the unit tests, keep CRLF line endings, and stay warning-free.

## Getting a Windows build

Every push to `master` and every pull request runs [.github/workflows/windows-build.yml](.github/workflows/windows-build.yml) on `windows-2022` (MSVC, Qt 6.11.3, vcpkg dependencies). The run uploads the artifact `PDF4QT-Windows-<version>.zip`. Download it from the run page, unzip it anywhere, and start `Pdf4QtEditor.exe`. Nothing is installed.

- The first run builds every vcpkg dependency and takes about 1.5 to 2 hours. Later runs reuse the vcpkg and Qt caches and are much faster.
- Run it by hand from the Actions tab with "Run workflow" on any branch.
- The upstream MSI/MSIX packaging is still in [WindowsInstall.yml](.github/workflows/WindowsInstall.yml) (manual). The upstream Ubuntu + Windows matrix [ci.yml](.github/workflows/ci.yml) is manual only in this fork, so pushes don't build Windows twice.

To build locally on Windows, follow the configure line in [CLAUDE.md](CLAUDE.md) with Visual Studio 2022, Qt 6.11 (msvc2022_64) and vcpkg. The `Build project` step of the workflow is the exact known-good command line.

## Branches and pull requests

- `master` is always buildable. Each feature goes on its own branch (`feature/<name>`) and lands by pull request once the Windows build is green.
- Keep a feature inside its area below, so parallel work doesn't collide. Changes to shared files (main window `.ui`, `pdfprogramcontroller.cpp`, top-level `CMakeLists.txt`) should be small and focused.
- Don't record fork work in upstream's `RELEASES.txt`; describe it in the pull request.

## Where things live

The app we ship is **Pdf4QtEditor** (`Pdf4QtEditor/main.cpp` is a thin launcher). Almost all of its code is in the libraries and plugins:

| Folder | What it holds |
| --- | --- |
| `Pdf4QtLibCore/sources` | PDF parsing, rendering, annotations, forms, measurement dictionaries, OCR core. No widgets. |
| `Pdf4QtLibWidgets/sources` | The page view widget, rendering cache, interactive tools (markup, form filling). |
| `Pdf4QtLibGui` | Main windows, menus/toolbars/actions, sidebar, settings dialogs. |
| `Pdf4QtEditorPlugins/*` | Plugins loaded by the editor (measuring is one). Each has its own `CMakeLists.txt` and `<Name>Plugin.json`. |
| `Pdf4QtPageMaster` | Separate page organizing app (merge, reorder, split). |
| `Pdf4QtViewer`, `Pdf4QtDiff`, `Pdf4QtLaunchPad`, `PdfTool` | Other upstream apps; not part of v1. |
| `UnitTests` | QtTest suites. |
| `WixInstaller`, `cmake`, `vcpkg*` | Packaging and dependency setup. |

### v1 feature map

**Everyday viewer (speed, tabs, thumbnails, zoom/pan, search, printing)**
- Page layout, zoom and scrolling: `Pdf4QtLibWidgets/sources/pdfdrawspacecontroller.*` (`PDFDrawSpaceController`, `PDFDrawWidgetProxy`) and `pdfdrawwidget.*` (mouse wheel and drag handling).
- Rendering speed and the page cache: `Pdf4QtLibWidgets/sources/pdfcompiler.*` (`PDFAsynchronousPageCompiler`, background page preparation) and `Pdf4QtLibCore/sources/pdfrenderer.*`, `pdfpainter.*`, `pdfpagecontentprocessor.*`. The preview-then-sharpen and preload-next-sheet work goes here.
- Thumbnails and bookmarks sidebar: `Pdf4QtLibGui/pdfsidebarwidget.*`, `pdfbookmarkmanager.*`.
- Main window, actions, recent files, settings: `Pdf4QtLibGui/pdfeditormainwindow.*`, `pdfprogramcontroller.*` (creates every action and menu), `pdfrecentfilemanager.*`, `pdfviewersettings.*`.
- Search: `Pdf4QtLibGui/pdfadvancedfindwidget.*`, text layout in `Pdf4QtLibCore/sources/pdftextlayout*`.
- File association, Start menu, installer: `WixInstaller/`.

**Scale, measuring and fixture count**
- Existing measuring plugin: `Pdf4QtEditorPlugins/DimensionsPlugin` (`dimensiontool.*` tools, `scaledialog.*` and `scalepresetsdialog.*` scale and calibration, `dimensionunits.*`, CSV export and convert-to-annotations in `dimensionsplugin.cpp`).
- Scale read from the drawing (PDF measure/viewport dictionaries) and number formats: `Pdf4QtLibCore/sources/pdfmeasure.*`.
- Snapping to line ends and corners: `Pdf4QtLibCore/sources/pdfsnapper.*`.
- Saving measurements as standard PDF measurement annotations (readable in Kofax/Bluebeam): `Pdf4QtLibCore/sources/pdfannotation.*` and `pdfdocumentbuilder.*`.
- **Fixture count (new):** add a new plugin at `Pdf4QtEditorPlugins/FixtureCountPlugin`, modelled on DimensionsPlugin, and register it in `Pdf4QtEditorPlugins/CMakeLists.txt`.

**Spec sheet forms**
- Form model, field values, appearance generation, flattening: `Pdf4QtLibCore/sources/pdfform.*`, `pdfformfieldformat.*`.
- Interactive filling, Tab order, editing widgets: `Pdf4QtLibWidgets/sources/pdfwidgetformmanager.*`, `pdftexteditpseudowidget.*`.
- Save / Save As / recovery copy: `Pdf4QtLibGui/pdfprogramcontroller.*`, writing in `Pdf4QtLibCore/sources/pdfdocumentwriter.*`.

**Combine and organize pages**
- Page operations engine: `Pdf4QtLibCore/sources/pdfdocumentmanipulator.*`.
- Existing UI to borrow from: `Pdf4QtPageMaster` (`pageitemmodel.*` drag-and-drop model, `pageitemdelegate.*`, `mainwindow.*`).
- Bringing drag-to-reorder and drop-to-combine into the editor's thumbnail strip: `Pdf4QtLibGui/pdfsidebarwidget.*`.

**Markup**
- Annotation creation tools (highlight, free text, sticky note, line/arrow, rectangle, ellipse, freehand, stamp): `Pdf4QtLibWidgets/sources/pdfadvancedtools.*`. The revision cloud tool goes here too.
- Tool framework: `Pdf4QtLibWidgets/sources/pdfwidgettool.*`; annotation drawing and objects: `Pdf4QtLibCore/sources/pdfannotation.*`, `pdfannotationmanipulator.*`; style editing: `Pdf4QtLibWidgets/sources/pdfannotationstyle.*`.
- Comments list: `Pdf4QtLibWidgets/sources/pdfitemmodels.*` and the sidebar.

**OCR (basic in v1 under Advanced, polished in v2)**
- Core: `Pdf4QtLibCore/sources/pdfocr*`; Tesseract engine: `Pdf4QtOcrTesseract`; dialogs: `Pdf4QtLibGui/pdfocr*`; models: `ocr/`.

**Clean toolbar and Advanced menu**
- Actions, menus and toolbars are built in `Pdf4QtLibGui/pdfprogramcontroller.*` and `pdfeditormainwindow.ui`. Plugins add their own actions through `Pdf4QtLibCore/sources/pdfplugin.*`.
