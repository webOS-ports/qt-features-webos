// Copyright (c) 2017-2024 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include <QDataStream>
#include <QSaveFile>
#include <QString>

#include <cstddef>
#include <cstdio>

#include "qevdevkeyboard_defaultmap_p.h"
#include "webos_keymap_p.h"

template <typename T, std::size_t N>
static constexpr quint32 entries(const T (&)[N])
{
    return quint32(N);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <output.qmap>\n",
                     argc > 0 && argv[0] ? argv[0] : "generate_qmap");
        return 1;
    }

    const char *filename = argv[1];

    // QSaveFile, not QFile: QFile buffers what it is given and writes it out
    // only when it is closed, which its destructor does without telling
    // anyone whether that worked. A full disk then left an empty or partial
    // keymap behind and still exited 0; Qt's evdev handler refuses such a
    // file at boot and falls back to its built-in table, so every key added
    // below went missing with no error at build time. QSaveFile writes to a
    // temporary file and only renames it over the target once everything has
    // been written and flushed, so the target is either the whole keymap or
    // untouched, and a failure is reported here.
    QSaveFile f(QString::fromLocal8Bit(filename));

    if (!f.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "Could not open keymap file '%s': %s\n",
                     filename, qPrintable(f.errorString()));
        return 1;
    }

    // .qmap files have a very simple structure:
    // quint32 magic           (QKeyboard::FileMagic)
    // quint32 version         (1)
    // quint32 keymap_size     (# of struct QKeyboard::Mappings)
    // quint32 keycompose_size (# of struct QKeyboard::Composings)
    // all QKeyboard::Mappings via QDataStream::operator(<<|>>)
    // all QKeyboard::Composings via QDataStream::operator(<<|>>)

    const quint32 qmap_version = 1;
    const quint32 default_keymap_size = entries(WebOSKeymapHandler::s_keymap_default);
    const quint32 default_keycompose_size = entries(WebOSKeymapHandler::s_keycompose_default);
    const quint32 webos_keymap_size = entries(webos_keymap);
    const quint32 qmap_keymap_size = default_keymap_size + webos_keymap_size;

    QDataStream ds(&f);

    ds << QEvdevKeyboardMap::FileMagic << qmap_version << qmap_keymap_size << default_keycompose_size;

    // The default table goes first: Qt's handler takes the first entry it
    // finds for a keycode and modifier state, so an entry in webos_keymap[]
    // can add a keycode but not override one the default table already maps.
    for (quint32 i = 0; i < default_keymap_size; ++i)
        ds << WebOSKeymapHandler::s_keymap_default[i];
    for (quint32 i = 0; i < webos_keymap_size; ++i)
        ds << webos_keymap[i];
    for (quint32 i = 0; i < default_keycompose_size; ++i)
        ds << WebOSKeymapHandler::s_keycompose_default[i];

    // Without commit() the destructor discards the temporary file.
    if (ds.status() != QDataStream::Ok || !f.commit()) {
        std::fprintf(stderr, "Keymap file '%s' can not be written: %s\n",
                     filename, qPrintable(f.errorString()));
        return 1;
    }

    return 0;
}
