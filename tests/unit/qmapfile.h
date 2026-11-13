// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef QMAPFILE_H
#define QMAPFILE_H

#include <QByteArray>
#include <QString>

#include <string>
#include <sys/types.h>
#include <vector>

// The keymap entries as generate_qmap compiles them in, and as the tests read
// them back. Kept separate from QEvdevKeyboardMap/QKeyboardMap so the reader
// below is not QDataStream checking itself.
struct Mapping {
    quint16 keycode;
    quint16 unicode;
    quint32 qtcode;
    quint8 modifiers;
    quint8 flags;
    quint16 special;

    bool operator==(const Mapping &o) const
    {
        return keycode == o.keycode && unicode == o.unicode && qtcode == o.qtcode
            && modifiers == o.modifiers && flags == o.flags && special == o.special;
    }
};

struct Composing {
    quint16 first;
    quint16 second;
    quint16 result;

    bool operator==(const Composing &o) const
    {
        return first == o.first && second == o.second && result == o.result;
    }
};

struct Qmap {
    quint32 magic = 0;
    quint32 version = 0;
    std::vector<Mapping> keymap;
    std::vector<Composing> compose;
};

// Bytes on disk: four big-endian quint32 (magic, version, keymap count,
// compose count), then 12 bytes per mapping and 6 per composing. Fails on a
// short file and on bytes left over.
bool parseQmap(const QByteArray &bytes, Qmap *out, std::string *why);

// The tables generate_qmap was built from (the same headers, compiled here).
std::vector<Mapping> defaultKeymap();
std::vector<Composing> defaultCompose();
std::vector<Mapping> webosKeymap();
quint32 qmapMagic();

struct RunOptions {
    long fsizeLimit = -1;   // RLIMIT_FSIZE in bytes, SIGXFSZ ignored; -1: none
    mode_t umask = 022;
};

struct RunResult {
    int exitCode = -1;      // -1 when it did not exit normally
    int signal = 0;         // the signal that ended it, if any
    std::string err;        // its stderr
};

// Runs the generate_qmap built next to the tests, with args after argv[0].
RunResult runGenerateQmap(const std::vector<std::string> &args, const RunOptions &opts = {});

QByteArray readFile(const QString &path);

#endif // QMAPFILE_H
