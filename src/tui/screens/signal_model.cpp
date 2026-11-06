#include "tui/screens/signal_model.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <limits>
#include <new>

#include <fmt/format.h>

#include "core/design/families.hpp"
#include "core/metrics.hpp"
#include "core/spectral.hpp"
#include "core/wavedec.hpp"
#include "i18n/i18n.hpp"
#include "tui/monitor/sampler.hpp"
#include "tui/shell/errors.hpp"
#include "tui/widgets/format.hpp"

namespace signals::tui {
namespace {

constexpr std::size_t kWelchSegment = 4096;
constexpr std::size_t kMinViewSamples = 32;
constexpr std::size_t kBandLevels = 6;
constexpr double kMinAmplitude = 1.0 / 1024;

std::string wall_clock() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &tm);
    return buf;
}

std::size_t estimated_load_bytes(const io::WavInfo& info) {
    const auto per_frame = static_cast<std::size_t>(info.channels) * (sizeof(double) + sizeof(float) + 1) + 2 * sizeof(double);
    return static_cast<std::size_t>(info.frames) * per_frame;
}

double to_db(double amplitude) { return amplitude > 0 ? 20 * std::log10(amplitude) : -std::numeric_limits<double>::infinity(); }

}  // namespace

SignalModel::SignalModel(std::function<std::string()> clock, std::size_t memory_budget)
    : clock_{clock ? std::move(clock) : wall_clock}, memory_budget_{memory_budget ? memory_budget : static_cast<std::size_t>(physical_memory_bytes() / 2)} {}

