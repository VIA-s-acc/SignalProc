#include <algorithm>
#include <array>
#include <string_view>

#include <fmt/format.h>

#include "i18n/i18n.hpp"
#include "python/script.hpp"

namespace signals::python {
namespace {

constexpr std::array<std::string_view, 198> kModules{
    "abc", "annotationlib", "antigravity", "argparse", "array", "ast", "asyncio", "atexit", "base64", "bdb", "binascii", "bisect", "builtins", "bz2",
    "cProfile", "calendar", "cmath", "cmd", "code", "codecs", "codeop", "collections", "colorsys", "compileall", "compression", "concurrent",
    "configparser", "contextlib", "contextvars", "copy", "copyreg", "csv", "ctypes", "curses", "dataclasses", "datetime", "dbm", "decimal", "difflib",
    "dis", "doctest", "email", "encodings", "ensurepip", "enum", "errno", "faulthandler", "fcntl", "filecmp", "fileinput", "fnmatch", "fractions",
    "ftplib", "functools", "gc", "genericpath", "getopt", "getpass", "gettext", "glob", "graphlib", "grp", "gzip", "hashlib", "heapq", "hmac", "html",
    "http", "idlelib", "imaplib", "importlib", "inspect", "io", "ipaddress", "itertools", "json", "keyword", "linecache", "locale", "logging", "lzma",
    "mailbox", "marshal", "math", "matplotlib", "mimetypes", "mmap", "modulefinder", "msvcrt", "multiprocessing", "netrc", "nt", "ntpath", "nturl2path",
    "numbers", "numpy", "opcode", "operator", "optparse", "os", "pathlib", "pdb", "pickle", "pickletools", "pkgutil", "platform", "plistlib", "poplib",
    "posix", "posixpath", "pprint", "profile", "pstats", "pty", "pwd", "py_compile", "pyclbr", "pydoc", "pydoc_data", "pyexpat", "pywt", "queue",
    "quopri", "random", "re", "readline", "reprlib", "resource", "rlcompleter", "runpy", "sched", "scipy", "secrets", "select", "selectors", "shelve",
    "shlex", "shutil", "signal", "site", "smtplib", "socket", "socketserver", "sqlite3", "sre_compile", "sre_constants", "sre_parse", "ssl", "stat",
    "statistics", "string", "stringprep", "struct", "subprocess", "symtable", "sys", "sysconfig", "syslog", "tabnanny", "tarfile", "tempfile", "termios",
    "textwrap", "this", "threading", "time", "timeit", "tkinter", "token", "tokenize", "tomllib", "trace", "traceback", "tracemalloc", "tty", "turtle",
    "turtledemo", "types", "typing", "unicodedata", "unittest", "urllib", "uuid", "venv", "warnings", "wave", "weakref", "webbrowser", "winreg",
    "winsound", "wsgiref", "xml", "xmlrpc", "zipapp", "zipfile", "zipimport", "zlib", "zoneinfo"};

}  // namespace

core::Result<void> check_script_name(const std::filesystem::path& out) {
    if (out.extension() != ".py" && out.extension() != ".ipynb") {
        const auto u = out.filename().u8string();
        return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("python.bad_extension")), fmt::arg("name", std::string{u.begin(), u.end()})));
    }
    if (out.extension() != ".py") return {};
    const auto u = out.stem().u8string();
    const std::string stem{u.begin(), u.end()};
    if (std::ranges::find(kModules, stem) == kModules.end()) return {};
    return core::make_error(core::Errc::invalid_argument, fmt::format(fmt::runtime(tr("python.module_name")), fmt::arg("name", stem)));
}

}  // namespace signals::python
