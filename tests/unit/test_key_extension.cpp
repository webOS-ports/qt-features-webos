// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// include/qweboskeyextension.h: the webOS key codes other components switch
// on, added to namespace Qt.

#include <qweboskeyextension.h>

#include <QFile>
#include <QMetaEnum>
#include <QRegularExpression>

#include <gtest/gtest.h>

#include <map>
#include <set>

// The header compiles, puts its enum in Qt, and the values are what callers
// were built against.
static_assert(Qt::Key_webOS_PowerOnOff == 0x01200000, "first webOS key moved");
static_assert(Qt::Key_webOS_Back == 0x01200003, "Key_webOS_Back moved");
static_assert(sizeof(Qt::KeyWebOS) <= sizeof(int), "KeyWebOS no longer fits a QKeyEvent key");

namespace {

std::multimap<quint32, QString> headerKeys()
{
    QFile f(QStringLiteral(SRC_ROOT "/include/qweboskeyextension.h"));
    std::multimap<quint32, QString> keys;
    if (!f.open(QIODevice::ReadOnly))
        return keys;
    const QString text = QString::fromUtf8(f.readAll());
    static const QRegularExpression re(QStringLiteral(R"((Key_webOS_\w+)\s*=\s*(0x[0-9a-fA-F]+))"));
    for (auto it = re.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        keys.emplace(m.captured(2).toUInt(nullptr, 16), m.captured(1));
    }
    return keys;
}

TEST(KeyExtension, EveryKeyHasAnExplicitValue)
{
    QFile f(QStringLiteral(SRC_ROOT "/include/qweboskeyextension.h"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    // An entry without "= 0x..." would take the previous value plus one, and
    // collide silently with whatever comes next.
    const qsizetype names = text.count(QRegularExpression(QStringLiteral(R"(\bKey_webOS_\w+)")));
    EXPECT_EQ(qsizetype(headerKeys().size()), names);
    EXPECT_GT(names, 300);
}

TEST(KeyExtension, NoNewValueIsShared)
{
    // Shared values that came with webOS OSE's first release, each a key of
    // a different product (signage, wearable) that never meet on one device.
    // They are public API and stay; this only stops a new one joining them.
    const std::map<quint32, std::set<QString>> known = {
        { 0x01200309, { "Key_webOS_SFP", "Key_webOS_UsbSwitchingLong" } },
        { 0x01200401, { "Key_webOS_BrightnessDown", "Key_webOS_Wearable_F2" } },
        { 0x01200402, { "Key_webOS_BrightnessUp", "Key_webOS_Wearable_Home" } },
        { 0x01200403, { "Key_webOS_Tile", "Key_webOS_Wearable_SRE" } },
        { 0x01200404, { "Key_webOS_WhiteBalance", "Key_webOS_Wearable_Palm" } },
        { 0x01200405, { "Key_webOS_AutoConfigure", "Key_webOS_Wearable_F3", "Key_webOS_Wearable_Next" } },
        { 0x01200406, { "Key_webOS_S_Menu", "Key_webOS_Wearable_F4" } },
    };
    const auto keys = headerKeys();
    std::map<quint32, std::set<QString>> shared;
    for (auto it = keys.begin(); it != keys.end(); it = keys.upper_bound(it->first)) {
        if (keys.count(it->first) < 2)
            continue;
        for (auto r = keys.equal_range(it->first); r.first != r.second; ++r.first)
            shared[it->first].insert(r.first->second);
    }
    EXPECT_EQ(shared, known);
}

TEST(KeyExtension, NamesAreUnique)
{
    std::set<QString> names;
    for (const auto &[value, name] : headerKeys())
        EXPECT_TRUE(names.insert(name).second) << qPrintable(name);
}

TEST(KeyExtension, NoValueIsAQtKey)
{
    // Qt::Key and KeyWebOS share namespace Qt and QKeyEvent::key(): a value
    // Qt itself uses would make a webOS key indistinguishable from it.
    const QMetaEnum qtKeys = QMetaEnum::fromType<Qt::Key>();
    ASSERT_TRUE(qtKeys.isValid());
    for (const auto &[value, name] : headerKeys()) {
        const char *clash = qtKeys.valueToKey(int(value));
        EXPECT_EQ(clash, nullptr) << qPrintable(name) << " is Qt::" << clash;
        EXPECT_EQ(value & 0xfff00000u, 0x01200000u) << qPrintable(name);
    }
}

} // namespace
