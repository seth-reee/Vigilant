#define VIGILANT_TESTING
#include "../main.cpp"
#include <QtTest>
#include <QTemporaryDir>
#include <unistd.h>

class PlatformTest : public QObject {
    Q_OBJECT
private slots:
    void detectsHostFromReleaseData() {
        QCOMPARE(parseHostSystem("ID=nixos\n"), HostSystem::NixOS);
        QCOMPARE(parseHostSystem("ID=fork\nID_LIKE=\"nixos linux\"\n"), HostSystem::NixOS);
        QCOMPARE(parseHostSystem("ID=arch\n"), HostSystem::Arch);
        QCOMPARE(parseHostSystem("ID=omarchy\nID_LIKE='arch linux'\n"), HostSystem::Arch);
        QCOMPARE(parseHostSystem("# ID=nixos\nID=ubuntu\nID_LIKE=debian\n"), HostSystem::Other);
        QCOMPARE(parseHostSystem("NAME=\"Arch or NixOS\"\nID=other\n"), HostSystem::Other);
        QCOMPARE(parseHostSystem({}), HostSystem::Other);
    }

    void distinguishesNixStoreFromSimilarNames() {
        QVERIFY(inNixStore("/nix/store/abc-unit"));
        QVERIFY(!inNixStore("/nix/store-backup/unit"));
        QVERIFY(!nixManagedPath(""));
        QVERIFY(nixManagedPath("/nix/store/vigilant-test-missing/unit.service"));
        QTemporaryDir root;
        QVERIFY(root.isValid());
        QVERIFY(!nixManagedPath(root.filePath("mutable.service")));
    }

    void protectsManagedLinksAndDirectories() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString link = root.filePath("managed.desktop");
        QCOMPARE(::symlink("/nix/store/vigilant-test-missing/entry.desktop", link.toLocal8Bit().constData()), 0);
        QVERIFY(nixManagedPath(link));
        QString error;
        QVERIFY(!writeFile(link, "replacement", &error));
        QVERIFY(error.contains("Managed by Nix"));
        QVERIFY(!removeStartupFile(link, &error));
        QVERIFY(QFileInfo(link).isSymLink());
        const QString directory = root.filePath("systemd");
        QCOMPARE(::symlink("/nix/store/vigilant-test-missing/units", directory.toLocal8Bit().constData()), 0);
        QVERIFY(nixManagedPath(directory + "/new.service"));
        QVERIFY(!writeFile(directory + "/new.service", "replacement", &error));
        QVERIFY(QFileInfo(directory).isSymLink());
    }

    void mutableFilesStillWork() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString path = root.filePath("autostart/manual.desktop");
        QString error;
        QVERIFY(writeFile(path, "original", &error));
        QVERIFY(writeFile(path, "restored", &error));
        QByteArray data;
        QVERIFY(readFile(path, &data, &error));
        QCOMPARE(data, QByteArray("restored"));
        QVERIFY(removeStartupFile(path, &error));
        QVERIFY(!QFileInfo::exists(path));
    }

    void systemEnablementDependsOnHost() {
        Entry system;
        system.kind = Kind::SystemUnit;
        system.key = "example.service";
        QVERIFY(managedEntry(system, HostSystem::NixOS));
        QVERIFY(!managedEntry(system, HostSystem::Arch));
        Entry user = system;
        user.kind = Kind::UserUnit;
        QVERIFY(!managedEntry(user, HostSystem::NixOS));
        user.path = "/nix/store/vigilant-test-missing/example.service";
        QVERIFY(managedEntry(user, HostSystem::Arch));
        QVERIFY(managedEntry(user, HostSystem::NixOS));
    }

    void oldBackupsCannotShadowManagedEntries() {
        Entry desktop;
        desktop.kind = Kind::Desktop;
        desktop.key = "managed.desktop";
        desktop.path = "/nix/store/vigilant-test-missing/managed.desktop";
        QString error;
        QVERIFY(!restoreStartupFile({desktop}, Kind::Desktop, desktop.key, "old backup", &error));
        QVERIFY(error.contains("Managed by Nix"));
        Entry userUnit;
        userUnit.kind = Kind::UserUnit;
        userUnit.key = "managed.service";
        userUnit.managed = true;
        QVERIFY(!restoreStartupFile({userUnit}, Kind::UserUnit, userUnit.key, "old backup", &error));
        QVERIFY(error.contains("Managed by Nix"));
    }

    void mutationGuardsApplyWithoutUi() {
        Entry entry;
        entry.kind = Kind::UserUnit;
        entry.key = "example.service";
        entry.managed = true;
        QString error;
        QVERIFY(!setUnitEnabled(entry, true, &error));
        QVERIFY(error.contains("Managed by Nix"));
        entry.kind = Kind::Desktop;
        QVERIFY(!setDesktopHidden(entry, true, &error));
        QVERIFY(error.contains("Managed by Nix"));
    }
};
QTEST_GUILESS_MAIN(PlatformTest)
#include "platform_test.moc"
