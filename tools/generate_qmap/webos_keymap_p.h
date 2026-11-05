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

#ifndef WEBOS_KEYMAP_P_H
#define WEBOS_KEYMAP_P_H

// Keymap for WebOS specific
// { keycode, unicode, qtcode, modifiers, flags, special }
const QEvdevKeyboardMap::Mapping webos_keymap[] = {
    { 122, 0xffff, Qt::Key_Hangul,  0x00, 0x00, 0x0000 }, // Hangul
    { 125, 0xffff, Qt::Key_Super_L, 0x00, 0x00, 0x0000 }, // LEFT_META (Windows key)

    // The hardware toolbelt of a phone with physical call keys. Qt's default
    // table has none of these, so without them the keys never become Qt key
    // events at all. What each one is used for is decided per device, above
    // this layer: these are the keycodes' own meanings, not one phone's labels.
    { 158, 0xffff, Qt::Key_Back,     0x00, 0x00, 0x0000 }, // KEY_BACK
    { 172, 0xffff, Qt::Key_HomePage, 0x00, 0x00, 0x0000 }, // KEY_HOMEPAGE
    { 231, 0xffff, Qt::Key_Call,     0x00, 0x00, 0x0000 }, // KEY_SEND
    { 580, 0xffff, Qt::Key_Menu,     0x00, 0x00, 0x0000 }, // KEY_APPSELECT

    // Sym tapped on its own, on a keyboard whose Sym selects the layer
    // printed on the key faces rather than acting as a modifier; see the
    // bbqX0kbd patches. Key_Launch0 carries no meaning of its own, which
    // leaves what the key does to the layer that knows the keyboard.
    { 585, 0xffff, Qt::Key_Launch0,  0x00, 0x00, 0x0000 }, // KEY_EMOJI_PICKER

    // Camera and programmable keys. As above, each keycode gets its own Qt
    // key and what the key does is the shell's per-device decision: on one
    // phone KEY_CAMERA is a shutter key, on another a privacy switch.
    // Key_Launch0 is taken by the emoji picker, so the programmable keys
    // start at Key_Launch1.
    { 112, 0xffff, Qt::Key_Launch2,     0x00, 0x00, 0x0000 }, // KEY_MACRO
    { 148, 0xffff, Qt::Key_Launch1,     0x00, 0x00, 0x0000 }, // KEY_PROG1
    { 212, 0xffff, Qt::Key_Camera,      0x00, 0x00, 0x0000 }, // KEY_CAMERA
    { 528, 0xffff, Qt::Key_CameraFocus, 0x00, 0x00, 0x0000 }  // KEY_CAMERA_FOCUS
};

#endif // WEBOS_KEYMAP_P_H