void SignalModel::open(const std::filesystem::path& path, JobRunner& jobs) {
    if (const auto previous = job()) jobs.cancel(*previous);
    const auto name = path.filename().string();
    auto id = jobs.submit(fmt::format(fmt::runtime(tr("tui.signal.job")), fmt::arg("name", name)),
                          {tr("tui.signal.stage.read"), tr("tui.signal.stage.stats"), tr("tui.signal.stage.pyramid"),
                           tr("tui.signal.stage.welch"), tr("tui.signal.stage.dwt")},
                          [this, path, name](JobContext& ctx) {
                              try {
                              ctx.begin(0);
                              if (const auto probe = io::probe_wav(path); probe && memory_budget_ > 0) {
                                  const auto need = estimated_load_bytes(*probe);
                                  if (need > memory_budget_) {
                                      const auto text = fmt::format(fmt::runtime(tr("tui.signal.too_large")),
                                                                    fmt::arg("need", format_si(static_cast<double>(need), tr("unit.byte"))),
                                                                    fmt::arg("budget", format_si(static_cast<double>(memory_budget_), tr("unit.byte"))));
                                      ctx.fail(text);
                                      add_log(text, LogLine::error);
                                      return;
                                  }
                              }
                              auto file = io::read_wav(path, &ctx);
                              if (!file) {
                                  if (file.error().code == core::Errc::cancelled) return;
                                  const auto text = error_text(file.error());
                                  ctx.fail(text);
                                  add_log(text, LogLine::error);
                                  return;
                              }
                              auto d = std::make_shared<SignalData>();
                              d->path = path;
                              d->info = file->info;
                              ctx.finish(file->info.frames * file->info.channels * sizeof(double));
                              if (ctx.cancelled()) return;

                              ctx.begin(1);
                              d->stats = io::channel_stats(*file);
                              for (const auto& s : d->stats) d->full_scale_samples += s.clipped;
                              ctx.finish();
                              if (ctx.cancelled()) return;

                              ctx.begin(2);
                              const double fs = file->audio.sample_rate;
                              const auto channels = static_cast<double>(file->audio.channels.size());
                              std::vector<double> mono(file->audio.frames(), 0.0);
                              std::size_t sample_bytes = 0, pyramid_bytes = 0;
                              for (auto& ch : file->audio.channels) {
                                  d->samples.emplace_back(ch.begin(), ch.end());
                                  sample_bytes += ch.size() * sizeof(float);
                                  for (std::size_t k = 0; k < mono.size(); ++k) mono[k] += ch[k];
                                  std::vector<double>{}.swap(ch);
                              }
                              for (double& v : mono) v /= channels;
                              for (const auto& ch : d->samples) {
                                  d->pyramids.emplace_back(ch);
                                  pyramid_bytes += d->pyramids.back().bytes();
                              }
                              d->memory = core::TrackedBytes{core::MemoryCategory::signals, sample_bytes};
                              d->display_memory = core::TrackedBytes{core::MemoryCategory::display, pyramid_bytes};
                              ctx.finish(sample_bytes + pyramid_bytes);
                              if (ctx.cancelled()) return;

                              ctx.begin(3);
                              auto psd = core::welch_psd(mono, fs, kWelchSegment, kWelchSegment / 2, &ctx);
                              if (psd) {
                                  const double peak = *std::max_element(psd->begin(), psd->end());
                                  const auto freqs = core::welch_frequencies(fs, kWelchSegment);
                                  for (std::size_t k = 1; k < psd->size(); ++k) {
                                      d->psd_hz.push_back(freqs[k]);
                                      d->psd_db.push_back(peak > 0 ? std::max(-100.0, 10 * std::log10((*psd)[k] / peak)) : -100.0);
                                  }
                                  ctx.finish(psd->size() * sizeof(double));
                              } else if (psd.error().code == core::Errc::cancelled) {
                                  return;
                              } else {
                                  ctx.finish(0, tr("tui.signal.too_short"));
                              }

                              ctx.begin(4);
                              const auto bank = core::wavelet("db2");
                              const auto levels = std::min(kBandLevels, core::dwt_max_level(mono.size(), bank->length()));
                              if (levels > 0) {
                                  auto coeffs = core::wavedec<double>(mono, *bank, levels, {});
                                  if (coeffs) {
                                      d->band_energy = core::band_energy_fractions(*coeffs);
                                      for (const auto& b : coeffs->bands) d->band_lengths.push_back(b.size());
                                  }
                                  ctx.finish();
                              } else {
                                  ctx.finish(0, tr("tui.signal.too_short"));
                              }
                              if (ctx.cancelled()) return;
                              add_log(fmt::format(fmt::runtime(tr("tui.signal.log.opened")), fmt::arg("name", name)), LogLine::info);
                              if (d->full_scale_samples > 0)
                                  add_log(fmt::format(fmt::runtime(tr("tui.signal.log.clipped")),
                                                      fmt::arg("count", format_number(static_cast<double>(d->full_scale_samples)))),
                                          LogLine::warn);
                              publish(std::move(d));
                              } catch (const std::bad_alloc&) {
                                  const auto text = tr("tui.jobs.out_of_memory");
                                  ctx.fail(text);
                                  add_log(text, LogLine::error);
                              } catch (const std::exception& e) {
                                  const auto text = fmt::format(fmt::runtime(tr("tui.jobs.internal_error")), fmt::arg("what", e.what()));
                                  ctx.fail(text);
                                  add_log(text, LogLine::error);
                              }
                          });
    std::lock_guard lock{mutex_};
    job_ = id;
}

std::shared_ptr<const SignalData> SignalModel::data() const {
    std::lock_guard lock{mutex_};
    return data_;
}

std::optional<audio::Clip> SignalModel::clip() const {
    auto d = data();
    if (!d || d->samples.empty()) return std::nullopt;
    audio::Clip c;
    for (const auto& ch : d->samples) c.channels.emplace_back(ch);
    c.rate = d->info.sample_rate;
    c.owner = d;
    c.tag = "signal";
    return c;
}

std::vector<LogLine> SignalModel::log() const {
    std::lock_guard lock{mutex_};
    return log_;
}

std::optional<std::uint64_t> SignalModel::job() const {
    std::lock_guard lock{mutex_};
    return job_;
}

void SignalModel::publish(std::shared_ptr<const SignalData> d) {
    std::lock_guard lock{mutex_};
    data_ = std::move(d);
    selection_stats_.clear();
}

void SignalModel::add_log(std::string text, LogLine::Level level) {
    std::lock_guard lock{mutex_};
    log_.push_back({clock_(), std::move(text), level});
}

double SignalModel::sync_view() const {
    auto d = data();
    if (d != view_data_) {
        view_data_ = d;
        begin_ = 0;
        end_ = d ? d->duration() : 0;
        amplitude_ = 1;
        cursor_a_.reset();
        cursor_b_.reset();
        selection_.reset();
    }
    return d ? d->duration() : 0.0;
}

