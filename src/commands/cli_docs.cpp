#include "commands/cli_docs.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <utility>

#include <CLI/CLI.hpp>
#include <fmt/format.h>

#include "commands/cli_tree.hpp"
#include "i18n/i18n.hpp"

namespace signals::cli {
namespace {

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

std::vector<std::string> choices_of(const std::string& type_name) {
    const auto open = type_name.find('{');
    const auto close = open == std::string::npos ? open : type_name.find('}', open);
    if (close == std::string::npos) return {};
    std::vector<std::string> out;
    std::string item;
    for (char c : type_name.substr(open + 1, close - open - 1)) {
        if (c != ',') {
            item += c;
            continue;
        }
        if (auto t = trim(item); !t.empty()) out.push_back(std::move(t));
        item.clear();
    }
    if (auto t = trim(item); !t.empty()) out.push_back(std::move(t));
    return out;
}

CliOption describe(const CLI::Option& o) {
    CliOption out;
    if (o.get_positional()) {
        out.names = {o.get_name(true)};
    } else {
        for (const auto& s : o.get_snames()) out.names.push_back("-" + s);
        for (const auto& l : o.get_lnames()) out.names.push_back("--" + l);
    }
    const auto type = o.get_type_name();
    if (o.get_type_size_max() > 0) out.value = type.substr(0, type.find(':'));
    out.description = o.get_description();
    out.choices = choices_of(type);
    return out;
}

void walk(const CLI::App& app, const std::vector<std::string>& path, std::vector<CliEntry>& out) {
    CliEntry entry{.path = path, .description = app.get_description(), .options = {}, .positionals = {}};
    for (const CLI::Option* o : app.get_options()) {
        if (o == app.get_help_ptr() || o == app.get_help_all_ptr()) continue;
        (o->get_positional() ? entry.positionals : entry.options).push_back(describe(*o));
    }
    out.push_back(std::move(entry));
    for (const CLI::App* sub : app.get_subcommands([](const CLI::App*) { return true; })) {
        auto next = path;
        next.push_back(sub->get_name());
        walk(*sub, next, out);
    }
}

std::string key_of(const std::vector<std::string>& path) {
    std::string key = "signals";
    for (const auto& word : path) {
        key += '_';
        for (char c : word) key += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    return key;
}

std::string first_line(const std::string& text) { return text.substr(0, text.find('\n')); }

std::string join(const std::vector<std::string>& words, std::string_view sep = " ") {
    std::string out;
    for (const auto& w : words) out += (out.empty() ? "" : std::string{sep}) + w;
    return out;
}

std::string sh_quote(const std::string& text) {
    std::string out = "'";
    for (char c : first_line(text)) out += c == '\'' ? std::string{"'\\''"} : std::string(1, c);
    return out + "'";
}

std::string fish_quote(const std::string& text) {
    std::string out = "'";
    for (char c : first_line(text)) {
        if (c == '\'' || c == '\\') out += '\\';
        out += c;
    }
    return out + "'";
}

std::vector<std::string> option_names(const CliEntry& e) {
    std::vector<std::string> out;
    for (const auto& o : e.options) out.insert(out.end(), o.names.begin(), o.names.end());
    return out;
}

std::vector<const CliEntry*> children(std::span<const CliEntry> entries, const CliEntry& parent) {
    std::vector<const CliEntry*> out;
    for (const auto& e : entries)
        if (e.path.size() == parent.path.size() + 1 && std::equal(parent.path.begin(), parent.path.end(), e.path.begin())) out.push_back(&e);
    return out;
}

std::vector<std::string> positional_choices(const CliEntry& e) { return e.positionals.empty() ? std::vector<std::string>{} : e.positionals.front().choices; }

std::string walker_cases(std::span<const CliEntry> entries, std::string_view indent, std::string_view assign) {
    std::string out;
    for (const auto& e : entries) {
        if (e.path.empty()) continue;
        const auto parent = std::vector<std::string>{e.path.begin(), e.path.end() - 1};
        out += fmt::format("{}{}:{}) {}{} ;;\n", indent, key_of(parent), e.path.back(), assign, key_of(e.path));
    }
    return out;
}

std::string bash_script(std::span<const CliEntry> entries) {
    std::string values, lists;
    for (const auto& e : entries) {
        for (const auto& o : e.options) {
            if (o.value.empty()) continue;
            std::vector<std::string> patterns;
            for (const auto& n : o.names) patterns.push_back(key_of(e.path) + ":" + n);
            const auto reply = o.choices.empty() ? std::string{"compgen -f -- \"$cur\""} : fmt::format("compgen -W \"{}\" -- \"$cur\"", join(o.choices));
            values += fmt::format("        {}) COMPREPLY=($({})); return ;;\n", join(patterns, "|"), reply);
        }
        std::vector<std::string> subs;
        for (const auto* c : children(entries, e)) subs.push_back(c->path.back());
        lists += fmt::format("        {}) opts=\"{}\"; subs=\"{}\"; args=\"{}\" ;;\n", key_of(e.path), join(option_names(e)), join(subs),
                             join(positional_choices(e)));
    }
    return fmt::format(R"(# bash completion for signals
_signals() {{
    local cur prev cmd i w opts subs args
    cur="${{COMP_WORDS[COMP_CWORD]}}"
    prev="${{COMP_WORDS[COMP_CWORD-1]}}"
    cmd=signals
    for ((i = 1; i < COMP_CWORD; i++)); do
        w="${{COMP_WORDS[i]}}"
        case "$cmd:$w" in
{}        esac
    done
    case "$cmd:$prev" in
{}    esac
    opts=""; subs=""; args=""
    case "$cmd" in
{}    esac
    if [[ "$cur" == -* ]]; then
        COMPREPLY=($(compgen -W "$opts" -- "$cur"))
    elif [[ -n "$subs" ]]; then
        COMPREPLY=($(compgen -W "$subs" -- "$cur"))
    elif [[ -n "$args" ]]; then
        COMPREPLY=($(compgen -W "$args" -- "$cur"))
    else
        compopt -o filenames 2>/dev/null
        COMPREPLY=($(compgen -f -- "$cur"))
    fi
}}
complete -F _signals signals
)",
                       walker_cases(entries, "            ", "cmd="), values, lists);
}

std::string zsh_script(std::span<const CliEntry> entries) {
    std::string values, lists;
    for (const auto& e : entries) {
        std::string opts, subs;
        for (const auto& o : e.options) {
            for (const auto& n : o.names) opts += " " + sh_quote(n + ":" + o.description);
            if (o.value.empty()) continue;
            std::vector<std::string> patterns;
            for (const auto& n : o.names) patterns.push_back(key_of(e.path) + ":" + n);
            const auto reply = o.choices.empty() ? std::string{"_files"} : "compadd -- " + join(o.choices);
            values += fmt::format("        {}) {}; return ;;\n", join(patterns, "|"), reply);
        }
        for (const auto* c : children(entries, e)) subs += " " + sh_quote(c->path.back() + ":" + c->description);
        lists += fmt::format("        {})\n            opts=({})\n            subs=({})\n            args=({})\n            ;;\n", key_of(e.path), opts, subs,
                             join(positional_choices(e)));
    }
    return fmt::format(R"(#compdef signals

_signals() {{
    local cmd=signals i w cur prev
    local -a opts subs args
    for ((i = 2; i < CURRENT; i++)); do
        w=${{words[i]}}
        case "$cmd:$w" in
{}        esac
    done
    cur=${{words[CURRENT]}}
    prev=${{words[CURRENT-1]}}
    case "$cmd:$prev" in
{}    esac
    case "$cmd" in
{}    esac
    if [[ $cur == -* ]]; then
        _describe -t options option opts
    elif (( ${{#subs}} )); then
        _describe -t commands command subs
    elif (( ${{#args}} )); then
        compadd -- "${{args[@]}}"
    else
        _files
    fi
}}

if [[ ${{zsh_eval_context[-1]}} == loadautofunc ]]; then
    _signals "$@"
else
    compdef _signals signals
fi
)",
                       walker_cases(entries, "            ", "cmd="), values, lists);
}

std::string fish_script(std::span<const CliEntry> entries) {
    std::string cases, lines;
    for (const auto& e : entries) {
        if (!e.path.empty()) {
            const auto parent = std::vector<std::string>{e.path.begin(), e.path.end() - 1};
            cases += fmt::format("            case '{}:{}'\n                set cmd {}\n", key_of(parent), e.path.back(), key_of(e.path));
        }
        const auto when = fmt::format("-n 'test (__signals_cmd) = {}'", key_of(e.path));
        for (const auto* c : children(entries, e)) lines += fmt::format("complete -c signals {} -a {} -d {}\n", when, c->path.back(), fish_quote(c->description));
        for (const auto& o : e.options) {
            std::string names;
            for (const auto& n : o.names) names += n.starts_with("--") ? " -l " + n.substr(2) : " -s " + n.substr(1);
            std::string value;
            if (!o.value.empty()) value = o.choices.empty() ? " -r -F" : fmt::format(" -x -a '{}'", join(o.choices));
            lines += fmt::format("complete -c signals {}{}{} -d {}\n", when, names, value, fish_quote(o.description));
        }
        if (children(entries, e).empty() && !e.positionals.empty()) {
            const auto choices = positional_choices(e);
            lines += choices.empty() ? fmt::format("complete -c signals {} -F\n", when) : fmt::format("complete -c signals {} -x -a '{}'\n", when, join(choices));
        }
    }
    return fmt::format(R"(# fish completion for signals
function __signals_cmd
    set -l tokens (commandline -opc)
    set -e tokens[1]
    set -l cmd signals
    for w in $tokens
        switch "$cmd:$w"
{}        end
    end
    echo $cmd
end

complete -c signals -f
{})",
                       cases, lines);
}

std::string roff(const std::string& text) {
    std::string out;
    bool line_start = true;
    for (char c : text) {
        if (line_start && (c == '.' || c == '\'')) out += "\\&";
        line_start = c == '\n';
        if (c == '\\') out += "\\e";
        else if (c == '-') out += "\\-";
        else out += c;
    }
    return out;
}

std::string roff_value(const CliOption& o) {
    if (o.value.empty()) return {};
    if (o.choices.empty()) return " \\fI" + roff(o.value) + "\\fR";
    std::string out = " ";
    for (std::size_t i = 0; i < o.choices.size(); ++i) out += (i ? "|" : "") + std::string{"\\fI"} + roff(o.choices[i]) + "\\fR";
    return out;
}

void roff_options(std::string& out, const std::vector<CliOption>& options) {
    for (const auto& o : options) {
        std::string names;
        for (const auto& n : o.names) names += (names.empty() ? "" : ", ") + std::string{"\\fB"} + roff(n) + "\\fR";
        out += ".TP\n" + names + roff_value(o) + "\n" + roff(o.description) + "\n";
    }
}

void roff_items(std::string& out, std::initializer_list<std::pair<std::string_view, std::string>> items, std::string_view font) {
    for (const auto& [name, text] : items) out += fmt::format(".TP\n.{} {}\n{}\n", font, roff(std::string{name}), roff(text));
}

std::string man_text(std::span<const CliEntry> entries, std::string_view version) {
    const std::chrono::year_month_day today{std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now())};
    const auto date = fmt::format("{:04}-{:02}-{:02}", static_cast<int>(today.year()), static_cast<unsigned>(today.month()), static_cast<unsigned>(today.day()));
    std::string out = fmt::format(".TH SIGNALS 1 {} \"signals {}\" \"Signals\"\n", date, roff(std::string{version}));
    const CliEntry* root = nullptr;
    for (const auto& e : entries)
        if (e.path.empty()) root = &e;
    out += ".SH NAME\nsignals \\- " + roff(tr("cli.man.name")) + "\n";
    out += fmt::format(".SH SYNOPSIS\n.B signals\n[\\fB\\-\\-lang\\fR \\fIru\\fR|\\fIen\\fR] \\fI{}\\fR [\\fI{}\\fR]\n", roff(tr("cli.man.word.command")),
                       roff(tr("cli.man.word.options")));
    out += ".SH DESCRIPTION\n" + roff(tr("cli.man.description")) + "\n";
    out += ".SH OPTIONS\n";
    if (root) roff_options(out, root->options);
    out += ".SH COMMANDS\n";
    for (const auto& e : entries) {
        if (e.path.empty()) continue;
        std::string title = "signals";
        for (const auto& w : e.path) title += " " + w;
        out += ".SS \"" + roff(title) + "\"\n" + roff(e.description) + "\n";
        if (!e.positionals.empty()) {
            std::string usage = ".PP\n\\fB" + roff(title) + "\\fR";
            for (const auto& p : e.positionals) usage += " \\fI" + roff(p.names.front()) + "\\fR";
            out += usage + "\n";
        }
        roff_options(out, e.options);
    }
    out += ".SH ENVIRONMENT\n";
    roff_items(out,
               {{"SIGNALS_LANG", tr("cli.man.env.signals_lang")},
                {"XDG_CONFIG_HOME, APPDATA, HOME", tr("cli.man.env.config")},
                {"VISUAL, EDITOR", tr("cli.man.env.editor")},
                {"NO_COLOR", tr("cli.man.env.no_color")},
                {"TERM, COLORTERM, TERM_PROGRAM, WT_SESSION", tr("cli.man.env.term")},
                {"LC_ALL, LC_CTYPE, LANG", tr("cli.man.env.locale")}},
               "B");
    out += ".SH FILES\n";
    roff_items(out,
               {{"~/.config/signals/terminal.toml", tr("cli.man.files.terminal")},
                {"~/.config/signals/session.toml", tr("cli.man.files.session")},
                {"~/.config/signals/learn.toml", tr("cli.man.files.learn")},
                {"~/.config/signals/filters/", tr("cli.man.files.filters")}},
               "I");
    out += ".SH \"EXIT STATUS\"\n";
    roff_items(out, {{"0", tr("cli.man.exit.0")}, {"1", tr("cli.man.exit.1")}, {"2", tr("cli.man.exit.2")}, {"3", tr("cli.man.exit.3")}, {"130", tr("cli.man.exit.130")}}, "B");
    out += ".SH \"SEE ALSO\"\nhttps://github.com/VIA\\-s\\-acc/SignalProc\n";
    return out;
}

}  // namespace

std::vector<CliEntry> cli_entries(const CLI::App& root) {
    std::vector<CliEntry> out;
    walk(root, {}, out);
    return out;
}

std::vector<CliEntry> cli_entries() {
    std::vector<CliEntry> out;
    with_cli_tree([&out](const CLI::App& root) { out = cli_entries(root); });
    return out;
}

std::string completion_script(std::span<const CliEntry> entries, ShellKind shell) {
    switch (shell) {
        case ShellKind::bash: return bash_script(entries);
        case ShellKind::zsh: return zsh_script(entries);
        case ShellKind::fish: return fish_script(entries);
    }
    return {};
}

std::string man_page(std::span<const CliEntry> entries, std::string_view version) { return man_text(entries, version); }

}  // namespace signals::cli
