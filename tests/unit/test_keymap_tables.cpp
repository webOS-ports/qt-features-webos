// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// The two tables generate_qmap writes: the copy of Qt's default keymap, and
// webos_keymap[] after it.

#include "qmapfile.h"

#include <QFile>
#include <QRegularExpression>
#include <Qt>

#include <gtest/gtest.h>
#include <linux/input.h>

#include <map>
#include <set>
#include <utility>

#ifndef KEY_EMOJI_PICKER
#define KEY_EMOJI_PICKER 0x249 // linux 5.13
#endif

namespace {

// One array's initialiser from a header, whitespace collapsed.
QString tableBody(const QString &file, const QString &array)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("<cannot read %1>").arg(file);
    const QString text = QString::fromUtf8(f.readAll());
    const qsizetype start = text.indexOf(array + QStringLiteral("[] = {"));
    if (start < 0)
        return QStringLiteral("<no %1 in %2>").arg(array, file);
    const qsizetype end = text.indexOf(QStringLiteral("\n};"), start);
    QString body = text.mid(start, end - start);
    body.remove(0, body.indexOf(QLatin1Char('{')));
    body.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    return body.simplified();
}

TEST(KeymapTables, DefaultTableIsQtsOwn)
{
    // The copy 0001 made must stay what Qt's evdev handler has built in, or
    // the qmap changes keys nobody meant to change. Compared against the
    // header of the Qt these tests are built with.
    const QString ours = QStringLiteral(SRC_ROOT "/tools/generate_qmap/qevdevkeyboard_defaultmap_p.h");
    const QString qts = QStringLiteral(QT_DEFAULTMAP_HEADER);
    for (const QString &array : { QStringLiteral("s_keymap_default"), QStringLiteral("s_keycompose_default") }) {
        const QString a = tableBody(ours, array);
        const QString b = tableBody(qts, array);
        EXPECT_FALSE(a.startsWith(QLatin1Char('<'))) << qPrintable(a);
        EXPECT_EQ(a, b) << qPrintable(array);
    }
}

TEST(KeymapTables, DefaultTableIsNotEmpty)
{
    EXPECT_GT(defaultKeymap().size(), 500u);
    EXPECT_GT(defaultCompose().size(), 100u);
}

TEST(KeymapTables, WebOSEntriesAreTheOnesIntended)
{
    // keycode (linux/input.h) -> Qt key; each one plain, no text, no flags.
    const std::map<quint16, quint32> expected = {
        { KEY_HANGEUL, Qt::Key_Hangul },
        { KEY_LEFTMETA, Qt::Key_Super_L },
        { KEY_BACK, Qt::Key_Back },
        { KEY_HOMEPAGE, Qt::Key_HomePage },
        { KEY_SEND, Qt::Key_Call },
        { KEY_APPSELECT, Qt::Key_Menu },
        { KEY_EMOJI_PICKER, Qt::Key_Launch0 },
        { KEY_MACRO, Qt::Key_Launch2 },
        { KEY_PROG1, Qt::Key_Launch1 },
        { KEY_CAMERA, Qt::Key_Camera },
        { KEY_CAMERA_FOCUS, Qt::Key_CameraFocus },
        { 250, Qt::Key_Launch1 }, // MediaTek's programmable key, no Linux name
    };
    std::map<quint16, quint32> actual;
    for (const Mapping &m : webosKeymap()) {
        EXPECT_TRUE(actual.emplace(m.keycode, m.qtcode).second) << "keycode " << m.keycode << " twice";
        EXPECT_EQ(m.unicode, 0xffff) << m.keycode;
        EXPECT_EQ(m.modifiers, 0) << m.keycode;
        EXPECT_EQ(m.flags, 0) << m.keycode;
        EXPECT_EQ(m.special, 0) << m.keycode;
    }
    EXPECT_EQ(actual, expected);
}

TEST(KeymapTables, WebOSEntriesAreNotShadowedByTheDefaultTable)
{
    // Qt takes the first entry for a keycode and modifier state, and the
    // default table is written first: an entry here for a pair it already
    // has would never be used.
    std::set<std::pair<quint16, quint8>> defaults;
    for (const Mapping &m : defaultKeymap())
        defaults.emplace(m.keycode, m.modifiers);
    for (const Mapping &m : webosKeymap())
        EXPECT_EQ(defaults.count({ m.keycode, m.modifiers }), 0u)
            << "keycode " << m.keycode << " is already in Qt's default table";
}

TEST(KeymapTables, WebOSQtKeysAreDistinctFromTheDefaultTables)
{
    // Each keycode gets a Qt key nothing else produces, so the shell can
    // tell them apart. KEY_PROG1 and MediaTek's 250 are the one pair that
    // shares, on purpose: the same key on different phones.
    std::set<quint32> defaultCodes;
    for (const Mapping &m : defaultKeymap())
        defaultCodes.insert(m.qtcode);
    std::multimap<quint32, quint16> byCode;
    for (const Mapping &m : webosKeymap()) {
        EXPECT_EQ(defaultCodes.count(m.qtcode), 0u)
            << "keycode " << m.keycode << ": Qt key 0x" << std::hex << m.qtcode
            << " is also in the default table";
        byCode.emplace(m.qtcode, m.keycode);
    }
    for (auto it = byCode.begin(); it != byCode.end(); it = byCode.upper_bound(it->first)) {
        if (byCode.count(it->first) == 1)
            continue;
        std::set<quint16> codes;
        for (auto r = byCode.equal_range(it->first); r.first != r.second; ++r.first)
            codes.insert(r.first->second);
        EXPECT_EQ(codes, (std::set<quint16>{ KEY_PROG1, 250 }))
            << "Qt key 0x" << std::hex << it->first << " shared";
    }
}

TEST(KeymapTables, KeycodesAreOnesTheKernelCanSend)
{
    for (const Mapping &m : webosKeymap()) {
        EXPECT_GT(m.keycode, KEY_RESERVED);
        EXPECT_LE(m.keycode, KEY_MAX);
    }
}

} // namespace
