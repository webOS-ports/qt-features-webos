// Copyright (c) 2026 Herman van Hazendonk <github.com@herrie.org>
//
// SPDX-License-Identifier: Apache-2.0

#include "qmapfile.h"

#include <QFile>

#include <cerrno>
#include <iterator>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

// The only translation unit that includes these: the first defines
// WebOSKeymapHandler's static tables, which may exist once per program.
#include "qevdevkeyboard_defaultmap_p.h"
#include "webos_keymap_p.h"

namespace {

template <typename T, std::size_t N>
std::vector<Mapping> toMappings(const T (&table)[N])
{
    std::vector<Mapping> v;
    v.reserve(N);
    for (const T &m : table)
        v.push_back({ m.keycode, m.unicode, m.qtcode, m.modifiers, m.flags, m.special });
    return v;
}

class Reader
{
public:
    explicit Reader(const QByteArray &b) : m_b(b) {}

    bool u8(quint8 *v) { return take(1, v); }
    bool u16(quint16 *v) { return take(2, v); }
    bool u32(quint32 *v) { return take(4, v); }
    qsizetype left() const { return m_b.size() - m_pos; }

private:
    template <typename T>
    bool take(int n, T *v)
    {
        if (left() < n)
            return false;
        quint64 r = 0;
        for (int i = 0; i < n; ++i)
            r = (r << 8) | quint8(m_b.at(m_pos + i));
        m_pos += n;
        *v = T(r);
        return true;
    }

    const QByteArray &m_b;
    qsizetype m_pos = 0;
};

} // namespace

bool parseQmap(const QByteArray &bytes, Qmap *out, std::string *why)
{
    Reader r(bytes);
    quint32 nmap = 0, ncomp = 0;
    if (!r.u32(&out->magic) || !r.u32(&out->version) || !r.u32(&nmap) || !r.u32(&ncomp)) {
        *why = "short header";
        return false;
    }
    // Checked before anything is reserved, so a corrupt count cannot ask for
    // gigabytes.
    if (quint64(nmap) * 12 + quint64(ncomp) * 6 != quint64(r.left())) {
        *why = "counts " + std::to_string(nmap) + "/" + std::to_string(ncomp)
             + " do not match the " + std::to_string(r.left()) + " bytes that follow";
        return false;
    }
    out->keymap.resize(nmap);
    for (Mapping &m : out->keymap) {
        if (!r.u16(&m.keycode) || !r.u16(&m.unicode) || !r.u32(&m.qtcode)
            || !r.u8(&m.modifiers) || !r.u8(&m.flags) || !r.u16(&m.special)) {
            *why = "short mapping";
            return false;
        }
    }
    out->compose.resize(ncomp);
    for (Composing &c : out->compose) {
        if (!r.u16(&c.first) || !r.u16(&c.second) || !r.u16(&c.result)) {
            *why = "short composing";
            return false;
        }
    }
    return true;
}

std::vector<Mapping> defaultKeymap()
{
    return toMappings(WebOSKeymapHandler::s_keymap_default);
}

std::vector<Mapping> webosKeymap()
{
    return toMappings(webos_keymap);
}

std::vector<Composing> defaultCompose()
{
    std::vector<Composing> v;
    v.reserve(std::size(WebOSKeymapHandler::s_keycompose_default));
    for (const auto &c : WebOSKeymapHandler::s_keycompose_default)
        v.push_back({ c.first, c.second, c.result });
    return v;
}

quint32 qmapMagic()
{
    return QEvdevKeyboardMap::FileMagic;
}

RunResult runGenerateQmap(const std::vector<std::string> &args, const RunOptions &opts)
{
    std::vector<std::string> owned{ GENERATE_QMAP };
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char *> argv;
    argv.reserve(owned.size() + 1);
    for (std::string &s : owned)
        argv.push_back(s.data());
    argv.push_back(nullptr);

    RunResult res;
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) != 0)
        return res;

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return res;
    }
    if (pid == 0) {
        // Only async-signal-safe calls from here: the parent has threads.
        umask(opts.umask);
        if (opts.fsizeLimit >= 0) {
            // Ignored, so write() fails with EFBIG as on a full disk instead
            // of the process being killed.
            signal(SIGXFSZ, SIG_IGN);
            struct rlimit rl = { rlim_t(opts.fsizeLimit), rlim_t(opts.fsizeLimit) };
            setrlimit(RLIMIT_FSIZE, &rl);
        }
        dup2(pipefd[1], STDERR_FILENO);
        execv(argv[0], argv.data());
        _exit(127);
    }

    close(pipefd[1]);
    char buf[512];
    for (;;) {
        const ssize_t n = read(pipefd[0], buf, sizeof buf);
        if (n > 0)
            res.err.append(buf, size_t(n));
        else if (n == 0 || errno != EINTR)
            break;
    }
    close(pipefd[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status))
        res.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        res.signal = WTERMSIG(status);
    return res;
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}
