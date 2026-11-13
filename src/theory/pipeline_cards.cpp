#include "theory/pipeline_cards.hpp"

#include <map>

namespace signals::theory {

BlockTheory block_theory(std::string_view block) {
    static const std::map<std::string, std::string, std::less<>> cards{
        {"wav", "wav_in"}, {"idtcwt", "dtcwt"}, {"sst_band", "sst"}, {"sst_ridge", "sst"},         {"gen", "generator"},         {"samples", "generator"},  {"chirp", "generator"},   {"osc", "generator"},
        {"check", "error"},        {"assert", "error"},        {"resample", "upsample"},     {"lift53", "lifting"},     {"ilift53", "lifting"},   {"lift97", "lifting"},
        {"ilift97", "lifting"},    {"mra", "dwt"},               {"iir", "iir_design"},     {"fir", "fir_design"},    {"moving_average", "fir_design"},
        {"preemphasis", "fir_design"}, {"deemphasis", "iir_design"}, {"add", "filter_bank"}, {"multiply", "gain"},   {"invert", "gain"},
        {"pad", "delay"},          {"concat", "cut"},            {"trim_silence", "cut"},   {"merge", "split"},       {"swap", "split"},
        {"duplicate", "split"},    {"pan", "mixdown"},           {"add_noise", "snr"},      {"dc_offset", "remove_dc"}, {"stats", "band_energy"},
        {"loudness", "band_energy"}, {"pitch_shift", "time_stretch"}, {"speed", "time_stretch"},
        {"abs", "arithmetic"}, {"affine", "arithmetic"}, {"am", "modulation"}, {"balance", "mixdown"}, {"band_power", "spectral_features"}, {"binary", "arithmetic"}, {"biquad", "recursive"}, {"channel_delay", "delay"}, {"clip", "nonlinear"}, {"coeff_quantize", "coeff_compression"}, {"coeff_stats", "statistics"}, {"comb", "recursive"}, {"compressor", "dynamics"}, {"cumsum", "difference"}, {"dc_block", "recursive"}, {"dct", "cepstrum"}, {"deconvolve", "restoration"}, {"delay_estimate", "correlation"}, {"denoise", "threshold"}, {"diff", "difference"}, {"echo", "reverb"}, {"envelope", "analytic"}, {"export_report", "export"}, {"fade", "envelope_window"}, {"fir_taps", "fir_design"}, {"firls", "fir_design"}, {"fm", "modulation"}, {"gate", "dynamics"}, {"gausspulse", "generator"}, {"hilbert", "analytic"}, {"histogram", "statistics"}, {"idct", "cepstrum"}, {"impulse_train", "generator"}, {"inst_freq", "analytic"}, {"ipolyphase", "polyphase"}, {"iwpt", "packets"}, {"keep_largest", "coeff_compression"}, {"lfilter", "recursive"}, {"limiter", "dynamics"}, {"lpc_synth", "lpc"}, {"math", "arithmetic"}, {"median", "smoothing"}, {"mfcc", "spectral_features"}, {"mix", "arithmetic"}, {"modulated_delay", "modulation"}, {"mulaw", "quantize"}, {"onsets", "spectral_features"}, {"phaser", "modulation"}, {"remez", "fir_design"}, {"repeat", "hold_repeat"}, {"sample_hold", "hold_repeat"}, {"savgol", "smoothing"}, {"select_channel", "split"}, {"shift", "delay"}, {"softclip", "nonlinear"}, {"spectral_subtract", "restoration"}, {"spectrum_peaks", "spectral_features"}, {"square", "arithmetic"}, {"stereo_width", "mid_side"}, {"thd", "spectral_features"}, {"tremolo", "modulation"}, {"wavefold", "nonlinear"}, {"wiener", "restoration"}, {"window", "envelope_window"}, {"wpt", "packets"}, {"xcorr", "correlation"}};
    if (block == "swt" || block == "iswt") return {true, "swt-modwt"};
    const auto it = cards.find(block);
    return {false, it == cards.end() ? std::string{block} : it->second};
}

}  // namespace signals::theory
