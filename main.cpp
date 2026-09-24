#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QPixmap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace {

enum class Kind { Desktop, UserUnit, SystemUnit };

struct Entry {
    Kind kind;
    QString name;
    QString source;
    QString detail;
    QString key;
    QString path;
    QString globalPath;
    QString state;
    bool enabled = false;
    bool deletable = false;
};

QString configHome() {
    const QString value = qEnvironmentVariable("XDG_CONFIG_HOME");
    return value.isEmpty() ? QDir::homePath() + "/.config" : value;
}

QString autostartDir() { return configHome() + "/autostart"; }
QString userUnitDir() { return configHome() + "/systemd/user"; }

bool safeDesktopName(const QString &name) {
    static const QRegularExpression pattern(R"(^[A-Za-z0-9][A-Za-z0-9_.@+-]*\.desktop$)");
    return pattern.match(name).hasMatch();
}

bool safeUnitName(const QString &name) {
    static const QRegularExpression pattern(R"(^[A-Za-z0-9][A-Za-z0-9_.@-]*\.(service|timer)$)");
    return pattern.match(name).hasMatch();
}

QString desktopValue(const QByteArray &bytes, const QString &key) {
    bool inGroup = false;
    for (const QByteArray &raw : bytes.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith('[')) {
            inGroup = line == "[Desktop Entry]";
            continue;
        }
        if (inGroup && line.startsWith(key + '=')) return line.mid(key.size() + 1).trimmed();
    }
    return {};
}

bool writeFile(const QString &path, const QByteArray &data, QString *error) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        *error = "Could not create the destination directory.";
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = "Could not write " + path + ": " + file.errorString();
        return false;
    }
    return true;
}

bool readFile(const QString &path, QByteArray *data, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = "Could not read " + path + ": " + file.errorString();
        return false;
    }
    *data = file.readAll();
    return true;
}

bool setDesktopHidden(const Entry &entry, bool hidden, QString *error) {
    const QString target = autostartDir() + '/' + entry.key;
    QByteArray bytes;
    if (QFileInfo::exists(target)) {
        if (!readFile(target, &bytes, error)) return false;
    } else {
        if (entry.globalPath.isEmpty()) {
            *error = "The desktop entry no longer exists.";
            return false;
        }
        if (!readFile(entry.globalPath, &bytes, error)) return false;
    }

    QList<QByteArray> lines = bytes.split('\n');
    bool inGroup = false;
    bool found = false;
    for (QByteArray &line : lines) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith('[')) inGroup = trimmed == "[Desktop Entry]";
        if (inGroup && trimmed.startsWith("Hidden=")) {
            line = hidden ? "Hidden=true" : "Hidden=false";
            found = true;
        }
        if (inGroup && trimmed.startsWith("X-GNOME-Autostart-enabled="))
            line = hidden ? "X-GNOME-Autostart-enabled=false" : "X-GNOME-Autostart-enabled=true";
    }
    if (!found) {
        int groupLine = -1;
        for (int i = 0; i < lines.size(); ++i)
            if (lines[i].trimmed() == "[Desktop Entry]") { groupLine = i; break; }
        if (groupLine < 0) {
            *error = "This file has no [Desktop Entry] section.";
            return false;
        }
        lines.insert(groupLine + 1, hidden ? "Hidden=true" : "Hidden=false");
    }
    return writeFile(target, lines.join('\n'), error);
}

bool runProcess(const QString &program, const QStringList &args, QString *error) {
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(5000)) {
        *error = "Could not start " + program + ": " + process.errorString();
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        *error = "The command timed out.";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error->isEmpty()) *error = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        if (error->isEmpty()) *error = program + " failed.";
        return false;
    }
    return true;
}

bool setUnitEnabled(const Entry &entry, bool enabled, QString *error) {
    if (!safeUnitName(entry.key)) { *error = "Invalid unit name."; return false; }
    const QString command = enabled ? "enable" : "disable";
    if (entry.kind == Kind::UserUnit)
        return runProcess("/usr/bin/systemctl", {"--user", command, "--", entry.key}, error);
    return runProcess("/usr/bin/pkexec", {"/usr/bin/systemctl", command, "--", entry.key}, error);
}

