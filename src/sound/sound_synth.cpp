#include "sound_synth.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace md {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Bruit pseudo-aléatoire reproductible (même son à chaque fois).
struct Noise {
    std::uint32_t state = 0x9E3779B9u;
    double next() {
        state = state * 1664525u + 1013904223u;
        return double(state >> 8) / double(1u << 24) * 2 - 1;   // [-1, 1)
    }
};

// Passe-bas à un pôle : adoucit le bruit (souffle, papier).
struct LowPass {
    double a, y = 0;
    LowPass(double cutoff, int rate) : a(1 - std::exp(-2 * kPi * cutoff / rate)) {}
    double operator()(double x) { return y += a * (x - y); }
};

// Signal [-1, 1] → PCM, au plus `level` de la pleine échelle, avec 3 ms de fondu à chaque bout (aucun déclic).
std::vector<std::int16_t> finish(std::vector<double> v, int rate, double level) {
    double peak = 1e-9;
    for (double x : v) peak = std::max(peak, std::abs(x));
    const std::size_t fade = std::size_t(rate * 0.003);
    std::vector<std::int16_t> out(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        double g = level / peak;
        if (i < fade) g *= double(i) / fade;
        if (v.size() - 1 - i < fade) g *= double(v.size() - 1 - i) / fade;
        out[i] = std::int16_t(std::lround(std::clamp(v[i] * g, -1.0, 1.0) * 32767));
    }
    return out;
}

// Déclic d'appareil photo : deux claquements secs (miroir, puis obturateur) sur un petit « toc » grave.
std::vector<double> shutter(int rate) {
    const std::size_t n = std::size_t(rate * 0.20);
    std::vector<double> v(n, 0.0);
    Noise noise;
    for (double start : {0.0, 0.075}) {
        LowPass lp(5200, rate);
        const double gain = start == 0.0 ? 1.0 : 0.75;
        for (std::size_t i = std::size_t(start * rate); i < n; ++i) {
            const double t = double(i) / rate - start;
            if (t > 0.05) break;
            const double click = lp(noise.next()) * std::exp(-t / 0.006);
            const double thunk = std::sin(2 * kPi * 150 * t) * std::exp(-t / 0.018) * 0.6;
            v[i] += gain * (click + thunk);
        }
    }
    return v;
}

// Froissement : grains de bruit aux amplitudes aléatoires, de plus en plus rares.
std::vector<double> crumple(int rate) {
    const std::size_t n = std::size_t(rate * 0.48);
    std::vector<double> v(n, 0.0);
    Noise noise, grains;
    LowPass lp(3800, rate), body(900, rate);
    double grain = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        if (i % std::size_t(rate * 0.004) == 0) grain = std::abs(grains.next()) < 0.55 ? std::abs(grains.next()) : 0.05;
        const double env = std::min(1.0, t / 0.02) * std::exp(-t / 0.22);
        const double x = noise.next();
        v[i] = (lp(x) * 0.7 + (x - body(x)) * 0.3) * grain * env;
    }
    return v;
}

// « Pop » du volume : une goutte qui descend de 950 à 550 Hz en 50 ms.
std::vector<double> pop(int rate) {
    const std::size_t n = std::size_t(rate * 0.06);
    std::vector<double> v(n);
    double phase = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        const double f = 550 + 400 * std::exp(-t / 0.012);
        phase += 2 * kPi * f / rate;
        v[i] = std::sin(phase) * std::min(1.0, t / 0.002) * std::exp(-t / 0.014);
    }
    return v;
}

// Souffle du nuage « poof » : bruit doux qui gonfle puis s'éteint.
std::vector<double> poof(int rate) {
    const std::size_t n = std::size_t(rate * 0.42);
    std::vector<double> v(n);
    Noise noise;
    LowPass lp(1400, rate), lp2(1400, rate);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = double(i) / rate;
        const double env = std::min(1.0, t / 0.035) * std::exp(-t / 0.11);
        v[i] = lp2(lp(noise.next())) * env;
    }
    return v;
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) out.push_back(std::uint8_t(v >> (8 * k)));
}
void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(std::uint8_t(v));
    out.push_back(std::uint8_t(v >> 8));
}

} // namespace

std::vector<std::int16_t> synthesizeSound(SystemSound s, int sampleRate) {
    const int rate = std::max(8000, sampleRate);
    switch (s) {
        case SystemSound::Screenshot: return finish(shutter(rate), rate, 0.42);
        case SystemSound::EmptyTrash: return finish(crumple(rate), rate, 0.38);
        case SystemSound::Volume: return finish(pop(rate), rate, 0.30);
        case SystemSound::Poof: return finish(poof(rate), rate, 0.32);
    }
    return {};
}

std::vector<std::uint8_t> wavBytes(const std::vector<std::int16_t>& pcm, int sampleRate) {
    const std::uint32_t data = std::uint32_t(pcm.size() * 2);
    std::vector<std::uint8_t> out;
    out.reserve(44 + data);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    put32(out, 36 + data);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(out, 16);                               // taille du bloc fmt
    put16(out, 1);                                // PCM
    put16(out, 1);                                // mono
    put32(out, std::uint32_t(sampleRate));
    put32(out, std::uint32_t(sampleRate) * 2);    // octets par seconde
    put16(out, 2);                                // octets par échantillon
    put16(out, 16);                               // bits
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    put32(out, data);
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(pcm.data());
    out.insert(out.end(), bytes, bytes + data);
    return out;
}

} // namespace md
