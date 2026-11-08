#include "theory/blocks.hpp"

#include <array>

namespace signals::theory {
namespace {

using V = std::string_view;

constexpr std::array<V, 15> kHalfband{"block.p", "block.degree", "block.splits", "block.wavelet_splits", "block.split",
                                      "block.zeros_h0", "block.zeros_f0", "block.h0_length", "block.f0_length", "block.delay",
                                      "check.pr_alias", "check.pr_distortion", "check.orthogonality", "metric.sobolev_h0", "metric.sobolev_f0"};
constexpr std::array<V, 7> kFilterBank{"block.bank", "block.length", "block.delay", "block.linear_phase", "check.pr_alias", "check.pr_distortion",
                                       "check.orthogonality"};
constexpr std::array<V, 9> kFreqResponse{"block.filter", "block.length", "block.points", "block.zeros_at_minus_one", "block.linear_phase",
                                         "metric.cutoff_3db", "metric.ripple_db", "metric.attenuation_db", "metric.transition"};
constexpr std::array<V, 7> kEquivalent{"block.bank", "block.band", "block.level", "block.length", "metric.cutoff_3db", "check.pr_distortion",
                                       "check.pr_alias"};
constexpr std::array<V, 6> kToeplitz{"block.filter", "block.n", "block.rows", "block.cols", "block.band_width", "check.circulant_fft"};
constexpr std::array<V, 9> kBankMatrix{"block.bank", "block.n", "block.mode", "block.order", "block.rows", "block.cols", "metric.cond",
                                       "check.ata_identity", "check.sa_identity"};
constexpr std::array<V, 15> kDwt{"block.n", "block.wavelet", "block.levels", "block.mode", "block.filter_length", "block.max_level",
                                 "block.band_lengths", "block.band_sum", "block.band_ranges", "block.linear_phase", "metric.multiplies",
                                 "check.round_trip", "check.orthogonality", "check.vanishing_moments", "check.parseval"};
constexpr std::array<V, 5> kIdwt{"block.wavelet", "block.levels", "block.mode", "block.n", "check.round_trip"};
constexpr std::array<V, 7> kThreshold{"block.rule", "block.mode", "block.levels", "block.thresholds", "metric.sigma", "metric.kept",
                                      "metric.energy_removed"};
constexpr std::array<V, 4> kBandEdit{"block.enabled", "block.gains", "metric.energy_kept", "block.aliasing"};
constexpr std::array<V, 3> kBandEnergy{"block.bands", "metric.energies", "check.parseval"};
constexpr std::array<V, 4> kError{"metric.max_abs", "metric.rms", "metric.relative", "check.round_trip"};
constexpr std::array<V, 2> kSnr{"metric.snr_db", "metric.psnr_db"};
constexpr std::array<V, 4> kConvolve{"block.method", "block.filter_length", "block.n", "metric.multiplies"};
constexpr std::array<V, 4> kDownsample{"block.m", "block.taps", "block.attenuation_db", "block.anti_alias"};
constexpr std::array<V, 2> kUpsample{"block.m", "block.taps"};
constexpr std::array<V, 9> kWavIn{"block.path", "block.format", "block.channels", "block.rate", "block.frames", "block.duration",
                                  "metric.peak_dbfs", "metric.clipped", "metric.load_time"};
constexpr std::array<V, 3> kWavOut{"block.format", "block.dither", "metric.clipped"};
constexpr std::array<V, 3> kGenerator{"block.kind", "block.rate", "block.duration"};
constexpr std::array<V, 1> kGain{"block.gain_db"};
constexpr std::array<V, 2> kNormalize{"block.target_dbfs", "block.kind"};
constexpr std::array<V, 1> kRemoveDc{"metric.dc"};
constexpr std::array<V, 1> kChannels{"block.channels"};
constexpr std::array<V, 1> kDelay{"block.samples"};
constexpr std::array<V, 0> kNone{};
constexpr std::array<V, 2> kCut{"block.begin", "block.end"};
constexpr std::array<V, 3> kListen{"block.clip", "block.rate", "metric.position"};
constexpr std::array<V, 5> kIirDesign{"block.filter", "block.order", "block.cutoff", "metric.poles", "check.stable"};
constexpr std::array<V, 5> kFirDesign{"block.filter", "block.length", "block.cutoff", "metric.ripple_db", "metric.attenuation_db"};
constexpr std::array<V, 4> kPhiPsi{"block.filter", "block.factors", "metric.zeros", "metric.partition"};
constexpr std::array<V, 2> kLifting{"block.levels", "block.integer"};
constexpr std::array<V, 10> kTimeStretch{"block.mode",     "block.rate",   "block.semitones", "block.method", "block.n_fft",
                                        "block.hop",      "metric.duration", "metric.f0_in",  "metric.f0_out", "metric.cents"};
constexpr std::array<V, 2> kPitch{"metric.f0_in", "metric.f0_out"};
constexpr std::array<V, 7> kCard_quantize{"block.bits", "block.bytes", "block.direction", "block.dither", "block.mu", "block.seed", "block.wall"};
constexpr std::array<V, 7> kCard_coeff_compression{"block.amount", "block.bits", "block.bytes", "block.keep_approx", "block.mode", "block.step", "block.wall"};
constexpr std::array<V, 5> kCard_nonlinear{"block.bytes", "block.drive", "block.limit", "block.shape", "block.wall"};
constexpr std::array<V, 3> kCard_analytic{"block.bytes", "block.output", "block.wall"};
constexpr std::array<V, 2> kCard_cepstrum{"block.bytes", "block.wall"};
constexpr std::array<V, 4> kCard_correlation{"block.bytes", "block.mode", "block.normalize", "block.wall"};
constexpr std::array<V, 13> kCard_spectral_features{"block.bands", "block.bytes", "block.count", "block.frame", "block.harmonics", "block.hop", "block.n", "block.n_mels", "block.n_mfcc", "block.prominence_db", "block.rolloff_pct", "block.segment", "block.wall"};
constexpr std::array<V, 5> kCard_statistics{"block.bins", "block.bytes", "block.range", "block.tol", "block.wall"};
constexpr std::array<V, 4> kCard_export{"block.bytes", "block.format", "block.path", "block.wall"};
constexpr std::array<V, 8> kCard_envelope_window{"block.alpha", "block.beta", "block.bytes", "block.curve", "block.fade_in", "block.fade_out", "block.kind", "block.wall"};
constexpr std::array<V, 2> kCard_difference{"block.bytes", "block.wall"};
constexpr std::array<V, 7> kCard_smoothing{"block.bytes", "block.deriv", "block.kernel", "block.mode", "block.polyorder", "block.wall", "block.window"};
constexpr std::array<V, 12> kCard_recursive{"block.a", "block.b", "block.bytes", "block.delay", "block.f0", "block.gain", "block.gain_db", "block.kind", "block.q", "block.r", "block.type", "block.wall"};
constexpr std::array<V, 5> kCard_lms{"block.bytes", "block.mu", "block.rule", "block.taps", "block.wall"};
constexpr std::array<V, 9> kCard_restoration{"block.alpha", "block.beta", "block.bytes", "block.n", "block.noise", "block.noise_s", "block.size", "block.snr_db", "block.wall"};
constexpr std::array<V, 15> kCard_modulation{"block.base_ms", "block.bytes", "block.carrier_hz", "block.depth", "block.depth_ms", "block.deviation_hz", "block.feedback", "block.max_hz", "block.min_hz", "block.mix", "block.mode", "block.rate_hz", "block.shape", "block.stages", "block.wall"};
constexpr std::array<V, 9> kCard_reverb{"block.bytes", "block.damping", "block.delay_ms", "block.dry", "block.feedback", "block.mix", "block.room_size", "block.wall", "block.wet"};
constexpr std::array<V, 11> kCard_dynamics{"block.attack_ms", "block.bytes", "block.detector", "block.knee_db", "block.makeup_db", "block.range_db", "block.ratio", "block.release_ms", "block.rms_ms", "block.threshold_db", "block.wall"};
constexpr std::array<V, 11> kCard_arithmetic{"block.at", "block.bytes", "block.crossfade_ms", "block.gain_a_db", "block.gain_b_db", "block.mode", "block.offset", "block.op", "block.p", "block.scale", "block.wall"};
constexpr std::array<V, 2> kCard_polyphase{"block.bytes", "block.wall"};
constexpr std::array<V, 6> kCard_packets{"block.basis", "block.bytes", "block.ext", "block.levels", "block.wall", "block.wavelet"};
constexpr std::array<V, 3> kCard_lpc{"block.bytes", "block.order", "block.wall"};
constexpr std::array<V, 4> kCard_hold_repeat{"block.bytes", "block.hold", "block.times", "block.wall"};
constexpr std::array<V, 7> kStft{"block.n", "block.hop", "block.overlap", "block.rate", "metric.dt", "metric.df", "metric.frames"};

constexpr std::array kCatalog{
    BlockSpec{"halfband", "Half-band P(z) and its splits", "fb-maxflat", Tier::core, kHalfband},
    BlockSpec{"filter_bank", "Two-channel filter bank", "two-channel", Tier::core, kFilterBank},
    BlockSpec{"freq_response", "Frequency response", "freq-magnitude", Tier::core, kFreqResponse},
    BlockSpec{"equivalent_filters", "Equivalent band filters", "freq-equivalent-filters", Tier::core, kEquivalent},
    BlockSpec{"toeplitz", "Convolution as a Toeplitz matrix", "toeplitz-convolution", Tier::core, kToeplitz},
    BlockSpec{"bank_matrix", "Analysis and synthesis matrices", "fb-matrices", Tier::core, kBankMatrix},
    BlockSpec{"dwt", "DWT, Mallat pyramid", "dwt-mallat", Tier::core, kDwt},
    BlockSpec{"idwt", "Inverse DWT", "dwt-mallat", Tier::core, kIdwt},
    BlockSpec{"threshold", "Wavelet thresholding", "audio-denoising", Tier::core, kThreshold},
    BlockSpec{"band_edit", "Band zeroing and gains", "audio-subband-zeroing", Tier::core, kBandEdit},
    BlockSpec{"band_energy", "Band energy", "met-parseval", Tier::core, kBandEnergy},
    BlockSpec{"error", "Reconstruction error", "met-reconstruction", Tier::core, kError},
    BlockSpec{"snr", "SNR and PSNR", "met-snr", Tier::core, kSnr},
    BlockSpec{"convolve", "Convolution", "lti-convolution", Tier::core, kConvolve},
    BlockSpec{"downsample", "Downsampling by M", "multirate-down-up", Tier::core, kDownsample},
    BlockSpec{"upsample", "Upsampling by M", "multirate-down-up", Tier::core, kUpsample},
    BlockSpec{"wav_in", "WAV input", "num-formats", Tier::core, kWavIn},
    BlockSpec{"wav_out", "WAV output", "num-formats", Tier::core, kWavOut},
    BlockSpec{"generator", "Signal generator", "conv-signals", Tier::core, kGenerator},
    BlockSpec{"gain", "Gain", "conv-signals", Tier::core, kGain},
    BlockSpec{"normalize", "Normalisation", "conv-signals", Tier::core, kNormalize},
    BlockSpec{"remove_dc", "DC removal", "conv-signals", Tier::core, kRemoveDc},
    BlockSpec{"split", "Channel split", "conv-signals", Tier::core, kChannels},
    BlockSpec{"mixdown", "Mixdown to mono", "conv-signals", Tier::core, kChannels},
    BlockSpec{"mid_side", "Mid/side", "conv-signals", Tier::core, kChannels},
    BlockSpec{"delay", "Delay", "conv-signals", Tier::core, kDelay},
    BlockSpec{"reverse", "Time reversal", "conv-signals", Tier::core, kNone},
    BlockSpec{"cut", "Cut a fragment", "conv-signals", Tier::core, kCut},
    BlockSpec{"listen", "Listening", "num-formats", Tier::core, kListen},
    BlockSpec{"stft", "STFT spectrogram", "stft", Tier::v1, kStft},
    BlockSpec{"iir_design", "IIR design", "cls-iir-prototypes", Tier::v1, kIirDesign},
    BlockSpec{"fir_design", "FIR design", "cls-parks-mcclellan", Tier::v1, kFirDesign},
    BlockSpec{"phi_psi", "Spectra of phi and psi", "freq-phi-psi-spectra", Tier::v1, kPhiPsi},
    BlockSpec{"lifting", "Lifting 5/3 and 9/7", "lift-53", Tier::v1, kLifting},
    BlockSpec{"time_stretch", "Time stretch and pitch shift", "multirate-time-stretch", Tier::v2, kTimeStretch},
    BlockSpec{"pitch", "Fundamental frequency: YIN", "pitch-yin", Tier::v2, kPitch},
    BlockSpec{"quantize", "Quantisation and mu-law", "met-snr", Tier::v1, kCard_quantize},
    BlockSpec{"coeff_compression", "Coefficient compression", "audio-compression", Tier::v1, kCard_coeff_compression},
    BlockSpec{"nonlinear", "Memoryless nonlinearities", "conv-signals", Tier::v1, kCard_nonlinear},
    BlockSpec{"analytic", "Analytic signal and envelope", "lti-z-dtft", Tier::v1, kCard_analytic},
    BlockSpec{"cepstrum", "DCT and cepstrum", "toeplitz-plus-hankel", Tier::v1, kCard_cepstrum},
    BlockSpec{"correlation", "Correlation and delay", "lti-convolution", Tier::v1, kCard_correlation},
    BlockSpec{"spectral_features", "Spectral features", "audio-onsets", Tier::v1, kCard_spectral_features},
    BlockSpec{"statistics", "Histogram and coefficient statistics", "met-other", Tier::v1, kCard_statistics},
    BlockSpec{"export", "Export to CSV and JSON", "num-formats", Tier::v1, kCard_export},
    BlockSpec{"envelope_window", "Windows and fades", "cls-window-method", Tier::v1, kCard_envelope_window},
    BlockSpec{"difference", "Difference and running sum", "lti-z-dtft", Tier::v1, kCard_difference},
    BlockSpec{"smoothing", "Median and Savitzky-Golay smoothing", "cls-least-squares", Tier::v1, kCard_smoothing},
    BlockSpec{"recursive", "Recursive filters", "cls-biquads", Tier::v1, kCard_recursive},
    BlockSpec{"lms", "LMS and NLMS", "toeplitz-levinson", Tier::v1, kCard_lms},
    BlockSpec{"restoration", "Wiener filtering, spectral subtraction, deconvolution", "audio-denoising", Tier::v1, kCard_restoration},
    BlockSpec{"modulation", "Modulation effects", "cls-allpass-notch", Tier::v1, kCard_modulation},
    BlockSpec{"reverb", "Echo and reverberation", "cls-allpass-notch", Tier::v1, kCard_reverb},
    BlockSpec{"dynamics", "Dynamics: compressor, limiter, gate", "audio-band-energy", Tier::v1, kCard_dynamics},
    BlockSpec{"arithmetic", "Arithmetic on signals", "conv-signals", Tier::v1, kCard_arithmetic},
    BlockSpec{"polyphase", "Polyphase split and merge", "multirate-polyphase", Tier::v1, kCard_polyphase},
    BlockSpec{"packets", "Wavelet packets and the best basis", "wavelet-packets", Tier::v1, kCard_packets},
    BlockSpec{"lpc", "Linear prediction", "toeplitz-levinson", Tier::v1, kCard_lpc},
    BlockSpec{"hold_repeat", "Sample and hold, repetition", "multirate-down-up", Tier::v1, kCard_hold_repeat},
};

}  // namespace

std::span<const BlockSpec> block_catalog() { return kCatalog; }

const BlockSpec* find_block(std::string_view id) {
    for (const auto& b : kCatalog)
        if (b.id == id) return &b;
    return nullptr;
}

}  // namespace signals::theory
