#pragma once
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>

enum class HostSystem { NixOS, Arch, Other };

inline HostSystem parseHostSystem(const QByteArray &release) {
    QString id;
    QStringList related;
    for (const auto &raw : release.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        const int separator = line.indexOf('=');
        if (separator < 0 || line.startsWith('#')) continue;
        QString value = line.mid(separator + 1).trimmed();
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                                 (value.front() == '\'' && value.back() == '\'')))
            value = value.mid(1, value.size() - 2);
        if (line.left(separator) == "ID") id = value;
        if (line.left(separator) == "ID_LIKE") related = value.simplified().split(' ');
    }
    if (id == "nixos" || related.contains("nixos")) return HostSystem::NixOS;
    if (id == "arch" || related.contains("arch")) return HostSystem::Arch;
    return HostSystem::Other;
}

inline HostSystem hostSystem() {
    static const HostSystem system = [] {
        QFile release("/etc/os-release");
        if (release.open(QIODevice::ReadOnly)) {
            const auto detected = parseHostSystem(release.readAll());
            if (detected != HostSystem::Other) return detected;
        }
        return QFileInfo::exists("/etc/NIXOS") ? HostSystem::NixOS : HostSystem::Other;
    }();
    return system;
}

inline bool inNixStore(const QString &path) {
    return path == "/nix/store" || path.startsWith("/nix/store/");
}

// Check ancestors too: Home Manager can link a whole directory into the store.
inline bool nixManagedPath(const QString &path) {
    if (path.isEmpty()) return false;
    QString current = QFileInfo(path).absoluteFilePath();
    while (true) {
        const QFileInfo info(current);
        if (inNixStore(QDir::cleanPath(current)) || inNixStore(info.canonicalFilePath()) ||
            (info.isSymLink() && inNixStore(QDir::cleanPath(info.symLinkTarget())))) return true;
        const QString parent = info.absolutePath();
        if (parent == current) return false;
        current = parent;
    }
}

inline QString managedEntryMessage() {
    return "Managed by Nix. Change this entry in your NixOS or Home Manager configuration and rebuild.";
}

inline QString hostExecutable(const QString &name) {
    QStringList directories;
    if (hostSystem() == HostSystem::NixOS)
        directories << "/run/wrappers/bin" << "/run/current-system/sw/bin" << "/run/current-system/systemd/bin";
    directories << "/usr/bin";
    for (const auto &directory : directories) {
        const QFileInfo candidate(directory + '/' + name);
        if (candidate.isFile() && candidate.isExecutable()) return candidate.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(name);
}
