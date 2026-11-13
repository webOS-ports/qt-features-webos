// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0
//
// The patterns the Coccinelle rules look for. Never built; each rule has to
// match something here, or it is reported as broken.

#include <QDataStream>
#include <QFile>

int writeKeymap(const char *filename)
{
    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("Could not open keymap file '%s'", filename);
    QDataStream ds(&f);
    ds << quint32(1);
    return 0;
}