QMap<QString, QString> desktopFiles(const QString &directory) {
    QMap<QString, QString> result;
    QDir dir(directory);
    for (const QString &name : dir.entryList({"*.desktop"}, QDir::Files | QDir::NoDotAndDotDot)) {
        if (safeDesktopName(name)) result.insert(name, dir.filePath(name));
    }
    return result;
}

QList<Entry> scanDesktop() {
    QMap<QString, QString> global;
    QStringList dirs = qEnvironmentVariable("XDG_CONFIG_DIRS", "/etc/xdg").split(':', Qt::SkipEmptyParts);
    for (const QString &dir : dirs) {
        const auto found = desktopFiles(dir + "/autostart");
        for (auto it = found.begin(); it != found.end(); ++it)
            if (!global.contains(it.key())) global.insert(it.key(), it.value());
    }
    const auto user = desktopFiles(autostartDir());
    QSet<QString> names;
    for (auto it = global.begin(); it != global.end(); ++it) names.insert(it.key());
    for (auto it = user.begin(); it != user.end(); ++it) names.insert(it.key());

    QList<Entry> entries;
    for (const QString &name : names) {
        const QString path = user.value(name, global.value(name));
        QByteArray bytes;
        QString error;
        if (!readFile(path, &bytes, &error)) continue;
        Entry entry;
        entry.kind = Kind::Desktop;
        entry.key = name;
        entry.path = path;
        entry.globalPath = global.value(name);
        entry.name = desktopValue(bytes, "Name");
        if (entry.name.isEmpty()) entry.name = name;
        entry.detail = desktopValue(bytes, "Exec");
        entry.enabled = desktopValue(bytes, "Hidden").compare("true", Qt::CaseInsensitive) != 0 &&
                        desktopValue(bytes, "X-GNOME-Autostart-enabled").compare("false", Qt::CaseInsensitive) != 0;
        entry.state = entry.enabled ? "Enabled" : "Disabled";
        entry.source = user.contains(name) ? (global.contains(name) ? "User override" : "User app") : "System app";
        entry.deletable = user.contains(name);
        entries.append(entry);
    }
    return entries;
}

QList<Entry> scanUnits(bool user) {
    QList<Entry> entries;
    for (const QString &type : {"service", "timer"}) {
        QProcess process;
        QStringList args;
        if (user) args << "--user";
        args << "list-unit-files" << "--type=" + type << "--no-legend" << "--no-pager";
        process.start("/usr/bin/systemctl", args);
        if (!process.waitForFinished(10000) || process.exitCode() != 0) continue;
        const QString output = QString::fromUtf8(process.readAllStandardOutput());
        for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
            const QStringList columns = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (columns.size() < 2 || !safeUnitName(columns[0])) continue;
            const QString state = columns[1];
            if (state != "enabled" && state != "enabled-runtime" && state != "disabled") continue;
            Entry entry;
            entry.kind = user ? Kind::UserUnit : Kind::SystemUnit;
            entry.key = columns[0];
            entry.name = entry.key;
            entry.source = user ? "User systemd" : "System systemd";
            entry.detail = type == "timer" ? "Timer" : "Service";
            entry.state = state;
            entry.enabled = state.startsWith("enabled");
            const QString localPath = userUnitDir() + '/' + entry.key;
            const QFileInfo info(localPath);
            if (user && info.exists()) entry.path = localPath;
            entry.deletable = user && info.isFile() && !info.isSymLink();
            entries.append(entry);
        }
    }
    return entries;
}

