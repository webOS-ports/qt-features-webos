// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// generate_qmap as luna-surfacemanager's recipe runs it: one argument, the
// file to write. What it writes, and what it does when it cannot.

#include "qmapfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <sys/stat.h>

namespace {

class GenerateQmap : public ::testing::Test
{
protected:
    void SetUp() override { ASSERT_TRUE(m_dir.isValid()); }

    QString path(const QString &name) const { return m_dir.filePath(name); }
    std::string spath(const QString &name) const { return path(name).toStdString(); }

    // Everything in the directory but the names given: what a failed run
    // left behind.
    QStringList strays(const QStringList &expected = {}) const
    {
        QStringList l = QDir(m_dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::System);
        for (const QString &e : expected)
            l.removeAll(e);
        return l;
    }

    QTemporaryDir m_dir;
};

TEST_F(GenerateQmap, WritesTheDefaultTableThenWebOSThenCompose)
{
    const RunResult r = runGenerateQmap({ spath("k.qmap") });
    ASSERT_EQ(r.exitCode, 0) << r.err;
    EXPECT_EQ(r.err, "");

    Qmap q;
    std::string why;
    ASSERT_TRUE(parseQmap(readFile(path("k.qmap")), &q, &why)) << why;

    EXPECT_EQ(q.magic, qmapMagic());
    EXPECT_EQ(q.magic, 0x514d4150u); // 'QMAP', what Qt's loader checks for
    EXPECT_EQ(q.version, 1u);

    std::vector<Mapping> expected = defaultKeymap();
    const std::vector<Mapping> webos = webosKeymap();
    expected.insert(expected.end(), webos.begin(), webos.end());
    EXPECT_EQ(q.keymap, expected);
    EXPECT_EQ(q.compose, defaultCompose());
}

TEST_F(GenerateQmap, OutputIsReproducible)
{
    ASSERT_EQ(runGenerateQmap({ spath("a.qmap") }).exitCode, 0);
    ASSERT_EQ(runGenerateQmap({ spath("b.qmap") }).exitCode, 0);
    const QByteArray a = readFile(path("a.qmap"));
    EXPECT_FALSE(a.isEmpty());
    EXPECT_EQ(a, readFile(path("b.qmap")));
}

TEST_F(GenerateQmap, ReplacesAnExistingFileWhole)
{
    {
        QFile f(path("k.qmap"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(qsizetype(64) * 1024, 'x'));
    }
    ASSERT_EQ(runGenerateQmap({ spath("k.qmap") }).exitCode, 0);
    Qmap q;
    std::string why;
    EXPECT_TRUE(parseQmap(readFile(path("k.qmap")), &q, &why)) << why;
    EXPECT_EQ(strays({ "k.qmap" }), QStringList());
}

TEST_F(GenerateQmap, NewFileGetsTheUmasksPermissions)
{
    // The recipe installs the file as written; it has to come out 0644 under
    // the build's umask, not 0600 as a temporary file would.
    RunOptions o;
    o.umask = 022;
    ASSERT_EQ(runGenerateQmap({ spath("k.qmap") }, o).exitCode, 0);
    struct stat st;
    ASSERT_EQ(stat(path("k.qmap").toLocal8Bit().constData(), &st), 0);
    EXPECT_EQ(st.st_mode & 07777, 0644u);
}

TEST_F(GenerateQmap, WrongArgumentCountFailsCleanly)
{
    for (const std::vector<std::string> &args :
         { std::vector<std::string>{}, std::vector<std::string>{ spath("a"), spath("b") } }) {
        const RunResult r = runGenerateQmap(args);
        EXPECT_EQ(r.exitCode, 1) << "signal " << r.signal;
        EXPECT_NE(r.err.find("usage:"), std::string::npos) << r.err;
    }
    EXPECT_EQ(strays(), QStringList());
}

TEST_F(GenerateQmap, MissingDirectoryFails)
{
    const RunResult r = runGenerateQmap({ spath("no/such/dir/k.qmap") });
    EXPECT_EQ(r.exitCode, 1) << "signal " << r.signal;
    EXPECT_NE(r.err.find("no/such/dir/k.qmap"), std::string::npos) << r.err;
}

TEST_F(GenerateQmap, NonRegularTargetFails)
{
    // /dev/full takes the open and fails every write: the old QFile code
    // reported success on it.
    const RunResult r = runGenerateQmap({ "/dev/full" });
    EXPECT_EQ(r.exitCode, 1) << "signal " << r.signal << " " << r.err;
}

TEST_F(GenerateQmap, WriteFailureFailsAndKeepsTheOldFile)
{
    // A disk filling up part-way through. The old code exited 0 here with the
    // keymap cut short at the limit, which Qt then refuses at boot.
    ASSERT_EQ(runGenerateQmap({ spath("k.qmap") }).exitCode, 0);
    const QByteArray good = readFile(path("k.qmap"));
    ASSERT_GT(good.size(), 4096);

    RunOptions o;
    o.fsizeLimit = 4096;
    const RunResult r = runGenerateQmap({ spath("k.qmap") }, o);
    EXPECT_EQ(r.exitCode, 1) << "signal " << r.signal;
    EXPECT_NE(r.err.find("can not be written"), std::string::npos) << r.err;
    EXPECT_EQ(readFile(path("k.qmap")), good);
    EXPECT_EQ(strays({ "k.qmap" }), QStringList());
}

TEST_F(GenerateQmap, WriteFailureLeavesNoFileWhereThereWasNone)
{
    RunOptions o;
    o.fsizeLimit = 1024;
    EXPECT_EQ(runGenerateQmap({ spath("k.qmap") }, o).exitCode, 1);
    EXPECT_EQ(strays(), QStringList());
}

TEST_F(GenerateQmap, WritesThroughASymlink)
{
    // As QFile did: the link stays, its target gets the keymap.
    ASSERT_TRUE(QFile::link(path("real.qmap"), path("link.qmap")));
    {
        QFile real(path("real.qmap"));
        ASSERT_TRUE(real.open(QIODevice::WriteOnly));
    }
    ASSERT_EQ(runGenerateQmap({ spath("link.qmap") }).exitCode, 0);
    EXPECT_TRUE(QFileInfo(path("link.qmap")).isSymLink());
    Qmap q;
    std::string why;
    EXPECT_TRUE(parseQmap(readFile(path("real.qmap")), &q, &why)) << why;
}

} // namespace
