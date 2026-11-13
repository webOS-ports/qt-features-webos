// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0

#include <QGuiApplication>

#include <gtest/gtest.h>

int main(int argc, char **argv)
{
    // The evdev handler hands its key events to QGuiApplication; offscreen
    // needs no display.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    ::testing::InitGoogleTest(&argc, argv);
    QGuiApplication app(argc, argv);
    return RUN_ALL_TESTS();
}