QString sourcePath(const Entry &entry, QString *error) {
    if (entry.kind == Kind::Desktop || !entry.path.isEmpty()) return entry.path;
    QStringList args;
    if (entry.kind == Kind::UserUnit) args << "--user";
    args << "show" << "--property=FragmentPath" << "--value" << entry.key;
    QProcess process;
    process.start("/usr/bin/systemctl", args);
    if (!process.waitForFinished(10000) || process.exitCode() != 0) {
        *error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        if (error->isEmpty()) *error = "Could not locate the unit file.";
        return {};
    }
    const QString path = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    if (path.isEmpty()) *error = "This unit has no installed service file.";
    return path;
}

QMap<QString, QColor> themeColors() {
    QMap<QString, QColor> colors;
    QFile file(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml");
    if (!file.open(QIODevice::ReadOnly)) return colors;
    static const QRegularExpression line("^\\s*([a-z_]+)\\s*=\\s*\"(#[0-9A-Fa-f]{6})\"");
    for (const QString &text : QString::fromUtf8(file.readAll()).split('\n')) {
        auto match = line.match(text);
        if (match.hasMatch()) colors.insert(match.captured(1), QColor(match.captured(2)));
    }
    return colors;
}

void applyTheme(QApplication &app) {
    const auto colors = themeColors();
    auto pick = [&](const QString &key, const char *fallback) { return colors.value(key, QColor(fallback)); };
    const QColor bg = pick("background", "#121212");
    const QColor panel = pick("lighter_background", "#1e1e1e");
    const QColor fg = pick("foreground", "#bebebe");
    const QColor accent = pick("accent", "#e68e0d");
    const QColor selection = pick("selection", "#333333");
    QPalette palette;
    palette.setColor(QPalette::Window, bg);
    palette.setColor(QPalette::Base, panel);
    palette.setColor(QPalette::AlternateBase, bg);
    palette.setColor(QPalette::Button, panel);
    palette.setColor(QPalette::WindowText, fg);
    palette.setColor(QPalette::Text, fg);
    palette.setColor(QPalette::ButtonText, fg);
    palette.setColor(QPalette::Highlight, selection);
    palette.setColor(QPalette::HighlightedText, fg);
    palette.setColor(QPalette::PlaceholderText, pick("light_foreground", "#8a8a8d"));
    app.setPalette(palette);
    app.setStyleSheet(QString("QWidget { font-size: 13px; } "
                              "QPushButton, QLineEdit, QComboBox { padding: 7px 10px; border: 1px solid %1; border-radius: 6px; } "
                              "QPushButton:hover { border-color: %2; } QPushButton:disabled { color: %3; } "
                              "QTableWidget { border: 1px solid %1; border-radius: 6px; gridline-color: %1; } "
                              "QHeaderView::section { background: %4; padding: 8px; border: none; border-bottom: 1px solid %1; } "
                              "QTableWidget::item { padding: 5px; } QTableWidget::item:selected { background: %5; }")
                          .arg(pick("muted", "#333333").name(), accent.name(),
                               pick("dark_foreground", "#555555").name(), panel.name(), selection.name()));
}

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("Vigilant");
        resize(1040, 650);
        auto *central = new QWidget;
        auto *layout = new QVBoxLayout(central);
        layout->setContentsMargins(20, 20, 20, 12);
        layout->setSpacing(12);

        auto *title = new QLabel("Vigilant");
        QFont titleFont = title->font();
        titleFont.setPointSize(18);
        titleFont.setBold(true);
        title->setFont(titleFont);
        auto *titleRow = new QHBoxLayout;
        auto *about = new QPushButton("About");
        auto *titleIcon = new QLabel;
        titleIcon->setPixmap(QPixmap(":/vigilant.png").scaled(42, 42, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        titleRow->addWidget(titleIcon);
        titleRow->addWidget(title);
        titleRow->addStretch();
        titleRow->addWidget(about);
        layout->addLayout(titleRow);
        auto *subtitle = new QLabel("Manage apps and services that start with your session or system.");
        layout->addWidget(subtitle);

        auto *filterRow = new QHBoxLayout;
        search_ = new QLineEdit;
        search_->setPlaceholderText("Search entries…");
        source_ = new QComboBox;
        source_->addItems({"All sources", "Desktop apps", "User systemd", "System systemd"});
        filterRow->addWidget(search_, 1);
        filterRow->addWidget(source_);
        layout->addLayout(filterRow);

        table_ = new QTableWidget;
        table_->setColumnCount(4);
        table_->setHorizontalHeaderLabels({"Name", "Source", "State", "Command / type"});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setAlternatingRowColors(true);
        table_->setContextMenuPolicy(Qt::CustomContextMenu);
        table_->verticalHeader()->hide();
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        layout->addWidget(table_, 1);

        auto *actions = new QHBoxLayout;
        refresh_ = new QPushButton("Refresh");
        backup_ = new QPushButton("Save backup…");
        restore_ = new QPushButton("Load backup…");
        enable_ = new QPushButton("Enable");
        disable_ = new QPushButton("Disable");
        remove_ = new QPushButton("Delete…");
        actions->addWidget(refresh_);
        actions->addWidget(backup_);
        actions->addWidget(restore_);
        actions->addStretch();
        actions->addWidget(enable_);
        actions->addWidget(disable_);
        actions->addWidget(remove_);
        layout->addLayout(actions);
        setCentralWidget(central);
        statusBar()->showMessage("Ready");

        connect(refresh_, &QPushButton::clicked, this, [this] { refresh(); });
        connect(backup_, &QPushButton::clicked, this, [this] { saveBackup(); });
        connect(restore_, &QPushButton::clicked, this, [this] { loadBackup(); });
        connect(enable_, &QPushButton::clicked, this, [this] { changeState(true); });
        connect(disable_, &QPushButton::clicked, this, [this] { changeState(false); });
        connect(remove_, &QPushButton::clicked, this, [this] { deleteEntry(); });
        connect(about, &QPushButton::clicked, this, [this] { showAbout(); });
        connect(search_, &QLineEdit::textChanged, this, [this] { populate(); });
        connect(source_, &QComboBox::currentIndexChanged, this, [this] { populate(); });
        connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
        connect(table_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &position) {
            QTableWidgetItem *item = table_->itemAt(position);
            if (!item) return;
            table_->selectRow(item->row());
            QMenu menu(this);
            QAction *open = menu.addAction("Open in default editor");
            if (menu.exec(table_->viewport()->mapToGlobal(position)) == open) openSourceFile();
        });
        refresh();
        auto *themeTimer = new QTimer(this);
        connect(themeTimer, &QTimer::timeout, this, [] { applyTheme(*qApp); });
        themeTimer->start(5000);
    }

private:
    void showAbout() {
        QDialog dialog(this);
        dialog.setWindowTitle("About Vigilant");
        dialog.resize(580, 440);
        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(20, 20, 20, 20);
        layout->setSpacing(10);

        auto *aboutIcon = new QLabel;
        aboutIcon->setPixmap(QPixmap(":/vigilant.png").scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        layout->addWidget(aboutIcon);
        auto *heading = new QLabel("Vigilant " + qApp->applicationVersion());
        QFont font = heading->font();
        font.setPointSize(17);
        font.setBold(true);
        heading->setFont(font);
        layout->addWidget(heading);
        layout->addWidget(new QLabel("A startup entry manager for Omarchy."));

        auto *github = new QLabel("Created by seth-reee · <a href=\"https://github.com/seth-reee\">GitHub profile</a>");
        github->setOpenExternalLinks(true);
        layout->addWidget(github);
        layout->addWidget(new QLabel("MIT License · Copyright © 2026 seth-reee"));

        QFile license(":/LICENSE");
        auto *licenseText = new QTextBrowser;
        if (license.open(QIODevice::ReadOnly)) licenseText->setPlainText(QString::fromUtf8(license.readAll()));
        layout->addWidget(licenseText, 1);

        auto *close = new QPushButton("Close");
        auto *bottom = new QHBoxLayout;
        bottom->addStretch();
        bottom->addWidget(close);
        layout->addLayout(bottom);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }

    void refresh() {
        entries_ = scanDesktop();
        entries_.append(scanUnits(true));
        entries_.append(scanUnits(false));
        std::sort(entries_.begin(), entries_.end(), [](const Entry &a, const Entry &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        populate();
        statusBar()->showMessage(QString::number(entries_.size()) + " entries found", 5000);
    }

    void populate() {
        table_->setRowCount(0);
        const QString query = search_->text().trimmed();
        for (int i = 0; i < entries_.size(); ++i) {
            const Entry &e = entries_[i];
            const int source = source_->currentIndex();
            if ((source == 1 && e.kind != Kind::Desktop) ||
                (source == 2 && e.kind != Kind::UserUnit) ||
                (source == 3 && e.kind != Kind::SystemUnit)) continue;
            if (!query.isEmpty() && !e.name.contains(query, Qt::CaseInsensitive) &&
                !e.key.contains(query, Qt::CaseInsensitive) &&
                !e.detail.contains(query, Qt::CaseInsensitive)) continue;
            const int row = table_->rowCount();
            table_->insertRow(row);
            const QStringList cells = {e.name, e.source, e.state, e.detail};
            for (int col = 0; col < cells.size(); ++col) {
                auto *item = new QTableWidgetItem(cells[col]);
                item->setData(Qt::UserRole, i);
                item->setToolTip(e.path);
                table_->setItem(row, col, item);
            }
        }
        updateButtons();
    }

    const Entry *selected() const {
        const int row = table_->currentRow();
        if (row < 0 || !table_->item(row, 0)) return nullptr;
        const int index = table_->item(row, 0)->data(Qt::UserRole).toInt();
        return index >= 0 && index < entries_.size() ? &entries_[index] : nullptr;
    }

    void updateButtons() {
        const Entry *entry = selected();
        enable_->setEnabled(entry && !entry->enabled);
        disable_->setEnabled(entry && entry->enabled);
        remove_->setEnabled(entry && entry->deletable);
    }

    void openSourceFile() {
        const Entry *entry = selected();
        if (!entry) return;
        QString error;
        const QString path = sourcePath(*entry, &error);
        if (path.isEmpty() || !QFileInfo(path).isFile()) {
            QMessageBox::warning(this, "Cannot open file",
                                 error.isEmpty() ? "The source file is no longer available." : error);
            return;
        }
        if (!QProcess::startDetached("omarchy", {"launch", "editor", path}))
            QMessageBox::warning(this, "Cannot open file", "Could not launch the Omarchy default editor.");
    }

    void changeState(bool enabled) {
        const Entry *entry = selected();
        if (!entry) return;
        QString error;
        const bool ok = entry->kind == Kind::Desktop ? setDesktopHidden(*entry, !enabled, &error)
                                                      : setUnitEnabled(*entry, enabled, &error);
        if (!ok) QMessageBox::warning(this, "Action failed", error);
        refresh();
    }

    void deleteEntry() {
        const Entry *entry = selected();
        if (!entry || !entry->deletable) return;
        const Entry copy = *entry;
        const QString explanation = copy.kind == Kind::Desktop && !copy.globalPath.isEmpty()
            ? "This deletes the user override. The system desktop entry will become visible again."
            : "This deletes the local startup file.";
        if (QMessageBox::question(this, "Delete startup entry",
                                  "Delete " + copy.name + "?\n\n" + explanation) != QMessageBox::Yes) return;
        QString error;
        if (copy.kind == Kind::UserUnit && copy.enabled && !setUnitEnabled(copy, false, &error)) {
            QMessageBox::warning(this, "Action failed", error);
            return;
        }
        if (!QFile::remove(copy.path)) {
            QMessageBox::warning(this, "Action failed", "Could not delete " + copy.path);
            return;
        }
        if (copy.kind == Kind::UserUnit && !runProcess("/usr/bin/systemctl", {"--user", "daemon-reload"}, &error))
            QMessageBox::warning(this, "Reload failed", error);
        refresh();
    }

    void saveBackup() {
        const QString suggested = QDir::homePath() + "/vigilant-" +
            QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss") + ".json";
        const QString path = QFileDialog::getSaveFileName(this, "Save startup backup", suggested, "JSON (*.json)");
        if (path.isEmpty()) return;
        QJsonObject root;
        root["format"] = "vigilant-1";
        root["created"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QJsonArray files;
        const auto userFiles = desktopFiles(autostartDir());
        for (auto it = userFiles.begin(); it != userFiles.end(); ++it) {
            QByteArray bytes;
            QString error;
            if (!readFile(it.value(), &bytes, &error)) {
                QMessageBox::warning(this, "Backup failed", error);
                return;
            }
            files.append(QJsonObject{{"name", it.key()}, {"data", QString::fromLatin1(bytes.toBase64())}});
        }
        root["userDesktopFiles"] = files;
        QJsonArray userUnits;
        QDir unitDirectory(userUnitDir());
        for (const QString &name : unitDirectory.entryList({"*.service", "*.timer"}, QDir::Files | QDir::NoDotAndDotDot)) {
            if (!safeUnitName(name)) continue;
            const QFileInfo info(unitDirectory.filePath(name));
            if (info.isSymLink()) continue;
            QByteArray bytes;
            QString error;
            if (!readFile(info.filePath(), &bytes, &error)) {
                QMessageBox::warning(this, "Backup failed", error);
                return;
            }
            userUnits.append(QJsonObject{{"name", name}, {"data", QString::fromLatin1(bytes.toBase64())}});
        }
        root["userUnitFiles"] = userUnits;
        QJsonArray globalNames;
        QJsonArray units;
        for (const Entry &entry : entries_) {
            if (entry.kind == Kind::Desktop && !entry.globalPath.isEmpty()) globalNames.append(entry.key);
            if (entry.kind != Kind::Desktop)
                units.append(QJsonObject{{"scope", entry.kind == Kind::UserUnit ? "user" : "system"},
                                         {"name", entry.key}, {"enabled", entry.enabled}});
        }
        root["globalDesktopNames"] = globalNames;
        root["units"] = units;
        QString error;
        if (!writeFile(path, QJsonDocument(root).toJson(QJsonDocument::Indented), &error))
            QMessageBox::warning(this, "Backup failed", error);
        else statusBar()->showMessage("Backup saved to " + path, 10000);
    }

    void loadBackup() {
        const QString path = QFileDialog::getOpenFileName(this, "Load startup backup", QDir::homePath(), "JSON (*.json)");
        if (path.isEmpty()) return;
        QByteArray bytes;
        QString error;
        if (!readFile(path, &bytes, &error)) { QMessageBox::warning(this, "Load failed", error); return; }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        const QJsonObject root = document.object();
        const QString format = root.value("format").toString();
        if (!document.isObject() || (format != "vigilant-1" && format != "bootkeeper-1") ||
            !root.value("userDesktopFiles").isArray() || !root.value("userUnitFiles").isArray() ||
            !root.value("globalDesktopNames").isArray() ||
            !root.value("units").isArray()) {
            QMessageBox::warning(this, "Load failed", "This is not a Vigilant backup.");
            return;
        }
        QMap<QString, QByteArray> files;
        for (const QJsonValue &value : root.value("userDesktopFiles").toArray()) {
            const QJsonObject object = value.toObject();
            const QString name = object.value("name").toString();
            const QByteArray encoded = object.value("data").toString().toLatin1();
            if (!safeDesktopName(name) || encoded.size() > 8 * 1024 * 1024 ||
                QByteArray::fromBase64Encoding(encoded).decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
                QMessageBox::warning(this, "Load failed", "The backup contains an invalid desktop file.");
                return;
            }
            files.insert(name, QByteArray::fromBase64(encoded));
        }
        QSet<QString> globalNames;
        QMap<QString, QByteArray> userUnitFiles;
        for (const QJsonValue &value : root.value("userUnitFiles").toArray()) {
            const QJsonObject object = value.toObject();
            const QString name = object.value("name").toString();
            const QByteArray encoded = object.value("data").toString().toLatin1();
            if (!safeUnitName(name) || encoded.size() > 8 * 1024 * 1024 ||
                QByteArray::fromBase64Encoding(encoded).decodingStatus != QByteArray::Base64DecodingStatus::Ok) {
                QMessageBox::warning(this, "Load failed", "The backup contains an invalid user unit file.");
                return;
            }
            userUnitFiles.insert(name, QByteArray::fromBase64(encoded));
        }
        for (const QJsonValue &value : root.value("globalDesktopNames").toArray()) {
            const QString name = value.toString();
            if (!safeDesktopName(name)) { QMessageBox::warning(this, "Load failed", "Invalid desktop entry name."); return; }
            globalNames.insert(name);
        }
        QList<Entry> units;
        for (const QJsonValue &value : root.value("units").toArray()) {
            const QJsonObject object = value.toObject();
            const QString name = object.value("name").toString();
            const QString scope = object.value("scope").toString();
            if (!safeUnitName(name) || (scope != "user" && scope != "system") || !object.value("enabled").isBool()) {
                QMessageBox::warning(this, "Load failed", "The backup contains an invalid unit.");
                return;
            }
            Entry entry;
            entry.kind = scope == "user" ? Kind::UserUnit : Kind::SystemUnit;
            entry.key = name;
            entry.enabled = object.value("enabled").toBool();
            units.append(entry);
        }
        if (QMessageBox::question(this, "Restore startup backup",
            QString("Restore %1 desktop files, %2 user unit files, and %3 unit states? Existing matching files will be replaced. System unit changes may ask for authentication.")
                .arg(files.size()).arg(userUnitFiles.size()).arg(units.size())) != QMessageBox::Yes) return;

        QStringList failures;
        for (auto it = files.begin(); it != files.end(); ++it) {
            if (!writeFile(autostartDir() + '/' + it.key(), it.value(), &error)) failures.append(error);
        }
        for (const QString &name : globalNames) {
            const QString local = autostartDir() + '/' + name;
            if (!files.contains(name) && QFileInfo::exists(local) && !QFile::remove(local))
                failures.append("Could not remove " + local);
        }
        for (auto it = userUnitFiles.begin(); it != userUnitFiles.end(); ++it) {
            if (!writeFile(userUnitDir() + '/' + it.key(), it.value(), &error)) failures.append(error);
        }
        if (!userUnitFiles.isEmpty() && !runProcess("/usr/bin/systemctl", {"--user", "daemon-reload"}, &error))
            failures.append("User systemd reload: " + error);
        refresh();
        for (const Entry &unit : units) {
            const auto current = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &e) {
                return e.kind == unit.kind && e.key == unit.key;
            });
            if (current == entries_.end()) { failures.append("Unit missing: " + unit.key); continue; }
            if (current->enabled != unit.enabled && !setUnitEnabled(unit, unit.enabled, &error))
                failures.append(unit.key + ": " + error);
        }
        refresh();
        if (failures.isEmpty()) statusBar()->showMessage("Backup restored", 10000);
        else QMessageBox::warning(this, "Restore incomplete", failures.join("\n"));
    }

    QList<Entry> entries_;
    QTableWidget *table_ = nullptr;
    QLineEdit *search_ = nullptr;
    QComboBox *source_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QPushButton *backup_ = nullptr;
    QPushButton *restore_ = nullptr;
    QPushButton *enable_ = nullptr;
    QPushButton *disable_ = nullptr;
    QPushButton *remove_ = nullptr;
};

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Vigilant");
    app.setApplicationVersion("0.2.0");
    app.setWindowIcon(QIcon(":/vigilant.png"));
    app.setStyle("Fusion");
    applyTheme(app);
    MainWindow window;
    window.show();
    return app.exec();
}
