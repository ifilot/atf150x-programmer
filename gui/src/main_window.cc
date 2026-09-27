// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Main window layout, programmer state machine and user commands.
 */
#include "gui/src/main_window.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

#include "cli/src/programmer.h"
#include "cli/src/serial.h"
#include "firmware/atf1502_programmer/version.h"
#include "gui/src/fuse_map_widget.h"
#include "gui/src/update_checker.h"

namespace atf {
namespace gui {
namespace {

constexpr int kMaxRecentFiles = 8;
constexpr char kFirmwareFile[] = "firmware/atf150x-leonardo-firmware.hex";

/**
 * @brief Draws a small colored square for legend entries.
 *
 * @param[in] color Fill color.
 * @return Icon with a thin darker border.
 */
QIcon Swatch(const QColor& color) {
    QPixmap pixmap(14, 14);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setPen(color.darker(140));
    painter.setBrush(color);
    painter.drawRect(0, 0, 13, 13);
    return QIcon(pixmap);
}

/**
 * @brief Formats a colored status dot followed by text.
 *
 * @param[in] color Dot color.
 * @param[in] text Plain text; escaped for rich text.
 * @return Rich-text label contents.
 */
QString StatusText(const QColor& color, const QString& text) {
    return QStringLiteral("<span style=\"color:%1\">&#9679;</span>&nbsp;%2")
        .arg(color.name(), text.toHtmlEscaped());
}

/**
 * @brief Creates a selectable value label for the summary forms.
 *
 * @return Label that elides nothing and allows copying.
 */
QLabel* ValueLabel() {
    auto* label = new QLabel(QStringLiteral("–"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    return label;
}

/**
 * @brief Tests whether a path names a JEDEC file by its extension.
 *
 * @param[in] path Candidate path.
 * @return True for ".jed", in any letter case.
 */
bool IsJedecPath(const QString& path) {
    return path.endsWith(QStringLiteral(".jed"), Qt::CaseInsensitive);
}

/**
 * @brief Log view that starts a few lines tall instead of Qt's default.
 */
class LogView : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;

    /**
     * @brief Requests room for about six lines.
     *
     * @return Preferred size of the log dock contents.
     */
    QSize sizeHint() const override {
        return QSize(400, fontMetrics().lineSpacing() * 6 + 8);
    }
};

const QColor kGreen(0x16, 0xa3, 0x4a);
const QColor kAmber(0xd9, 0x77, 0x06);
const QColor kRed(0xdc, 0x26, 0x26);
const QColor kGray(0x9c, 0xa3, 0xaf);

}  // namespace

/**
 * @brief Builds widgets, starts the worker thread and schedules discovery.
 */
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      settings_(QStringLiteral("atf150x-programmer"),
                QStringLiteral("ATF150x Programmer")) {
    qRegisterMetaType<ProbeResult>();
    setWindowTitle(tr("ATF150x Programmer %1").arg(kVersion));
    setAcceptDrops(true);

    worker_ = new ProgrammerWorker();
    worker_->moveToThread(&worker_thread_);
    connect(&worker_thread_, &QThread::finished, worker_,
            &QObject::deleteLater);
    connect(worker_, &ProgrammerWorker::ProbeFinished, this,
            &MainWindow::OnProbeFinished);
    connect(worker_, &ProgrammerWorker::Progress, this,
            &MainWindow::OnProgress);
    connect(worker_, &ProgrammerWorker::Log, this, &MainWindow::AppendLog);
    connect(worker_, &ProgrammerWorker::DeviceIdentified, this,
            [this](const QString& idcode) {
                idcode_ = idcode;
                UpdateDevicePanel();
            });
    connect(worker_, &ProgrammerWorker::OperationFinished, this,
            &MainWindow::OnOperationFinished);
    connect(worker_, &ProgrammerWorker::FirmwareFinished, this,
            &MainWindow::OnFirmwareFinished);
    worker_thread_.start();

    update_checker_ = new UpdateChecker(this);
    connect(update_checker_, &UpdateChecker::UpdateAvailable, this,
            &MainWindow::OnUpdateAvailable);
    connect(update_checker_, &UpdateChecker::UpToDate, this,
            &MainWindow::OnUpToDate);
    connect(update_checker_, &UpdateChecker::CheckFailed, this,
            &MainWindow::OnUpdateCheckFailed);

    CreateActions();
    CreateMenus();
    CreateToolBar();
    CreateCentralWidget();
    CreateLogDock();

    progress_caption_ = new QLabel(this);
    progress_ = new QProgressBar(this);
    progress_->setMaximumWidth(260);
    progress_->setTextVisible(true);
    statusBar()->addPermanentWidget(progress_caption_);
    statusBar()->addPermanentWidget(progress_);
    progress_caption_->hide();
    progress_->hide();

    RestoreSettings();
    UpdateProgrammerPanel();
    UpdateDevicePanel();
    UpdateDocumentPanel();
    UpdateActions();

    AppendLog(tr("ATF150x Programmer %1 started.").arg(kVersion));
    if (BundledFirmwarePath().isEmpty()) {
        AppendLog(
            tr("No bundled Leonardo firmware found; firmware "
               "installation is unavailable."));
    }

    port_timer_ = new QTimer(this);
    port_timer_->setInterval(1000);
    connect(port_timer_, &QTimer::timeout, this,
            [this] { RefreshPorts(false); });
    port_timer_->start();
    QTimer::singleShot(0, this, [this] { RefreshPorts(true); });

    if (startup_check_action_->isChecked()) {
        QTimer::singleShot(2000, this,
                           [this] { update_checker_->Check(false); });
    }
}

/**
 * @brief Saves window state and joins the worker thread.
 */
MainWindow::~MainWindow() {
    worker_thread_.quit();
    worker_thread_.wait();
}

/**
 * @brief Creates all commands with shortcuts and standard icons.
 */
void MainWindow::CreateActions() {
    QStyle* s = style();
    open_action_ = new QAction(s->standardIcon(QStyle::SP_DialogOpenButton),
                               tr("&Open JEDEC…"), this);
    open_action_->setShortcut(QKeySequence::Open);
    connect(open_action_, &QAction::triggered, this,
            &MainWindow::ShowOpenDialog);

    reload_action_ = new QAction(s->standardIcon(QStyle::SP_BrowserReload),
                                 tr("&Reload"), this);
    reload_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    connect(reload_action_, &QAction::triggered, this, [this] {
        if (!document_.empty()) {
            OpenFile(document_.path());
        }
    });

    identify_action_ = new QAction(
        s->standardIcon(QStyle::SP_FileDialogInfoView), tr("&Identify"), this);
    identify_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    identify_action_->setToolTip(tr("Read the IDCODE of the socketed CPLD"));
    connect(identify_action_, &QAction::triggered, this,
            [this] { StartTask(Task::kIdentify); });

    program_action_ = new QAction(QIcon(QStringLiteral(":/icons/program.png")),
                                  tr("&Program"), this);
    program_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    program_action_->setToolTip(
        tr("Erase, program and verify the CPLD with the open JEDEC file"));
    connect(program_action_, &QAction::triggered, this,
            [this] { StartTask(Task::kFlash); });

    verify_action_ = new QAction(s->standardIcon(QStyle::SP_DialogApplyButton),
                                 tr("&Verify"), this);
    verify_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    verify_action_->setToolTip(tr("Compare the CPLD with the open JEDEC file"));
    connect(verify_action_, &QAction::triggered, this,
            [this] { StartTask(Task::kVerify); });

    erase_action_ =
        new QAction(s->standardIcon(QStyle::SP_TrashIcon), tr("E&rase"), this);
    erase_action_->setToolTip(tr("Erase and blank-check the CPLD"));
    connect(erase_action_, &QAction::triggered, this,
            [this] { StartTask(Task::kErase); });

    firmware_action_ =
        new QAction(QIcon(QStringLiteral(":/icons/firmware.png")),
                    tr("Install &Firmware…"), this);
    firmware_action_->setToolTip(
        tr("Install the bundled programmer firmware on the Leonardo"));
    connect(firmware_action_, &QAction::triggered, this,
            &MainWindow::InstallFirmware);

    refresh_action_ = new QAction(s->standardIcon(QStyle::SP_BrowserReload),
                                  tr("Re&connect"), this);
    refresh_action_->setShortcut(QKeySequence(Qt::Key_F5));
    refresh_action_->setToolTip(tr("Search for the programmer again"));
    connect(refresh_action_, &QAction::triggered, this, [this] {
        prompted_ports_.clear();
        RefreshPorts(true);
    });

    zoom_in_action_ = new QAction(tr("Zoom &In"), this);
    zoom_in_action_->setShortcut(QKeySequence::ZoomIn);
    connect(zoom_in_action_, &QAction::triggered, this,
            [this] { map_->SetZoom(map_->zoom() + 1); });
    zoom_out_action_ = new QAction(tr("Zoom &Out"), this);
    zoom_out_action_->setShortcut(QKeySequence::ZoomOut);
    connect(zoom_out_action_, &QAction::triggered, this,
            [this] { map_->SetZoom(map_->zoom() - 1); });
    zoom_fit_action_ = new QAction(tr("&Fit to Window"), this);
    zoom_fit_action_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(zoom_fit_action_, &QAction::triggered, this,
            [this] { map_->SetFitToWindow(); });

    startup_check_action_ =
        new QAction(tr("Check for Updates at &Startup"), this);
    startup_check_action_->setCheckable(true);
    startup_check_action_->setChecked(
        settings_.value(QStringLiteral("check_updates"), true).toBool());
    connect(startup_check_action_, &QAction::toggled, this, [this](bool on) {
        settings_.setValue(QStringLiteral("check_updates"), on);
    });
}

/**
 * @brief Builds the File, Device, View and Help menus.
 */
void MainWindow::CreateMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(open_action_);
    recent_menu_ = file->addMenu(tr("Open &Recent"));
    file->addAction(reload_action_);
    file->addSeparator();
    QAction* quit = file->addAction(tr("E&xit"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    RebuildRecentMenu();

    QMenu* device = menuBar()->addMenu(tr("&Device"));
    device->addAction(identify_action_);
    device->addAction(program_action_);
    device->addAction(verify_action_);
    device->addAction(erase_action_);
    device->addSeparator();
    device->addAction(refresh_action_);
    device->addAction(firmware_action_);

    QMenu* view = menuBar()->addMenu(tr("&View"));
    view->addAction(zoom_in_action_);
    view->addAction(zoom_out_action_);
    view->addAction(zoom_fit_action_);
    view->setObjectName(QStringLiteral("view_menu"));

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Check for &Updates"), this,
                    [this] { update_checker_->Check(true); });
    help->addAction(startup_check_action_);
    help->addSeparator();
    help->addAction(tr("Project &Website"), this, [] {
        QDesktopServices::openUrl(QUrl(
            QStringLiteral("https://github.com/ifilot/atf150x-programmer")));
    });
    help->addAction(tr("&About ATF150x Programmer"), this,
                    &MainWindow::ShowAbout);
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

/**
 * @brief Adds the main commands with captions beside their icons.
 */
void MainWindow::CreateToolBar() {
    QToolBar* bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("main_toolbar"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->addAction(open_action_);
    bar->addSeparator();
    bar->addAction(identify_action_);
    bar->addAction(program_action_);
    bar->addAction(verify_action_);
    bar->addAction(erase_action_);
    bar->addSeparator();
    bar->addAction(firmware_action_);
}

/**
 * @brief Places the update banner above a side panel / fuse map splitter.
 */
void MainWindow::CreateCentralWidget() {
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 0);

    update_banner_ = new QFrame(central);
    update_banner_->setObjectName(QStringLiteral("update_banner"));
    update_banner_->setStyleSheet(QStringLiteral(
        "#update_banner { background: #fff8c5; border: 1px solid #e5d48a; }"));
    auto* banner_layout = new QHBoxLayout(update_banner_);
    banner_layout->setContentsMargins(8, 4, 4, 4);
    update_label_ = new QLabel(update_banner_);
    update_label_->setOpenExternalLinks(true);
    banner_layout->addWidget(update_label_, 1);
    auto* close_banner = new QToolButton(update_banner_);
    close_banner->setAutoRaise(true);
    close_banner->setIcon(
        style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    connect(close_banner, &QToolButton::clicked, update_banner_,
            &QWidget::hide);
    banner_layout->addWidget(close_banner);
    update_banner_->hide();
    layout->addWidget(update_banner_);

    auto* splitter = new QSplitter(Qt::Horizontal, central);
    splitter->setObjectName(QStringLiteral("main_splitter"));
    splitter->addWidget(CreateSidePanel());
    splitter->addWidget(CreateMapPanel());
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({330, 870});
    layout->addWidget(splitter, 1);
    setCentralWidget(central);
}

/**
 * @brief Programmer, target, JEDEC summary and legend group boxes.
 */
QWidget* MainWindow::CreateSidePanel() {
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 4, 0);

    auto* programmer = new QGroupBox(tr("Programmer"), panel);
    auto* programmer_layout = new QVBoxLayout(programmer);
    auto* port_row = new QHBoxLayout();
    port_combo_ = new QComboBox(programmer);
    port_combo_->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    port_combo_->setMinimumContentsLength(18);
    connect(port_combo_, &QComboBox::activated, this,
            [this](int) { OnPortSelected(); });
    auto* refresh = new QToolButton(programmer);
    refresh->setDefaultAction(refresh_action_);
    port_row->addWidget(port_combo_, 1);
    port_row->addWidget(refresh);
    programmer_layout->addLayout(port_row);
    programmer_status_ = new QLabel(programmer);
    programmer_status_->setWordWrap(true);
    programmer_layout->addWidget(programmer_status_);
    firmware_label_ = new QLabel(programmer);
    firmware_label_->setWordWrap(true);
    programmer_layout->addWidget(firmware_label_);
    install_button_ = new QPushButton(programmer);
    connect(install_button_, &QPushButton::clicked, this,
            &MainWindow::InstallFirmware);
    programmer_layout->addWidget(install_button_);
    result_label_ = new QLabel(programmer);
    result_label_->setWordWrap(true);
    result_label_->hide();
    programmer_layout->addWidget(result_label_);
    layout->addWidget(programmer);

    auto* target = new QGroupBox(tr("Target device"), panel);
    auto* target_form = new QFormLayout(target);
    device_label_ = ValueLabel();
    idcode_label_ = ValueLabel();
    match_label_ = new QLabel(target);
    match_label_->setWordWrap(true);
    target_form->addRow(tr("Device:"), device_label_);
    target_form->addRow(tr("IDCODE:"), idcode_label_);
    target_form->addRow(match_label_);
    layout->addWidget(target);

    auto* jedec = new QGroupBox(tr("JEDEC file"), panel);
    auto* jedec_form = new QFormLayout(jedec);
    file_label_ = ValueLabel();
    jedec_device_label_ = ValueLabel();
    fuses_label_ = ValueLabel();
    pterms_label_ = ValueLabel();
    macrocells_label_ = ValueLabel();
    checksum_label_ = ValueLabel();
    arming_label_ = ValueLabel();
    jtag_label_ = ValueLabel();
    ues_label_ = ValueLabel();
    jedec_form->addRow(tr("File:"), file_label_);
    jedec_form->addRow(tr("Device:"), jedec_device_label_);
    jedec_form->addRow(tr("Programmed:"), fuses_label_);
    jedec_form->addRow(tr("Product terms:"), pterms_label_);
    jedec_form->addRow(tr("Macrocells:"), macrocells_label_);
    jedec_form->addRow(tr("Checksum:"), checksum_label_);
    jedec_form->addRow(tr("Outputs:"), arming_label_);
    jedec_form->addRow(tr("JTAG:"), jtag_label_);
    jedec_form->addRow(tr("Signature:"), ues_label_);
    layout->addWidget(jedec);

    layout->addStretch(1);

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(300);
    return scroll;
}

/**
 * @brief Fuse map with zoom controls and a hover description line.
 */
QWidget* MainWindow::CreateMapPanel() {
    auto* panel = new QGroupBox(tr("Fuse map"), this);
    auto* layout = new QVBoxLayout(panel);

    auto* controls = new QHBoxLayout();
    auto* caption = new QLabel(
        tr("Columns are physical Flash rows, cells are bits of each row. "
           "Ctrl+wheel zooms."),
        panel);
    caption->setForegroundRole(QPalette::PlaceholderText);
    controls->addWidget(caption, 1);
    auto add_button = [&](QAction* action) {
        auto* button = new QToolButton(panel);
        button->setDefaultAction(action);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        controls->addWidget(button);
    };
    add_button(zoom_out_action_);
    zoom_label_ = new QLabel(panel);
    zoom_label_->setMinimumWidth(64);
    zoom_label_->setAlignment(Qt::AlignCenter);
    controls->addWidget(zoom_label_);
    add_button(zoom_in_action_);
    add_button(zoom_fit_action_);
    layout->addLayout(controls);

    map_ = new FuseMapWidget();
    map_->SetRegionColors(
        settings_.value(QStringLiteral("region_colors"), true).toBool());
    map_->SetMuteUnusedTerms(
        settings_.value(QStringLiteral("mute_unused_terms"), true).toBool());
    map_scroll_ = new QScrollArea(panel);
    map_scroll_->setWidget(map_);
    map_scroll_->setWidgetResizable(true);
    map_scroll_->setBackgroundRole(QPalette::Base);
    layout->addWidget(map_scroll_, 1);
    connect(map_, &FuseMapWidget::ZoomChanged, this,
            &MainWindow::UpdateZoomLabel);

    hover_label_ = new QLabel(panel);
    hover_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hover_label_->setMinimumHeight(
        QFontMetrics(hover_label_->font()).height() * 2 + 4);
    hover_label_->setWordWrap(true);
    hover_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    connect(
        map_, &FuseMapWidget::CellHovered, this, [this](const QString& text) {
            hover_label_->setText(text.isEmpty()
                                      ? tr("Hover over a cell to see its fuse.")
                                      : text);
        });
    hover_label_->setText(tr("Hover over a cell to see its fuse."));
    layout->addWidget(hover_label_);

    // Legend: one checkable swatch per region, three per row.
    auto* legend = new QGridLayout();
    legend_layout_ = legend;
    legend->setHorizontalSpacing(18);
    legend->setVerticalSpacing(2);
    int slot = 0;
    for (int i = 0; i < kRegionCount; ++i) {
        Region region = static_cast<Region>(i);
        if (region == Region::kReserved) {
            continue;  // Reserved fuses have no physical cell.
        }
        auto* box = new QCheckBox(RegionName(region), panel);
        box->setIcon(Swatch(RegionColor(region)));
        box->setChecked(true);
        box->setToolTip(tr("Uncheck to dim this region in the fuse map"));
        connect(box, &QCheckBox::toggled, this, [this, region](bool on) {
            map_->SetRegionVisible(region, on);
        });
        auto* count = new QLabel(panel);
        count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        count->setToolTip(tr("Programmed fuses / total fuses"));
        count->setForegroundRole(QPalette::PlaceholderText);
        legend_boxes_[i] = box;
        legend_counts_[i] = count;
        legend->addWidget(box, slot / 3, (slot % 3) * 2);
        legend->addWidget(count, slot / 3, (slot % 3) * 2 + 1);
        ++slot;
    }
    for (int column = 0; column < 6; column += 2) {
        legend->setColumnStretch(column, 1);
    }
    layout->addLayout(legend);

    auto* options = new QHBoxLayout();
    region_colors_box_ = new QCheckBox(tr("Region colors"), panel);
    region_colors_box_->setChecked(
        settings_.value(QStringLiteral("region_colors"), true).toBool());
    connect(region_colors_box_, &QCheckBox::toggled, this, [this](bool on) {
        map_->SetRegionColors(on);
        settings_.setValue(QStringLiteral("region_colors"), on);
    });
    options->addWidget(region_colors_box_);
    auto* mute = new QCheckBox(tr("Mute unused product terms"), panel);
    mute->setToolTip(
        tr("Compilers disable unused product terms by connecting every "
           "input. Muting them makes the terms in use stand out."));
    mute->setChecked(
        settings_.value(QStringLiteral("mute_unused_terms"), true).toBool());
    connect(mute, &QCheckBox::toggled, this, [this](bool on) {
        map_->SetMuteUnusedTerms(on);
        settings_.setValue(QStringLiteral("mute_unused_terms"), on);
    });
    options->addWidget(mute);
    options->addStretch(1);
    auto* hint = new QLabel(
        tr("Solid: programmed (0) · pale: erased (1) · muted: unused term"),
        panel);
    hint->setForegroundRole(QPalette::PlaceholderText);
    options->addWidget(hint);
    layout->addLayout(options);
    UpdateZoomLabel(map_->zoom(), map_->fit_to_window());
    return panel;
}

/**
 * @brief Session log in a bottom dock that can be hidden.
 */
void MainWindow::CreateLogDock() {
    auto* dock = new QDockWidget(tr("Log"), this);
    dock->setObjectName(QStringLiteral("log_dock"));
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable);
    log_ = new LogView(dock);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    dock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    if (QMenu* view =
            menuBar()->findChild<QMenu*>(QStringLiteral("view_menu"))) {
        view->addSeparator();
        view->addAction(dock->toggleViewAction());
    }
}

/**
 * @brief Restores geometry and dock layout from the previous session.
 */
void MainWindow::RestoreSettings() {
    if (!restoreGeometry(
            settings_.value(QStringLiteral("geometry")).toByteArray())) {
        resize(1200, 800);
    }
    restoreState(settings_.value(QStringLiteral("window_state")).toByteArray());
}

/**
 * @brief Re-reads the USB port list and probes a changed selection.
 */
void MainWindow::RefreshPorts(bool force_probe) {
    if (worker_active_) {
        return;
    }
    QList<LeonardoPort> ports = FindLeonardoPorts();
    if (ports == ports_ && !force_probe) {
        return;
    }
    QString previous;
    if (const LeonardoPort* selected = SelectedPort()) {
        previous = selected->name;
    }
    QSet<QString> names;
    for (const LeonardoPort& port : ports) {
        names.insert(port.name);
    }
    // Replugging a board offers the firmware installation again.
    prompted_ports_.intersect(names);
    for (const LeonardoPort& port : ports) {
        if (!ports_.contains(port)) {
            AppendLog(tr("Found %1.").arg(PortLabel(port)));
        }
    }
    ports_ = ports;

    port_combo_->clear();
    int selection = -1;
    for (int i = 0; i < ports_.size(); ++i) {
        port_combo_->addItem(PortLabel(ports_[i]));
        if (ports_[i].name == previous && selection < 0) {
            selection = i;
        }
    }
    if (selection < 0) {
        // Prefer a running sketch over a bootloader that is about to exit.
        for (int i = 0; i < ports_.size() && selection < 0; ++i) {
            if (!ports_[i].bootloader) {
                selection = i;
            }
        }
        if (selection < 0 && !ports_.isEmpty()) {
            selection = 0;
        }
    }
    if (ports_.isEmpty()) {
        port_combo_->addItem(tr("No Arduino Leonardo found"));
    }
    port_combo_->setCurrentIndex(std::max(selection, 0));

    const LeonardoPort* selected = SelectedPort();
    bool changed = selected == nullptr || selected->name != previous ||
                   !probe_valid_ || probe_.port != selected->name;
    if (force_probe || changed) {
        OnPortSelected();
    }
}

/**
 * @brief Returns the port behind the current combo box entry.
 */
const LeonardoPort* MainWindow::SelectedPort() const {
    int index = port_combo_->currentIndex();
    if (index < 0 || index >= ports_.size()) {
        return nullptr;
    }
    return &ports_[index];
}

/**
 * @brief Probes a sketch port; bootloader ports cannot answer HELLO.
 */
void MainWindow::OnPortSelected() {
    if (worker_active_) {
        return;
    }
    probe_valid_ = false;
    idcode_.clear();
    const LeonardoPort* port = SelectedPort();
    if (port == nullptr || port->bootloader) {
        UpdateProgrammerPanel();
        UpdateDevicePanel();
        UpdateActions();
        return;
    }
    worker_active_ = true;
    QString name = port->name;
    programmer_status_->setText(
        StatusText(kGray, tr("Checking the programmer on %1…").arg(name)));
    UpdateActions();
    QMetaObject::invokeMethod(
        worker_, [worker = worker_, name] { worker->Probe(name); },
        Qt::QueuedConnection);
}

/**
 * @brief Records the handshake result and offers firmware when needed.
 */
void MainWindow::OnProbeFinished(const ProbeResult& result) {
    worker_active_ = false;
    probe_ = result;
    probe_valid_ = true;
    idcode_ = result.idcode;
    switch (result.state) {
        case FirmwareState::kCompatible:
            AppendLog(tr("Programmer on %1 runs compatible firmware (%2).")
                          .arg(result.port, result.hello));
            break;
        case FirmwareState::kDifferentVersion:
            AppendLog(tr("Programmer on %1 runs %2; this application "
                         "needs %3.")
                          .arg(result.port, result.hello,
                               QString::fromStdString(ExpectedHello())));
            break;
        case FirmwareState::kNoResponse:
            AppendLog(tr("The Leonardo on %1 did not answer the programmer "
                         "protocol (%2).")
                          .arg(result.port, result.error));
            break;
        case FirmwareState::kPortError:
            AppendLog(result.error);
            break;
    }
    if (result.state == FirmwareState::kCompatible && !result.error.isEmpty()) {
        AppendLog(tr("Reading the IDCODE failed: %1").arg(result.error));
    }
    UpdateProgrammerPanel();
    UpdateDevicePanel();
    UpdateActions();
    OfferFirmwareInstall(result);
}

/**
 * @brief Asks once per connection before replacing another sketch.
 */
void MainWindow::OfferFirmwareInstall(const ProbeResult& result) {
    if (result.state != FirmwareState::kDifferentVersion &&
        result.state != FirmwareState::kNoResponse) {
        return;
    }
    if (prompted_ports_.contains(result.port) ||
        !firmware_action_->isEnabled()) {
        return;
    }
    prompted_ports_.insert(result.port);
    QString reason =
        result.state == FirmwareState::kDifferentVersion
            ? tr("The programmer on %1 runs firmware \"%2\", but this "
                 "application requires \"%3\".")
                  .arg(result.port, result.hello,
                       QString::fromStdString(ExpectedHello()))
            : tr("The Arduino Leonardo on %1 does not run the ATF150x "
                 "programmer firmware. It may be blank or run another "
                 "sketch.")
                  .arg(result.port);
    QMessageBox box(QMessageBox::Question, tr("Install programmer firmware"),
                    reason + QStringLiteral("\n\n") +
                        tr("Install the bundled firmware v%1 now? The current "
                           "sketch is replaced; the Arduino bootloader is "
                           "kept.")
                            .arg(kVersion),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() == QMessageBox::Yes) {
        InstallFirmware();
    }
}

/**
 * @brief True when the selected port runs matching firmware.
 */
bool MainWindow::ProgrammerReady() const {
    const LeonardoPort* port = SelectedPort();
    return port != nullptr && !port->bootloader && probe_valid_ &&
           probe_.port == port->name &&
           probe_.state == FirmwareState::kCompatible;
}

/**
 * @brief Validates preconditions, then hands the task to the worker.
 */
void MainWindow::StartTask(Task task) {
    if (!ProgrammerReady() || worker_active_) {
        return;
    }
    Device expected = Device::kUnknown;
    Image image;
    if (task == Task::kFlash || task == Task::kVerify) {
        if (document_.empty() || !ReloadIfChanged()) {
            return;
        }
        expected = document_.device();
        image = document_.image();
    }
    if (task == Task::kErase) {
        auto answer = QMessageBox::warning(
            this, tr("Erase device"),
            tr("Erase the CPLD in the socket? Its current design is "
               "destroyed."),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    static const char* const kCaptions[] = {
        QT_TR_NOOP("Identifying"), QT_TR_NOOP("Erasing"),
        QT_TR_NOOP("Programming"), QT_TR_NOOP("Verifying")};
    QString caption = tr(kCaptions[static_cast<int>(task)]);
    if (task == Task::kFlash || task == Task::kVerify) {
        AppendLog(tr("%1 %2 with %3…")
                      .arg(caption, QString::fromLatin1(DeviceName(expected)),
                           QDir::toNativeSeparators(document_.path())));
    } else {
        AppendLog(caption + QStringLiteral("…"));
    }
    SetBusy(true, caption);
    QString port = probe_.port;
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, task, port, expected, image] {
            worker->Execute(task, port, expected, image);
        },
        Qt::QueuedConnection);
}

/**
 * @brief Picks up a JEDEC file rebuilt since it was opened.
 */
bool MainWindow::ReloadIfChanged() {
    QFileInfo info(document_.path());
    if (!info.exists()) {
        QMessageBox::critical(
            this, tr("File missing"),
            tr("%1 no longer exists.")
                .arg(QDir::toNativeSeparators(document_.path())));
        return false;
    }
    if (info.lastModified() == document_modified_ &&
        info.size() == document_size_) {
        return true;
    }
    AppendLog(tr("%1 changed on disk; reloading.").arg(info.fileName()));
    return OpenFile(document_.path());
}

/**
 * @brief Shows the result and re-enables the user interface.
 */
void MainWindow::OnOperationFinished(bool ok, const QString& message) {
    SetBusy(false);
    AppendLog(ok ? message : tr("Failed: %1").arg(message));
    SetResult(ok, message);
    statusBar()->showMessage(message, 10000);
    UpdateDevicePanel();
    if (!ok) {
        QMessageBox::critical(this, tr("Operation failed"), message);
    }
    RefreshPorts(false);
}

/**
 * @brief Updates the status-bar progress indicator.
 */
void MainWindow::OnProgress(const QString& stage, int done, int total) {
    progress_caption_->setText(stage);
    if (total <= 0) {
        progress_->setRange(0, 0);
    } else {
        progress_->setRange(0, total);
        progress_->setValue(done);
        progress_->setFormat(QStringLiteral("%v / %m"));
    }
}

/**
 * @brief Confirms, then resets into the bootloader and runs AVRDUDE.
 */
void MainWindow::InstallFirmware() {
    const LeonardoPort* port = SelectedPort();
    QString hex = BundledFirmwarePath();
    QString config;
    QString avrdude = AvrdudePath(&config);
    if (port == nullptr || hex.isEmpty() || avrdude.isEmpty() ||
        worker_active_) {
        return;
    }
    auto answer = QMessageBox::question(
        this, tr("Install programmer firmware"),
        tr("Install ATF150x programmer firmware v%1 on %2?\n\nThe sketch "
           "currently on the Leonardo is replaced. Do not disconnect the "
           "USB cable until the installation finishes.")
            .arg(kVersion, port->name),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok);
    if (answer != QMessageBox::Ok) {
        return;
    }
    AppendLog(tr("Installing firmware v%1 on %2…").arg(kVersion, port->name));
    SetBusy(true, tr("Installing firmware"));
    LeonardoPort target = *port;
    QMetaObject::invokeMethod(
        worker_,
        [worker = worker_, target, hex, avrdude, config] {
            worker->InstallFirmware(target, hex, avrdude, config);
        },
        Qt::QueuedConnection);
}

/**
 * @brief Reports the installation and probes the returning programmer.
 */
void MainWindow::OnFirmwareFinished(bool ok, const QString& message,
                                    const QString& port) {
    SetBusy(false);
    AppendLog(ok ? message
                 : tr("Firmware installation failed: %1").arg(message));
    SetResult(ok, message);
    if (!ok) {
        QMessageBox::critical(
            this, tr("Firmware installation failed"),
            message + QStringLiteral("\n\n") +
                tr("The Arduino bootloader is protected; you can retry."));
    }
    // The sketch usually returns on its previous port; otherwise the first
    // sketch port is selected. Either way the new firmware is probed.
    Q_UNUSED(port);
    ports_.clear();
    RefreshPorts(true);
}

/**
 * @brief Locks commands and shows progress while the worker is busy.
 */
void MainWindow::SetBusy(bool busy, const QString& caption) {
    busy_ = busy;
    worker_active_ = busy;
    progress_caption_->setVisible(busy);
    progress_->setVisible(busy);
    if (busy) {
        progress_caption_->setText(caption);
        progress_->setRange(0, 0);
        result_label_->hide();
        QApplication::setOverrideCursor(Qt::BusyCursor);
    } else {
        QApplication::restoreOverrideCursor();
    }
    UpdateActions();
}

/**
 * @brief Enables each command according to programmer and file state.
 */
void MainWindow::UpdateActions() {
    bool ready = ProgrammerReady() && !worker_active_;
    Device socket = DeviceFromIdText(idcode_.toStdString());
    bool matches = document_.empty() || socket == Device::kUnknown ||
                   socket == document_.device();
    bool has_file = !document_.empty();
    identify_action_->setEnabled(ready);
    erase_action_->setEnabled(ready);
    program_action_->setEnabled(ready && has_file && matches);
    verify_action_->setEnabled(ready && has_file && matches);
    reload_action_->setEnabled(has_file);
    QString config;
    bool can_install = SelectedPort() != nullptr && !worker_active_ &&
                       !BundledFirmwarePath().isEmpty() &&
                       !AvrdudePath(&config).isEmpty();
    firmware_action_->setEnabled(can_install);
    install_button_->setEnabled(can_install);
    refresh_action_->setEnabled(!worker_active_);
    port_combo_->setEnabled(!worker_active_ && !ports_.isEmpty());
    bool map = has_file;
    zoom_in_action_->setEnabled(map);
    zoom_out_action_->setEnabled(map);
    zoom_fit_action_->setEnabled(map);
}

/**
 * @brief Describes the connection and firmware state in plain words.
 */
void MainWindow::UpdateProgrammerPanel() {
    const LeonardoPort* port = SelectedPort();
    install_button_->setText(tr("Install firmware v%1…").arg(kVersion));
    install_button_->setDefault(false);
    if (port == nullptr) {
        programmer_status_->setText(StatusText(
            kGray, tr("Connect the programmer (Arduino Leonardo) by USB.")));
        firmware_label_->setText(tr("Bundled firmware: v%1").arg(kVersion));
        return;
    }
    if (port->bootloader) {
        programmer_status_->setText(StatusText(
            kAmber,
            tr("The Leonardo on %1 is in bootloader mode.").arg(port->name)));
        firmware_label_->setText(
            tr("It starts its sketch within 8 seconds, or install the "
               "firmware now."));
        return;
    }
    if (!probe_valid_ || probe_.port != port->name) {
        programmer_status_->setText(StatusText(
            kGray, tr("Checking the programmer on %1…").arg(port->name)));
        firmware_label_->clear();
        return;
    }
    QString running = probe_.hello.section(QLatin1Char(' '), 2);
    switch (probe_.state) {
        case FirmwareState::kCompatible:
            programmer_status_->setText(StatusText(
                kGreen, tr("Programmer ready on %1.").arg(port->name)));
            firmware_label_->setText(
                tr("Firmware %1 (protocol %2) is up to date.")
                    .arg(running, QString::number(kProtocolVersion)));
            install_button_->setText(
                tr("Reinstall firmware v%1…").arg(kVersion));
            break;
        case FirmwareState::kDifferentVersion:
            programmer_status_->setText(StatusText(
                kAmber, tr("Firmware update required on %1.").arg(port->name)));
            firmware_label_->setText(
                tr("Installed: %1 · Required: v%2 (protocol %3).")
                    .arg(probe_.hello.section(QLatin1Char(' '), 1),
                         QString::fromLatin1(kVersion),
                         QString::number(kProtocolVersion)));
            install_button_->setDefault(true);
            break;
        case FirmwareState::kNoResponse:
            programmer_status_->setText(StatusText(
                kAmber,
                tr("The Leonardo on %1 runs other firmware.").arg(port->name)));
            firmware_label_->setText(
                tr("Install the programmer firmware v%1 to use it.")
                    .arg(kVersion));
            install_button_->setDefault(true);
            break;
        case FirmwareState::kPortError:
            programmer_status_->setText(StatusText(
                kRed, tr("%1 cannot be opened. Close other programs that use "
                         "it (Arduino IDE serial monitor) and reconnect.")
                          .arg(port->name)));
            firmware_label_->setText(probe_.error);
            break;
    }
}

/**
 * @brief Shows the socketed device and whether it suits the JEDEC file.
 */
void MainWindow::UpdateDevicePanel() {
    Device socket = DeviceFromIdText(idcode_.toStdString());
    if (idcode_.isEmpty()) {
        device_label_->setText(QStringLiteral("–"));
        idcode_label_->setText(QStringLiteral("–"));
    } else {
        device_label_->setText(socket == Device::kUnknown
                                   ? tr("No supported CPLD")
                                   : QString::fromLatin1(DeviceName(socket)));
        idcode_label_->setText(QStringLiteral("0x") + idcode_);
    }
    if (socket == Device::kUnknown || document_.empty()) {
        match_label_->setText(
            idcode_.isEmpty() || socket != Device::kUnknown
                ? QString()
                : tr("Seat an ATF1502AS or ATF1504AS with JTAG enabled."));
    } else if (socket == document_.device()) {
        match_label_->setText(
            StatusText(kGreen, tr("Matches the JEDEC file.")));
    } else {
        match_label_->setText(StatusText(
            kRed,
            tr("The JEDEC file targets %1.")
                .arg(QString::fromLatin1(DeviceName(document_.device())))));
    }
    UpdateActions();
}

/**
 * @brief Fills the JEDEC summary and legend counts.
 */
void MainWindow::UpdateDocumentPanel() {
    const FuseDatabase* database =
        document_.empty() ? nullptr
                          : FuseDatabase::ForDevice(document_.device());
    for (int i = 0; i < kRegionCount; ++i) {
        if (legend_boxes_[i] == nullptr) {
            continue;
        }
        Region region = static_cast<Region>(i);
        bool block_cd = region == Region::kProductTermsC ||
                        region == Region::kProductTermsD;
        bool shown = !(block_cd && document_.device() == Device::kAtf1502as);
        legend_boxes_[i]->setHidden(!shown);
        legend_counts_[i]->setHidden(!shown);
        const RegionUsage& usage = document_.usage()[i];
        legend_counts_[i]->setText(database == nullptr ||
                                           region == Region::kPadding
                                       ? QString()
                                       : QStringLiteral("%1 / %2")
                                             .arg(usage.programmed)
                                             .arg(usage.total));
    }
    ArrangeLegend();
    if (document_.empty()) {
        for (QLabel* label : {file_label_, jedec_device_label_, fuses_label_,
                              pterms_label_, macrocells_label_, checksum_label_,
                              arming_label_, jtag_label_, ues_label_}) {
            label->setText(QStringLiteral("–"));
            label->setToolTip(QString());
        }
        setWindowTitle(tr("ATF150x Programmer %1").arg(kVersion));
        return;
    }
    QFileInfo info(document_.path());
    file_label_->setText(info.fileName());
    file_label_->setToolTip(
        QDir::toNativeSeparators(info.absoluteFilePath()) +
        (document_.header().isEmpty()
             ? QString()
             : QStringLiteral("\n\n") + document_.header()));
    unsigned int count = FuseCount(document_.device());
    jedec_device_label_->setText(
        tr("%1 · %2 fuses · %3 words")
            .arg(QString::fromLatin1(DeviceName(document_.device())))
            .arg(count)
            .arg(document_.image().size()));
    fuses_label_->setText(
        tr("%1 fuses (%2%)")
            .arg(document_.programmed())
            .arg(100.0 * document_.programmed() / count, 0, 'f', 1));
    pterms_label_->setText(tr("%1 of %2 in use")
                               .arg(document_.used_product_terms())
                               .arg(document_.total_product_terms()));
    pterms_label_->setToolTip(
        tr("Product terms that connect some, but not all, inputs"));
    macrocells_label_->setText(tr("%1 of %2 in use")
                                   .arg(document_.used_macrocells())
                                   .arg(document_.total_macrocells()));
    checksum_label_->setText(tr("%1 (valid)")
                                 .arg(QString::number(document_.checksum(), 16)
                                          .rightJustified(4, QLatin1Char('0'))
                                          .toUpper()));
    arming_label_->setText(document_.armed()
                               ? tr("Enabled after programming")
                               : tr("Kept disabled (arming switch safe)"));
    jtag_label_->setText(tr("Enabled, read protection off"));
    ues_label_->setText(QStringLiteral("0x") +
                        QString::number(document_.user_signature(), 16)
                            .rightJustified(4, QLatin1Char('0'))
                            .toUpper());
    setWindowTitle(tr("%1 – ATF150x Programmer %2")
                       .arg(info.fileName(), QString::fromLatin1(kVersion)));
}

/**
 * @brief Shows "Fit" or the fixed cell size.
 */
void MainWindow::UpdateZoomLabel(double cell_size, bool fit) {
    zoom_label_->setText(fit ? tr("Fit (%1 px)").arg(cell_size, 0, 'f', 1)
                             : tr("%1 px").arg(cell_size, 0, 'f', 0));
}

/**
 * @brief Re-flows the legend so hidden block C/D entries leave no gaps.
 */
void MainWindow::ArrangeLegend() {
    for (int i = 0; i < kRegionCount; ++i) {
        if (legend_boxes_[i] != nullptr) {
            legend_layout_->removeWidget(legend_boxes_[i]);
            legend_layout_->removeWidget(legend_counts_[i]);
        }
    }
    int slot = 0;
    for (int i = 0; i < kRegionCount; ++i) {
        if (legend_boxes_[i] == nullptr || legend_boxes_[i]->isHidden()) {
            continue;
        }
        legend_layout_->addWidget(legend_boxes_[i], slot / 3, (slot % 3) * 2);
        legend_layout_->addWidget(legend_counts_[i], slot / 3,
                                  (slot % 3) * 2 + 1);
        ++slot;
    }
}

/**
 * @brief Shows the latest operation outcome under the programmer status.
 */
void MainWindow::SetResult(bool ok, const QString& message) {
    result_label_->setText(StatusText(
        ok ? kGreen : kRed,
        QStringLiteral("%1  %2").arg(
            QTime::currentTime().toString(QStringLiteral("HH:mm")), message)));
    result_label_->show();
}

/**
 * @brief Appends a timestamped line and keeps the view scrolled down.
 */
void MainWindow::AppendLog(const QString& text) {
    log_->appendPlainText(QStringLiteral("%1  %2").arg(
        QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), text));
    log_->verticalScrollBar()->setValue(log_->verticalScrollBar()->maximum());
}

/**
 * @brief Loads a file, updates all views and remembers it.
 */
bool MainWindow::OpenFile(const QString& path) {
    JedecDocument document;
    QString error;
    if (!JedecDocument::Load(path, &document, &error)) {
        AppendLog(tr("Cannot load %1: %2")
                      .arg(QDir::toNativeSeparators(path), error));
        QMessageBox::critical(
            this, tr("Invalid JEDEC file"),
            tr("%1\n\n%2").arg(QDir::toNativeSeparators(path), error));
        return false;
    }
    document_ = std::move(document);
    QFileInfo info(path);
    document_modified_ = info.lastModified();
    document_size_ = info.size();
    map_->SetDocument(&document_);
    UpdateDocumentPanel();
    UpdateDevicePanel();
    UpdateActions();
    AddRecentFile(info.absoluteFilePath());
    settings_.setValue(QStringLiteral("last_directory"), info.absolutePath());
    AppendLog(tr("Loaded %1: %2, %3 of %4 fuses programmed.")
                  .arg(info.fileName(),
                       QString::fromLatin1(DeviceName(document_.device())))
                  .arg(document_.programmed())
                  .arg(FuseCount(document_.device())));
    return true;
}

/**
 * @brief Opens the file dialog in the last used directory.
 */
void MainWindow::ShowOpenDialog() {
    QString directory = settings_
                            .value(QStringLiteral("last_directory"),
                                   QStandardPaths::writableLocation(
                                       QStandardPaths::DocumentsLocation))
                            .toString();
    QString path =
        QFileDialog::getOpenFileName(this, tr("Open JEDEC file"), directory,
                                     tr("JEDEC files (*.jed);;All files (*)"));
    if (!path.isEmpty()) {
        OpenFile(path);
    }
}

/**
 * @brief Moves a path to the top of the recent-files list.
 */
void MainWindow::AddRecentFile(const QString& path) {
    QStringList files =
        settings_.value(QStringLiteral("recent_files")).toStringList();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > kMaxRecentFiles) {
        files.removeLast();
    }
    settings_.setValue(QStringLiteral("recent_files"), files);
    RebuildRecentMenu();
}

/**
 * @brief Lists recent files with keyboard accelerators.
 */
void MainWindow::RebuildRecentMenu() {
    recent_menu_->clear();
    QStringList files =
        settings_.value(QStringLiteral("recent_files")).toStringList();
    for (int i = 0; i < files.size(); ++i) {
        QString path = files[i];
        recent_menu_->addAction(QStringLiteral("&%1  %2").arg(i + 1).arg(
                                    QDir::toNativeSeparators(path)),
                                this, [this, path] { OpenFile(path); });
    }
    if (!files.isEmpty()) {
        recent_menu_->addSeparator();
        recent_menu_->addAction(tr("&Clear List"), this, [this] {
            settings_.remove(QStringLiteral("recent_files"));
            RebuildRecentMenu();
        });
    }
    recent_menu_->setEnabled(!files.isEmpty());
}

/**
 * @brief Shows a dismissible banner with a download link.
 */
void MainWindow::OnUpdateAvailable(const QString& version, const QUrl& url,
                                   bool interactive) {
    AppendLog(tr("Version %1 is available: %2").arg(version, url.toString()));
    update_label_->setText(
        tr("ATF150x Programmer %1 is available (you have %2). "
           "<a href=\"%3\">Download it from GitHub</a>.")
            .arg(version.toHtmlEscaped(), QString::fromLatin1(kVersion),
                 url.toString().toHtmlEscaped()));
    update_banner_->show();
    if (interactive) {
        auto answer = QMessageBox::information(
            this, tr("Update available"),
            tr("Version %1 is available. Open the download page?").arg(version),
            QMessageBox::Open | QMessageBox::Close, QMessageBox::Open);
        if (answer == QMessageBox::Open) {
            QDesktopServices::openUrl(url);
        }
    }
}

/**
 * @brief Confirms an explicit check only.
 */
void MainWindow::OnUpToDate(const QString& latest, bool interactive) {
    AppendLog(tr("No newer release available (latest is %1).").arg(latest));
    if (interactive) {
        QMessageBox::information(
            this, tr("No update available"),
            tr("ATF150x Programmer %1 is the latest version.").arg(kVersion));
    }
}

/**
 * @brief Logs silent failures; reports explicit ones.
 */
void MainWindow::OnUpdateCheckFailed(const QString& error, bool interactive) {
    AppendLog(tr("Update check failed: %1").arg(error));
    if (interactive) {
        QMessageBox::warning(
            this, tr("Update check failed"),
            tr("Could not check for updates.\n\n%1").arg(error));
    }
}

/**
 * @brief Looks for the image installed next to the executable.
 */
QString MainWindow::BundledFirmwarePath() const {
    QString path = QCoreApplication::applicationDirPath() + QLatin1Char('/') +
                   QString::fromLatin1(kFirmwareFile);
    return QFileInfo::exists(path) ? path : QString();
}

/**
 * @brief Prefers the bundled AVRDUDE, then one on the search path.
 */
QString MainWindow::AvrdudePath(QString* config) const {
    QString directory = QCoreApplication::applicationDirPath() +
                        QStringLiteral("/tools/avrdude");
#ifdef Q_OS_WIN
    QString bundled = directory + QStringLiteral("/avrdude.exe");
#else
    QString bundled = directory + QStringLiteral("/avrdude");
#endif
    QString bundled_config = directory + QStringLiteral("/avrdude.conf");
    if (QFileInfo::exists(bundled)) {
        *config =
            QFileInfo::exists(bundled_config) ? bundled_config : QString();
        return bundled;
    }
    config->clear();
    return QStandardPaths::findExecutable(QStringLiteral("avrdude"));
}

/**
 * @brief Credits, licenses and version information.
 */
void MainWindow::ShowAbout() {
    const FuseDatabase* database = FuseDatabase::ForDevice(Device::kAtf1502as);
    QMessageBox::about(
        this, tr("About ATF150x Programmer"),
        tr("<h3>ATF150x Programmer %1</h3>"
           "<p>Erase, program and verify ATF1502AS and ATF1504AS CPLDs with "
           "an Arduino Leonardo based programmer.</p>"
           "<p>Firmware protocol %2 · Qt %3</p>"
           "<p>Licensed under the GNU General Public License v3.0.<br>"
           "<a href=\"https://github.com/ifilot/atf150x-programmer\">"
           "github.com/ifilot/atf150x-programmer</a></p>"
           "<p>Fuse mapping and region data are adapted from Project Bureau "
           "by whitequark (%4). Firmware installation uses AVRDUDE, "
           "distributed under the GNU GPL v2.</p>")
            .arg(QString::fromLatin1(kVersion),
                 QString::number(kProtocolVersion),
                 QString::fromLatin1(qVersion()),
                 database ? database->source() : QString()));
}

/**
 * @brief Saves settings; refuses while the CPLD is being written.
 */
void MainWindow::closeEvent(QCloseEvent* event) {
    if (busy_) {
        QMessageBox::warning(
            this, tr("Operation in progress"),
            tr("Wait until the current operation finishes. Interrupting it "
               "can leave the CPLD or the programmer partially written."));
        event->ignore();
        return;
    }
    settings_.setValue(QStringLiteral("geometry"), saveGeometry());
    settings_.setValue(QStringLiteral("window_state"), saveState());
    event->accept();
}

/**
 * @brief Accepts drags that carry a local .jed file.
 */
void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls() && mime->urls().size() == 1 &&
        IsJedecPath(mime->urls().first().toLocalFile())) {
        event->acceptProposedAction();
    }
}

/**
 * @brief Opens the dropped file.
 */
void MainWindow::dropEvent(QDropEvent* event) {
    QString path = event->mimeData()->urls().first().toLocalFile();
    if (IsJedecPath(path)) {
        OpenFile(path);
        event->acceptProposedAction();
    }
}

}  // namespace gui
}  // namespace atf
