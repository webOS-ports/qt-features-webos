// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// The generated keymap through Qt's own evdev keyboard handler, as the
// compositor loads it: the keys come out of Qt as the key events the shell
// sees, or they do not come out at all.

#include "qmapfile.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QWindow>

#include <QtInputSupport/private/qevdevkeyboardhandler_p.h>

#include <gtest/gtest.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>

#include <memory>
#include <vector>

#ifndef KEY_EMOJI_PICKER
#define KEY_EMOJI_PICKER 0x249
#endif

namespace {

struct Seen {
    QEvent::Type type;
    int key;
    quint32 scancode;
    QString text;
};

class KeyRecorder : public QObject
{
public:
    std::vector<Seen> seen;

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::KeyPress || e->type() == QEvent::KeyRelease) {
            auto *k = static_cast<QKeyEvent *>(e);
            seen.push_back({ e->type(), k->key(), k->nativeScanCode(), k->text() });
        }
        return QObject::eventFilter(o, e);
    }
};

class EvdevHandler : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_NE(qGuiApp, nullptr) << "tests need the QGuiApplication from test_main.cpp";
        ASSERT_TRUE(m_dir.isValid());
        m_file = m_dir.filePath(QStringLiteral("webos-keymap.qmap"));
        const RunResult r = runGenerateQmap({ m_file.toStdString() });
        ASSERT_EQ(r.exitCode, 0) << r.err;

        m_window = std::make_unique<QWindow>();
        m_window->resize(10, 10);
        m_window->show();
        m_window->requestActivate();
        for (int i = 0; i < 50 && QGuiApplication::focusWindow() != m_window.get(); ++i)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        ASSERT_EQ(QGuiApplication::focusWindow(), m_window.get());
        m_window->installEventFilter(&m_rec);

        // The handler watches the fd for input_events; a pipe nobody writes
        // to. Keys go in through processKeycode(), which is what reading an
        // EV_KEY event calls.
        int p[2];
        ASSERT_EQ(pipe2(p, O_CLOEXEC | O_NONBLOCK), 0);
        m_writeEnd = p[1];
        QFdContainer fd(p[0]);
        m_handler = std::make_unique<QEvdevKeyboardHandler>(QStringLiteral("test"), fd, true, true, QString());
    }

    void TearDown() override
    {
        m_handler.reset();
        if (m_writeEnd >= 0)
            close(m_writeEnd);
        m_window.reset();
    }

    // Press and release; what reached the window.
    std::vector<Seen> tap(quint16 keycode, const std::vector<quint16> &held = {})
    {
        for (quint16 h : held)
            m_handler->processKeycode(h, true, false);
        m_handler->processKeycode(keycode, true, false);
        m_handler->processKeycode(keycode, false, false);
        for (quint16 h : held)
            m_handler->processKeycode(h, false, false);
        QCoreApplication::processEvents();
        std::vector<Seen> out;
        for (const Seen &s : m_rec.seen)
            if (s.scancode == quint32(keycode) + 8)
                out.push_back(s);
        m_rec.seen.clear();
        return out;
    }

    QTemporaryDir m_dir;
    QString m_file;
    std::unique_ptr<QWindow> m_window;
    KeyRecorder m_rec;
    int m_writeEnd = -1;
    std::unique_ptr<QEvdevKeyboardHandler> m_handler;
};

TEST_F(EvdevHandler, QtLoadsTheGeneratedKeymap)
{
    EXPECT_TRUE(m_handler->loadKeymap(m_file));
}

TEST_F(EvdevHandler, WebOSKeysReachTheShellAsTheirQtKeys)
{
    ASSERT_TRUE(m_handler->loadKeymap(m_file));
    const std::vector<std::pair<quint16, int>> keys = {
        { KEY_HANGEUL, Qt::Key_Hangul },     { KEY_LEFTMETA, Qt::Key_Super_L },
        { KEY_BACK, Qt::Key_Back },          { KEY_HOMEPAGE, Qt::Key_HomePage },
        { KEY_SEND, Qt::Key_Call },          { KEY_APPSELECT, Qt::Key_Menu },
        { KEY_EMOJI_PICKER, Qt::Key_Launch0 }, { KEY_MACRO, Qt::Key_Launch2 },
        { KEY_PROG1, Qt::Key_Launch1 },      { KEY_CAMERA, Qt::Key_Camera },
        { KEY_CAMERA_FOCUS, Qt::Key_CameraFocus }, { 250, Qt::Key_Launch1 },
    };
    for (const auto &[keycode, qtkey] : keys) {
        const std::vector<Seen> s = tap(keycode);
        ASSERT_EQ(s.size(), 2u) << "keycode " << keycode;
        EXPECT_EQ(s[0].type, QEvent::KeyPress) << keycode;
        EXPECT_EQ(s[1].type, QEvent::KeyRelease) << keycode;
        EXPECT_EQ(s[0].key, qtkey) << "keycode " << keycode;
        EXPECT_EQ(s[0].text, QString()) << keycode;
    }
}

TEST_F(EvdevHandler, WithoutTheKeymapThoseKeysAreDropped)
{
    // What a phone gets when the qmap is missing or refused: Qt's built-in
    // table, which has none of them. This is why a bad qmap must fail the
    // build rather than be installed.
    for (quint16 keycode : { quint16(KEY_BACK), quint16(KEY_HOMEPAGE), quint16(KEY_SEND), quint16(KEY_CAMERA), quint16(250) })
        EXPECT_TRUE(tap(keycode).empty()) << keycode;
}

TEST_F(EvdevHandler, DefaultKeysAreUnchanged)
{
    ASSERT_TRUE(m_handler->loadKeymap(m_file));
    auto a = tap(KEY_A);
    ASSERT_EQ(a.size(), 2u);
    EXPECT_EQ(a[0].key, Qt::Key_A);
    EXPECT_EQ(a[0].text, QStringLiteral("a"));

    auto A = tap(KEY_A, { KEY_LEFTSHIFT });
    ASSERT_EQ(A.size(), 2u);
    EXPECT_EQ(A[0].text, QStringLiteral("A"));

    auto v = tap(KEY_VOLUMEUP);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].key, Qt::Key_VolumeUp);

    // A webOS key with a modifier held still finds its plain entry.
    auto b = tap(KEY_BACK, { KEY_LEFTSHIFT });
    ASSERT_EQ(b.size(), 2u);
    EXPECT_EQ(b[0].key, Qt::Key_Back);
}

TEST_F(EvdevHandler, UnmappedKeycodeStaysUnmapped)
{
    ASSERT_TRUE(m_handler->loadKeymap(m_file));
    EXPECT_TRUE(tap(KEY_MENU).empty()); // in neither table, see 0004
}

TEST_F(EvdevHandler, QtRefusesATruncatedKeymap)
{
    // What the old generate_qmap could leave behind on a full disk.
    QByteArray bytes = readFile(m_file);
    ASSERT_GT(bytes.size(), 4096);
    const QString cut = m_dir.filePath(QStringLiteral("cut.qmap"));
    QFile f(cut);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes.left(4096));
    f.close();
    EXPECT_FALSE(m_handler->loadKeymap(cut));
}

} // namespace
