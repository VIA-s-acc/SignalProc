#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include "commands/command.hpp"
#include "commands/i18n.hpp"
#include "core/multirate.hpp"
#include "io/generators.hpp"
#include "io/stats.hpp"
#include "io/wav.hpp"

namespace signals::cli {
namespace {

using io::SampleFormat;

const std::map<std::string, SampleFormat> kWriteFormats{
    {"s16", SampleFormat::pcm_s16}, {"s24", SampleFormat::pcm_s24}, {"s32", SampleFormat::pcm_s32}, {"f32", SampleFormat::float32}};

nlohmann::json db_json(double v) { return std::isinf(v) && v < 0 ? nlohmann::json("-inf") : nlohmann::json(v); }

int report_written(std::ostream& out, const std::string& path, const core::AudioBuffer& audio, SampleFormat format,
                   const io::WavWriteReport& report) {
    out << fmt::format(fmt::runtime(tr("audio.written")), fmt::arg("path", path), fmt::arg("frames", audio.frames()),
                       fmt::arg("channels", audio.channels.size()), fmt::arg("rate", audio.sample_rate),
                       fmt::arg("format", io::to_string(format)))
        << '\n';
    if (report.clipped_samples > 0)
        out << fmt::format(fmt::runtime(tr("audio.clipped")), fmt::arg("count", report.clipped_samples)) << '\n';
    return 0;
}

class InfoCommand final : public Command {
public:
    explicit InfoCommand(CLI::App& app) : sub_{app.add_subcommand("info", tr("cli.info.description"))} {
        sub_->add_option("input", input_, tr("cli.input"))->required();
        sub_->add_flag("--json", json_, tr("cli.json.description"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const auto file = io::read_wav(input_);
        if (!file) return report_error(err, file.error());
        const auto& info = file->info;
        const double duration = static_cast<double>(info.frames) / info.sample_rate;
        const auto stats = io::channel_stats(*file);
        if (json_) {
            nlohmann::json channels = nlohmann::json::array();
            for (const auto& s : stats)
                channels.push_back({{"peak_dbfs", db_json(s.peak_dbfs)}, {"rms_dbfs", db_json(s.rms_dbfs)}, {"dc", s.dc},
                                    {"crest_factor_db", s.crest_factor_db}, {"clipped", s.clipped}});
            out << nlohmann::json{{"format", io::to_string(info.format)}, {"channels", info.channels},
                                  {"sample_rate", info.sample_rate}, {"frames", info.frames}, {"duration", duration},
                                  {"extensible", info.extensible}, {"channels_stats", channels}}
                       .dump(2)
                << '\n';
            return 0;
        }
        out << fmt::format(fmt::runtime(tr("audio.info.header")), fmt::arg("path", input_),
                           fmt::arg("format", io::to_string(info.format)), fmt::arg("channels", info.channels),
                           fmt::arg("rate", info.sample_rate), fmt::arg("frames", info.frames), fmt::arg("duration", duration))
            << '\n';
        for (std::size_t c = 0; c < stats.size(); ++c)
            out << fmt::format(fmt::runtime(tr("audio.info.channel")), fmt::arg("index", c), fmt::arg("peak", stats[c].peak_dbfs),
                               fmt::arg("rms", stats[c].rms_dbfs), fmt::arg("dc", stats[c].dc),
                               fmt::arg("crest", stats[c].crest_factor_db), fmt::arg("clipped", stats[c].clipped))
                << '\n';
        return 0;
    }

private:
    CLI::App* sub_;
    std::string input_;
    bool json_ = false;
};

class GenCommand final : public Command {
public:
    explicit GenCommand(CLI::App& app) : sub_{app.add_subcommand("gen", tr("cli.gen.description"))} {
        sub_->add_option("kind", kind_, tr("cli.gen.kind"))
            ->required()
            ->check(CLI::IsMember({"sine", "sweep", "impulse", "step", "noise", "harmonics", "am", "fm", "dj"}));
        sub_->add_option("--rate", rate_, tr("cli.gen.rate"))->check(CLI::Range(1, 768'000));
        sub_->add_option("--duration", duration_, tr("cli.gen.duration"))->check(CLI::Range(1e-6, 3600.0));
        sub_->add_option("--channels", channels_, tr("cli.gen.channels"))->check(CLI::Range(1, 64));
        sub_->add_option("--format", format_, tr("cli.format"))->check(CLI::IsMember({"s16", "s24", "s32", "f32"}));
        sub_->add_option("-o,--output", output_, tr("cli.output"))->required();
        sub_->add_flag("--dither", dither_, tr("cli.gen.dither"));
        sub_->add_option("--amp", amp_, tr("cli.gen.amp"));
        sub_->add_option("--freq", freq_, tr("cli.gen.freq"));
        sub_->add_option("--f0", f0_, tr("cli.gen.f0"));
        sub_->add_option("--f1", f1_, tr("cli.gen.f1"));
        sub_->add_flag("--log", log_, tr("cli.gen.log"));
        sub_->add_option("--color", color_, tr("cli.gen.color"))->check(CLI::IsMember({"white", "pink", "brown"}));
        sub_->add_option("--rms-db", rms_db_, tr("cli.gen.rms_db"));
        sub_->add_option("--amps", amps_, tr("cli.gen.amps"))->delimiter(',');
        sub_->add_option("--carrier", carrier_, tr("cli.gen.carrier"));
        sub_->add_option("--modulator", modulator_, tr("cli.gen.modulator"));
        sub_->add_option("--depth", depth_, tr("cli.gen.depth"));
        sub_->add_option("--index", index_, tr("cli.gen.index"));
        sub_->add_option("--signal", signal_, tr("cli.gen.signal"))
            ->check(CLI::IsMember({"blocks", "bumps", "heavisine", "doppler"}));
        sub_->add_option("--seed", seed_, tr("cli.gen.seed"));
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        const auto n = static_cast<std::size_t>(std::llround(static_cast<double>(rate_) * duration_));
        core::AudioBuffer audio{static_cast<double>(rate_), {}};
        for (unsigned c = 0; c < channels_; ++c) audio.channels.push_back(channel(n, c));
        const auto format = kWriteFormats.at(format_);
        const auto report = io::write_wav(output_, audio, {format, dither_});
        if (!report) return report_error(err, report.error());
        return report_written(out, output_, audio, format, *report);
    }

private:
    std::vector<double> channel(std::size_t n, unsigned index) const {
        const double fs = static_cast<double>(rate_);
        if (kind_ == "noise") {
            const auto color = color_ == "pink" ? io::NoiseColor::pink : color_ == "brown" ? io::NoiseColor::brown : io::NoiseColor::white;
            return io::noise(color, std::pow(10.0, rms_db_ / 20), n, seed_ + index);
        }
        std::vector<double> x;
        if (kind_ == "sine") x = io::sine(freq_, 1.0, 0.0, fs, n);
        else if (kind_ == "sweep") x = io::sweep(f0_, f1_, log_ ? io::SweepKind::logarithmic : io::SweepKind::linear, fs, n);
        else if (kind_ == "impulse") x = io::impulse(n, 0);
        else if (kind_ == "step") x = io::step(n, 0);
        else if (kind_ == "harmonics") x = io::harmonics(freq_, amps_, fs, n);
        else if (kind_ == "am") x = io::am(carrier_, modulator_, depth_, fs, n);
        else if (kind_ == "fm") x = io::fm(carrier_, modulator_, index_, fs, n);
        else x = peak_normalized(io::donoho_johnstone(dj_kind(), n));
        for (double& v : x) v *= amp_;
        return x;
    }

    io::DonohoJohnstone dj_kind() const {
        if (signal_ == "blocks") return io::DonohoJohnstone::blocks;
        if (signal_ == "bumps") return io::DonohoJohnstone::bumps;
        if (signal_ == "heavisine") return io::DonohoJohnstone::heavisine;
        return io::DonohoJohnstone::doppler;
    }

    static std::vector<double> peak_normalized(std::vector<double> x) {
        double peak = 0;
        for (double v : x) peak = std::max(peak, std::abs(v));
        if (peak > 0)
            for (double& v : x) v /= peak;
        return x;
    }

    CLI::App* sub_;
    std::string kind_, format_ = "s24", output_, color_ = "white", signal_ = "doppler";
    unsigned rate_ = 48'000, channels_ = 1;
    double duration_ = 1.0, amp_ = 0.5, freq_ = 1000, f0_ = 20, f1_ = 20'000, rms_db_ = -20;
    double carrier_ = 1000, modulator_ = 5, depth_ = 0.5, index_ = 2;
    std::vector<double> amps_{1.0, 0.5, 0.25};
    std::uint64_t seed_ = 1;
    bool log_ = false, dither_ = false;
};

SampleFormat writable(SampleFormat f) {
    switch (f) {
    case SampleFormat::pcm_s16:
    case SampleFormat::pcm_s24:
    case SampleFormat::pcm_s32:
    case SampleFormat::float32: return f;
    case SampleFormat::float64: return SampleFormat::float32;
    default: return SampleFormat::pcm_s16;
    }
}

class ResampleCommand final : public Command {
public:
    explicit ResampleCommand(CLI::App& app) : sub_{app.add_subcommand("resample", tr("cli.resample.description"))} {
        sub_->add_option("input", input_, tr("cli.input"))->required();
        auto* down = sub_->add_option("--down", down_, tr("cli.resample.down"))->check(CLI::Range(1, 1024));
        auto* up = sub_->add_option("--up", up_, tr("cli.resample.up"))->check(CLI::Range(1, 1024));
        down->excludes(up);
        sub_->add_flag("--no-filter", no_filter_, tr("cli.resample.no_filter"));
        sub_->add_option("-o,--output", output_, tr("cli.output"))->required();
    }
    bool parsed() const override { return sub_->parsed(); }

    int run(std::ostream& out, std::ostream& err) override {
        if ((down_ == 0) == (up_ == 0))
            return report_error(err, {core::Errc::invalid_argument, "give exactly one of --down M or --up M"});
        const auto file = io::read_wav(input_);
        if (!file) return report_error(err, file.error());
        core::MultirateOptions options;
        options.anti_alias = !no_filter_;
        const double rate = down_ ? file->audio.sample_rate / down_ : file->audio.sample_rate * up_;
        core::AudioBuffer audio{static_cast<double>(std::lround(rate)), {}};
        for (const auto& channel : file->audio.channels) {
            auto y = down_ ? core::decimate(channel, down_, options) : core::interpolate(channel, up_, options);
            if (!y) return report_error(err, y.error());
            audio.channels.push_back(std::move(*y));
        }
        const auto format = writable(file->info.format);
        const auto report = io::write_wav(output_, audio, {format});
        if (!report) return report_error(err, report.error());
        return report_written(out, output_, audio, format, *report);
    }

private:
    CLI::App* sub_;
    std::string input_, output_;
    unsigned down_ = 0, up_ = 0;
    bool no_filter_ = false;
};

}  // namespace

void add_audio_commands(CLI::App& app, Commands& commands) {
    commands.push_back(std::make_unique<InfoCommand>(app));
    commands.push_back(std::make_unique<GenCommand>(app));
    commands.push_back(std::make_unique<ResampleCommand>(app));
}

}  // namespace signals::cli
