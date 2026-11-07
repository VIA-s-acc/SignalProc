#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/command.hpp"
#include "i18n/i18n.hpp"
#include "core/design/families.hpp"
#include "core/freq.hpp"
#include "core/metrics.hpp"
#include "core/process/shrink.hpp"
#include "core/wavedec.hpp"
#include "io/wav.hpp"

namespace signals::cli {
namespace {

constexpr double kResponseBudget = 1 << 26;

const std::vector<std::string> kExtensions{"zero",         "constant",      "symmetric",     "reflect",    "periodic",
                                           "smooth",       "periodization", "antisymmetric", "antireflect"};

int report_too_short(std::ostream& err, const core::Error& error, std::size_t n, std::size_t filter_len) {
    if (error.code != core::Errc::level_out_of_range) return report_error(err, error);
    const auto message = fmt::format(fmt::runtime(tr("error.signal_too_short")), fmt::arg("n", n),
                                     fmt::arg("length", filter_len), fmt::arg("max", core::dwt_max_level(n, filter_len)));
    err << fmt::format(fmt::runtime(tr("error.line")), fmt::arg("message", message)) << '\n';
    return 2;
}

nlohmann::json finite_or_text(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    return v;
}

nlohmann::json finite_or_text(const std::vector<double>& v) {
    nlohmann::json a = nlohmann::json::array();
    for (double x : v) a.push_back(finite_or_text(x));
    return a;
}

class DwtCommand final : public Command {
public:
    explicit DwtCommand(CLI::App& app) : sub_{app.add_subcommand("dwt", tr("cli.dwt.description"))} {
        sub_->add_option("input", input_, tr("cli.input"))->required();
        sub_->add_option("--wavelet", wavelet_, tr("cli.wavelet"))->required();
        sub_->add_option("--levels", levels_, tr("cli.levels"))->required()->check(CLI::Range(1, 64));
        sub_->add_option("--ext", ext_, tr("cli.ext"))->check(CLI::IsMember(kExtensions));
        sub_->add_option("--channel", channel_, tr("cli.channel"));
        sub_->add_option("-o,--output", output_, tr("cli.output"))->required();
        sub_->add_option("--bands-dir", bands_dir_, tr("cli.dwt.bands_dir"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const auto bank = core::wavelet(wavelet_);
        if (!bank) return report_error(err, bank.error());
        const auto file = io::read_wav(input_);
        if (!file) return report_error(err, file.error());
        const auto& audio = file->audio;
        if (channel_ && *channel_ >= audio.channels.size())
            return report_error(err, {core::Errc::invalid_argument, "channel " + std::to_string(*channel_) + " of " +
                                                                        std::to_string(audio.channels.size())});
        const core::DwtOptions options{*core::parse_extension(ext_), false};
        nlohmann::json channels = nlohmann::json::array();
        for (std::size_t c = 0; c < audio.channels.size(); ++c) {
            if (channel_ && c != *channel_) continue;
            auto coeffs = core::wavedec<double>(audio.channels[c], *bank, levels_, options);
            if (!coeffs) return report_too_short(err, coeffs.error(), audio.frames(), bank->length());
            nlohmann::json bands = nlohmann::json::array();
            for (std::size_t b = 0; b < coeffs->bands.size(); ++b) {
                const std::size_t j = b == 0 ? levels_ : levels_ + 1 - b;
                const auto name = fmt::format("{}{}", b == 0 ? 'a' : 'd', j);
                const double rate = audio.sample_rate / std::ldexp(1.0, static_cast<int>(j));
                bands.push_back({{"name", name}, {"rate", rate}, {"coeffs", coeffs->bands[b]}});
                if (!bands_dir_.empty())
                    if (auto r = write_band(c, name, j, rate, coeffs->bands[b]); !r) return report_error(err, r.error());
            }
            channels.push_back({{"index", c}, {"bands", bands}});
        }
        std::ofstream json_out{output_};
        json_out << nlohmann::json{{"wavelet", wavelet_}, {"levels", levels_}, {"ext", ext_},
                                   {"sample_rate", audio.sample_rate}, {"channels", channels}}
                        .dump()
                 << '\n';
        if (!json_out) return report_error(err, {core::Errc::io_error, "cannot write " + output_});
        out << fmt::format(fmt::runtime(tr("dwt.written")), fmt::arg("path", output_), fmt::arg("levels", levels_),
                           fmt::arg("wavelet", wavelet_))
            << '\n';
        return 0;
    }

private:
    core::Result<io::WavWriteReport> write_band(std::size_t channel, const std::string& name, std::size_t level, double rate,
                                                const std::vector<double>& c) const {
        std::error_code ec;
        std::filesystem::create_directories(bands_dir_, ec);
        const double gain = std::pow(2.0, -0.5 * static_cast<double>(level));
        std::vector<double> scaled(c);
        for (double& v : scaled) v *= gain;
        const core::AudioBuffer band{static_cast<double>(std::lround(rate)), {std::move(scaled)}};
        const auto path = std::filesystem::path{bands_dir_} / fmt::format("ch{}_{}.wav", channel, name);
        return io::write_wav(path, band, {io::SampleFormat::float32, false});
    }

    CLI::App* sub_;
    std::string input_, wavelet_, ext_ = "symmetric", output_, bands_dir_;
    std::size_t levels_ = 1;
    std::optional<std::size_t> channel_;
};

class DenoiseCommand final : public Command {
public:
    explicit DenoiseCommand(CLI::App& app) : sub_{app.add_subcommand("denoise", tr("cli.denoise.description"))} {
        sub_->add_option("input", input_, tr("cli.input"))->required();
        sub_->add_option("-o,--output", output_, tr("cli.output"))->required();
        sub_->add_option("--wavelet", options_.wavelet, tr("cli.wavelet"));
        sub_->add_option("--levels", levels_, tr("cli.denoise.levels"))->check(CLI::Range(1, 64));
        sub_->add_option("--rule", rule_, tr("cli.denoise.rule"))->check(CLI::IsMember({"bayes", "visu", "sure", "minimax"}));
        sub_->add_option("--mode", mode_, tr("cli.denoise.mode"))->check(CLI::IsMember({"soft", "hard", "garrote"}));
        sub_->add_option("--ext", ext_, tr("cli.ext"))->check(CLI::IsMember(kExtensions));
        sub_->add_option("--format", format_, tr("cli.format"))->check(CLI::IsMember({"s16", "s24", "s32", "f32"}));
        sub_->add_option("--reference", reference_, tr("cli.denoise.reference"));
        sub_->add_option("--dither", dither_, tr("cli.dither"))->check(CLI::IsMember({"none", "tpdf"}));
        sub_->add_flag("--json", json_, tr("cli.json.description"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const auto file = io::read_wav(input_);
        if (!file) return report_error(err, file.error());
        options_.levels = levels_;
        options_.mode = *core::parse_extension(ext_);
        options_.rule = core::parse_shrink_rule(rule_).value_or(core::ShrinkRule::bayes);
        options_.threshold = *core::parse_threshold_mode(mode_);
        const auto bank = core::wavelet(options_.wavelet);
        if (!bank) return report_error(err, bank.error());
        std::optional<io::WavFile> reference;
        if (!reference_.empty()) {
            auto r = io::read_wav(reference_);
            if (!r) return report_error(err, r.error());
            if (r->audio.channels.size() != file->audio.channels.size() || r->audio.frames() != file->audio.frames())
                return report_error(err, {core::Errc::length_mismatch, "reference has another shape than the input"});
            reference = std::move(*r);
        }
        auto result = core::denoise(file->audio, options_);
        if (!result) return report_too_short(err, result.error(), file->audio.frames(), bank->length());
        const auto format = format_ == "s16" ? io::SampleFormat::pcm_s16
                            : format_ == "s32" ? io::SampleFormat::pcm_s32
                            : format_ == "f32" ? io::SampleFormat::float32
                                               : io::SampleFormat::pcm_s24;
        io::WavWriteOptions write_options;
        write_options.format = format;
        write_options.dither = dither_ == "tpdf";
        const auto written = io::write_wav(output_, result->first, write_options);
        if (!written) return report_error(err, written.error());

        nlohmann::json channels = nlohmann::json::array();
        for (std::size_t c = 0; c < result->second.size(); ++c) {
            const auto& rep = result->second[c];
            nlohmann::json ch{{"sigma", rep.sigma}, {"levels", rep.levels}, {"thresholds", rep.thresholds}};
            if (reference) {
                ch["snr_in"] = finite_or_text(core::snr_db(reference->audio.channels[c], file->audio.channels[c]));
                ch["snr_out"] = finite_or_text(core::snr_db(reference->audio.channels[c], result->first.channels[c]));
            }
            channels.push_back(ch);
        }
        if (json_) {
            out << nlohmann::json{{"output", output_}, {"wavelet", options_.wavelet}, {"rule", rule_}, {"mode", mode_},
                                  {"clipped", written->clipped_samples}, {"channels", channels}}
                       .dump(2)
                << '\n';
            return 0;
        }
        for (std::size_t c = 0; c < channels.size(); ++c) {
            out << fmt::format(fmt::runtime(tr("denoise.channel")), fmt::arg("index", c),
                               fmt::arg("sigma", channels[c]["sigma"].get<double>()),
                               fmt::arg("levels", channels[c]["levels"].get<std::size_t>()))
                << '\n';
            if (reference && channels[c]["snr_in"].is_number() && channels[c]["snr_out"].is_number())
                out << fmt::format(fmt::runtime(tr("denoise.snr")), fmt::arg("before", channels[c]["snr_in"].get<double>()),
                                   fmt::arg("after", channels[c]["snr_out"].get<double>()))
                    << '\n';
        }
        out << fmt::format(fmt::runtime(tr("audio.written")), fmt::arg("path", output_),
                           fmt::arg("frames", result->first.frames()), fmt::arg("channels", result->first.channels.size()),
                           fmt::arg("rate", result->first.sample_rate), fmt::arg("format", io::to_string(format)))
            << '\n';
        if (written->clipped_samples > 0)
            out << fmt::format(fmt::runtime(tr("audio.clipped")), fmt::arg("count", written->clipped_samples)) << '\n';
        return 0;
    }

private:
    CLI::App* sub_;
    std::string input_, output_, rule_ = "bayes", mode_ = "soft", ext_ = "symmetric", format_ = "s24", reference_, dither_ = "tpdf";
    std::optional<std::size_t> levels_;
    core::DenoiseOptions options_;
    bool json_ = false;
};

class FilterResponseCommand final : public Command {
public:
    explicit FilterResponseCommand(CLI::App& filter)
        : sub_{filter.add_subcommand("response", tr("cli.filter.response.description"))} {
        sub_->add_option("name", name_, tr("cli.filter.name"))->required();
        sub_->add_option("--points", points_, tr("cli.filter.points"))->check(CLI::Range(2, 1 << 20));
        sub_->add_option("--filter", filter_, tr("cli.filter.which"))->check(CLI::IsMember({"h0", "h1", "f0", "f1"}));
        auto* band = sub_->add_option("--band", band_, tr("cli.filter.band"))->check(CLI::IsMember({"a", "d"}));
        sub_->add_option("--level", level_, tr("cli.filter.level"))->check(CLI::Range(1, 30))->needs(band);
        sub_->add_flag("--json", json_, tr("cli.json.description"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const auto bank = core::wavelet(name_);
        if (!bank) return report_error(err, bank.error());
        core::Laurent h;
        std::string label;
        double lo = 0, hi = 0.5;
        if (!band_.empty()) {
            const double taps = static_cast<double>(bank->length() - 1) * (std::ldexp(1.0, static_cast<int>(level_)) - 1) + 1;
            if (taps * static_cast<double>(points_) > kResponseBudget)
                return report_error(err, {core::Errc::invalid_argument,
                                          fmt::format("equivalent filter of {} taps at {} points exceeds {} evaluations; "
                                                      "lower --level or --points",
                                                      taps, points_, kResponseBudget)});
            auto eq = core::equivalent_filter(*bank, level_, band_ == "d");
            if (!eq) return report_error(err, eq.error());
            h = std::move(*eq);
            label = fmt::format("{}{}", band_, level_);
            hi = std::ldexp(1.0, -static_cast<int>(level_) + (band_ == "d" ? 1 : 0));
            lo = band_ == "d" ? hi / 2 : 0.0;
        } else {
            h = filter_ == "h1" ? bank->h1 : filter_ == "f0" ? bank->f0 : filter_ == "f1" ? bank->f1 : bank->h0;
            label = filter_;
            if (filter_ == "h1" || filter_ == "f1") lo = 0.5, hi = 1.0;
        }
        const auto r = core::frequency_response(h, points_);
        const auto m = lo == 0 ? core::band_metrics(r, hi / 2, std::min(1.0, 1.5 * hi)) : core::band_metrics(r, -1.0, 2.0);
        std::size_t peak = 0;
        for (std::size_t k = 0; k < r.magnitude.size(); ++k)
            if (r.magnitude[k] > r.magnitude[peak]) peak = k;
        const double peak_at = r.omega[peak] / std::numbers::pi;
        if (json_) {
            out << nlohmann::json{{"name", name_},
                                  {"filter", label},
                                  {"points", points_},
                                  {"nominal_band", {lo, hi}},
                                  {"peak_omega", peak_at},
                                  {"omega", r.omega},
                                  {"magnitude_db", r.magnitude_db},
                                  {"phase_unwrapped", r.phase_unwrapped},
                                  {"group_delay", finite_or_text(r.group_delay)},
                                  {"metrics",
                                   {{"cutoff_3db", finite_or_text(m.cutoff_3db)},
                                    {"passband_ripple_db", finite_or_text(m.passband_ripple_db)},
                                    {"stopband_attenuation_db", finite_or_text(m.stopband_attenuation_db)},
                                    {"transition_width", finite_or_text(m.transition_width)}}}}
                       .dump()
                << '\n';
            return 0;
        }
        const auto text = [](double v) { return std::isfinite(v) ? fmt::format("{:.4f}", v) : std::string{"—"}; };
        out << fmt::format(fmt::runtime(tr("response.header")), fmt::arg("name", name_), fmt::arg("filter", label),
                           fmt::arg("length", h.coeffs.size()), fmt::arg("points", points_), fmt::arg("lo", lo),
                           fmt::arg("hi", hi))
            << '\n';
        out << fmt::format(fmt::runtime(tr("response.metrics")), fmt::arg("peak", peak_at), fmt::arg("cutoff", text(m.cutoff_3db)),
                           fmt::arg("ripple", text(m.passband_ripple_db)), fmt::arg("stop", text(m.stopband_attenuation_db)),
                           fmt::arg("transition", text(m.transition_width)))
            << '\n';
        return 0;
    }

private:
    CLI::App* sub_;
    std::string name_, filter_ = "h0", band_;
    std::size_t points_ = 512, level_ = 1;
    bool json_ = false;
};

}  // namespace

void add_analysis_commands(CLI::App& app, CLI::App& filter, Commands& commands) {
    commands.push_back(std::make_unique<DwtCommand>(app));
    commands.push_back(std::make_unique<DenoiseCommand>(app));
    commands.push_back(std::make_unique<FilterResponseCommand>(filter));
}

}  // namespace signals::cli
