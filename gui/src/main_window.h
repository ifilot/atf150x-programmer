// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 ATF1502 programmer contributors

/**
 * @file
 * @brief Main window: programmer status, JEDEC summary, fuse map and log.
 */
#ifndef ATF150X_PROGRAMMER_GUI_SRC_MAIN_WINDOW_H_
#define ATF150X_PROGRAMMER_GUI_SRC_MAIN_WINDOW_H_

#include <QDateTime>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QThread>
#include <array>

#include "gui/src/device_finder.h"
#include "gui/src/fuse_database.h"
#include "gui/src/jedec_document.h"
#include "gui/src/programmer_worker.h"

class QAction;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QTimer;
class QToolButton;
class QUrl;

namespace atf {
namespace gui {

class FuseMapWidget;
class UpdateChecker;

/**
 * @brief Top-level window of the ATF150x Programmer application.
 *
 * Owns the programmer worker thread. The Leonardo is found by USB identity,
 * probed with HELLO, and offered a firmware installation when it does not run
 * the matching firmware. Device operations run on the worker thread while
 * the window stays responsive.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    /**
     * @brief Builds the window and starts port monitoring.
     *
     * @param[in] parent Optional Qt parent.
     */
    explicit MainWindow(QWidget* parent = nullptr);

    /**
     * @brief Stops the worker thread after its current task.
     */
    ~MainWindow() override;

    /**
     * @brief Loads a JEDEC file and shows its fuse map.
     *
     * Reports failures in a message box and keeps the previous document.
     *
     * @param[in] path File to open.
     * @return True on success.
     */
    bool OpenFile(const QString& path);

protected:
    /** @brief Refuses to close while an operation is running. */
    void closeEvent(QCloseEvent* event) override;
    /** @brief Accepts dragged JEDEC files. */
    void dragEnterEvent(QDragEnterEvent* event) override;
    /** @brief Opens a dropped JEDEC file. */
    void dropEvent(QDropEvent* event) override;

private:
    /** @brief Creates all commands with shortcuts and icons. */
    void CreateActions();
    /** @brief Builds the menu bar. */
    void CreateMenus();
    /** @brief Builds the main toolbar. */
    void CreateToolBar();
    /** @brief Builds the update banner and the side panel/map splitter. */
    void CreateCentralWidget();
    /**
     * @brief Builds the programmer, target, JEDEC and legend groups.
     * @return Scrollable side panel owned by this window.
     */
    QWidget* CreateSidePanel();
    /**
     * @brief Builds the fuse map with zoom controls and hover line.
     * @return Map panel owned by this window.
     */
    QWidget* CreateMapPanel();
    /** @brief Builds the dockable session log. */
    void CreateLogDock();
    /** @brief Restores window geometry and dock layout. */
    void RestoreSettings();

    /**
     * @brief Re-reads Leonardo ports and probes a changed selection.
     * @param[in] force_probe Probe the selection even if nothing changed.
     */
    void RefreshPorts(bool force_probe);
    /**
     * @brief Returns the port behind the selector.
     * @return Port, or nullptr when no Leonardo is present.
     */
    const LeonardoPort* SelectedPort() const;
    /** @brief Probes the selected sketch port unless the worker is busy. */
    void OnPortSelected();
    /**
     * @brief Stores a probe result and updates the panels.
     * @param[in] result Handshake result from the worker.
     */
    void OnProbeFinished(const ProbeResult& result);
    /**
     * @brief Offers a firmware installation once per connection.
     * @param[in] result Probe result of a mismatching or foreign sketch.
     */
    void OfferFirmwareInstall(const ProbeResult& result);
    /**
     * @brief Tests whether the selected port runs matching firmware.
     * @return True when device commands may be sent.
     */
    bool ProgrammerReady() const;

    /**
     * @brief Validates preconditions and starts a task on the worker.
     * @param[in] task Task to run.
     */
    void StartTask(Task task);
    /**
     * @brief Reloads the JEDEC file when it changed on disk.
     * @return True when the document is current and valid.
     */
    bool ReloadIfChanged();
    /**
     * @brief Reports the end of a task.
     * @param[in] ok True on success.
     * @param[in] message Summary or diagnostic.
     */
    void OnOperationFinished(bool ok, const QString& message);
    /**
     * @brief Updates the progress indicator.
     * @param[in] stage Stage caption.
     * @param[in] done Completed units.
     * @param[in] total Total units, or zero when indeterminate.
     */
    void OnProgress(const QString& stage, int done, int total);
    /** @brief Confirms and starts the firmware installation. */
    void InstallFirmware();
    /**
     * @brief Reports the installation and re-probes the programmer.
     * @param[in] ok True when the firmware was written and verified.
     * @param[in] message Summary or diagnostic.
     * @param[in] port Returned sketch port, or empty.
     */
    void OnFirmwareFinished(bool ok, const QString& message,
                            const QString& port);
    /**
     * @brief Locks or unlocks commands around a worker task.
     * @param[in] busy True while a task runs.
     * @param[in] caption Progress caption while busy.
     */
    void SetBusy(bool busy, const QString& caption = QString());

