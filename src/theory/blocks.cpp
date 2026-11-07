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
};

}  // namespace

std::span<const BlockSpec> block_catalog() { return kCatalog; }

const BlockSpec* find_block(std::string_view id) {
    for (const auto& b : kCatalog)
        if (b.id == id) return &b;
    return nullptr;
}

}  // namespace signals::theory