double SignalModel::clamp_time(double t) const { return std::clamp(t, 0.0, sync_view()); }

double SignalModel::view_begin() const {
    sync_view();
    return begin_;
}

double SignalModel::view_end() const {
    sync_view();
    return end_;
}

double SignalModel::amplitude() const {
    sync_view();
    return amplitude_;
}

std::optional<double> SignalModel::cursor_a() const {
    sync_view();
    return cursor_a_;
}

std::optional<double> SignalModel::cursor_b() const {
    sync_view();
    return cursor_b_;
}

std::optional<std::pair<double, double>> SignalModel::selection() const {
    sync_view();
    return selection_;
}

void SignalModel::zoom(double factor, double anchor) {
    const double total = sync_view();
    if (total <= 0 || !(factor > 0)) return;
    const double min_span = std::min(total, static_cast<double>(kMinViewSamples) / view_data_->info.sample_rate);
    const double old_span = end_ - begin_;
    const double span = std::clamp(old_span / factor, min_span, total);
    anchor = std::clamp(anchor, begin_, end_);
    double b = anchor - (anchor - begin_) * (span / old_span);
    b = std::clamp(b, 0.0, total - span);
    begin_ = b;
    end_ = b + span;
}

void SignalModel::zoom_amplitude(double factor) {
    sync_view();
    if (!(factor > 0)) return;
    amplitude_ = std::clamp(amplitude_ / factor, kMinAmplitude, 1.0);
}

void SignalModel::pan(double dt) {
    const double total = sync_view();
    const double span = end_ - begin_;
    begin_ = std::clamp(begin_ + dt, 0.0, std::max(0.0, total - span));
    end_ = begin_ + span;
}

void SignalModel::set_cursor_a(double t) {
    if (sync_view() > 0) cursor_a_ = clamp_time(t);
}

void SignalModel::set_cursor_b(double t) {
    if (sync_view() > 0) cursor_b_ = clamp_time(t);
}

void SignalModel::select(double t0, double t1) {
    if (sync_view() <= 0) return;
    t0 = clamp_time(t0);
    t1 = clamp_time(t1);
    selection_ = std::pair{std::min(t0, t1), std::max(t0, t1)};
}

void SignalModel::clear_selection() {
    sync_view();
    selection_.reset();
    std::lock_guard lock{mutex_};
    selection_stats_.clear();
}

void SignalModel::compute_selection(JobRunner& jobs) {
    auto d = data();
    if (!d || !selection()) return;
    const auto [t0, t1] = *selection_;
    jobs.submit(tr("tui.signal.selection_job"), {tr("tui.signal.selection_stage")}, [this, d, t0, t1](JobContext& ctx) {
        ctx.begin(0);
        const double fs = d->info.sample_rate;
        const auto a = static_cast<std::size_t>(t0 * fs), b = std::min(d->samples.front().size(), static_cast<std::size_t>(t1 * fs));
        std::vector<SelectionStats> out;
        for (const auto& ch : d->samples) {
            SelectionStats s;
            s.duration = t1 - t0;
            s.rms_dbfs = s.peak_dbfs = s.dc = s.crest_db = std::numeric_limits<double>::quiet_NaN();
            if (b > a) {
                std::vector<double> part(ch.begin() + static_cast<std::ptrdiff_t>(a), ch.begin() + static_cast<std::ptrdiff_t>(b));
                double peak = 0;
                for (double v : part) peak = std::max(peak, std::abs(v));
                const double rms = std::sqrt(core::energy(part) / static_cast<double>(part.size()));
                s.peak_dbfs = to_db(peak);
                s.rms_dbfs = to_db(rms);
                s.dc = core::neumaier_sum(part) / static_cast<double>(part.size());
                s.crest_db = peak > 0 ? s.peak_dbfs - s.rms_dbfs : 0;
            }
            out.push_back(s);
        }
        {
            std::lock_guard lock{mutex_};
            selection_stats_ = std::move(out);
        }
        ctx.finish();
    });
}

std::vector<SelectionStats> SignalModel::selection_stats() const {
    std::lock_guard lock{mutex_};
    return selection_stats_;
}

}  // namespace signals::tui