    /** @brief Enables commands according to the current state. */
    void UpdateActions();
    /** @brief Shows connection and firmware state. */
    void UpdateProgrammerPanel();
    /** @brief Shows the socketed device and its match with the file. */
    void UpdateDevicePanel();
    /** @brief Shows the JEDEC summary and legend counts. */
    void UpdateDocumentPanel();
    /**
     * @brief Shows the current cell size.
     * @param[in] cell_size Pixels per cell.
     * @param[in] fit True in fit mode.
     */
    void UpdateZoomLabel(double cell_size, bool fit);
    /** @brief Lays out the visible legend entries three per row. */
    void ArrangeLegend();
    /**
     * @brief Shows the latest task outcome in the programmer group.
     * @param[in] ok True on success.
     * @param[in] message Summary or diagnostic.
     */
    void SetResult(bool ok, const QString& message);
    /**
     * @brief Adds a timestamped line to the log.
     * @param[in] text Line to add.
     */
    void AppendLog(const QString& text);

    /** @brief Asks for a JEDEC file to open. */
    void ShowOpenDialog();
    /**
     * @brief Moves a file to the top of the recent-files list.
     * @param[in] path Absolute file path.
     */
    void AddRecentFile(const QString& path);
    /** @brief Rebuilds the recent-files menu from the settings. */
    void RebuildRecentMenu();

    /**
     * @brief Shows the update banner.
     * @param[in] version Newer version.
     * @param[in] url Release page.
     * @param[in] interactive True for a check started by the user.
     */
    void OnUpdateAvailable(const QString& version, const QUrl& url,
                           bool interactive);
    /**
     * @brief Reports that no newer release exists.
     * @param[in] latest Latest published version.
     * @param[in] interactive True for a check started by the user.
     */
    void OnUpToDate(const QString& latest, bool interactive);
    /**
     * @brief Reports a failed update check.
     * @param[in] error Diagnostic.
     * @param[in] interactive True for a check started by the user.
     */
    void OnUpdateCheckFailed(const QString& error, bool interactive);
    /**
     * @brief Locates the firmware image bundled with the application.
     * @return Absolute path, or empty when missing.
     */
    QString BundledFirmwarePath() const;
    /**
     * @brief Locates AVRDUDE and its configuration.
     * @param[out] config Non-null; set to avrdude.conf or cleared.
     * @return Executable path, or empty when unavailable.
     */
    QString AvrdudePath(QString* config) const;
    /** @brief Shows version, license and attribution information. */
    void ShowAbout();

    QSettings settings_;
    QThread worker_thread_;
    ProgrammerWorker* worker_ = nullptr;
    UpdateChecker* update_checker_ = nullptr;
    QTimer* port_timer_ = nullptr;

    QList<LeonardoPort> ports_;
    ProbeResult probe_;
    bool probe_valid_ = false;
    bool worker_active_ = false;
    bool busy_ = false;
    QSet<QString> prompted_ports_;
    QString idcode_;
    JedecDocument document_;
    QDateTime document_modified_;
    qint64 document_size_ = -1;

    QAction* open_action_ = nullptr;
    QAction* reload_action_ = nullptr;
    QAction* identify_action_ = nullptr;
    QAction* program_action_ = nullptr;
    QAction* verify_action_ = nullptr;
    QAction* erase_action_ = nullptr;
    QAction* firmware_action_ = nullptr;
    QAction* refresh_action_ = nullptr;
    QAction* zoom_in_action_ = nullptr;
    QAction* zoom_out_action_ = nullptr;
    QAction* zoom_fit_action_ = nullptr;
    QAction* startup_check_action_ = nullptr;
    QMenu* recent_menu_ = nullptr;

    QFrame* update_banner_ = nullptr;
    QLabel* update_label_ = nullptr;
    QComboBox* port_combo_ = nullptr;
    QLabel* programmer_status_ = nullptr;
    QLabel* firmware_label_ = nullptr;
    QPushButton* install_button_ = nullptr;
    QLabel* result_label_ = nullptr;
    QLabel* device_label_ = nullptr;
    QLabel* idcode_label_ = nullptr;
    QLabel* match_label_ = nullptr;

    QLabel* file_label_ = nullptr;
    QLabel* jedec_device_label_ = nullptr;
    QLabel* fuses_label_ = nullptr;
    QLabel* pterms_label_ = nullptr;
    QLabel* macrocells_label_ = nullptr;
    QLabel* checksum_label_ = nullptr;
    QLabel* arming_label_ = nullptr;
    QLabel* jtag_label_ = nullptr;
    QLabel* ues_label_ = nullptr;

    std::array<QCheckBox*, kRegionCount> legend_boxes_{};
    std::array<QLabel*, kRegionCount> legend_counts_{};
    QCheckBox* region_colors_box_ = nullptr;
    QGridLayout* legend_layout_ = nullptr;

    FuseMapWidget* map_ = nullptr;
    QScrollArea* map_scroll_ = nullptr;
    QLabel* hover_label_ = nullptr;
    QLabel* zoom_label_ = nullptr;

    QPlainTextEdit* log_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* progress_caption_ = nullptr;
};

}  // namespace gui
}  // namespace atf

#endif  // ATF150X_PROGRAMMER_GUI_SRC_MAIN_WINDOW_H_
