#pragma once
// Disco Sauce Split't - DSP engine (no JUCE dependency, fully offline, no AI models)
#include <vector>
#include <cmath>
#include <algorithm>
#include <functional>
#include <utility>

namespace ds
{
constexpr double kPi = 3.14159265358979323846;
using ProgressFn = std::function<void (float)>;
using StopFn     = std::function<bool()>;

//==============================================================================
struct FFT
{
    explicit FFT (int n)
        : N (n), logN (0), cosT ((size_t) (n / 2)), sinT ((size_t) (n / 2)), rev ((size_t) n)
    {
        while ((1 << logN) < n) ++logN;
        for (int i = 0; i < N / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (2.0 * kPi * i / N);
            sinT[(size_t) i] = (float) -std::sin (2.0 * kPi * i / N);
        }
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < logN; ++b)
                if (i & (1 << b)) r |= 1 << (logN - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    void run (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= N; len <<= 1)
        {
            const int half = len >> 1, step = N / len;
            for (int i = 0; i < N; i += len)
                for (int k = 0; k < half; ++k)
                {
                    const float wr = cosT[(size_t) (k * step)];
                    const float wi = inverse ? -sinT[(size_t) (k * step)] : sinT[(size_t) (k * step)];
                    const int a = i + k, b = a + half;
                    const float tr = re[b] * wr - im[b] * wi;
                    const float ti = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - tr; im[b] = im[a] - ti;
                    re[a] += tr;        im[a] += ti;
                }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) N;
            for (int i = 0; i < N; ++i) { re[i] *= s; im[i] *= s; }
        }
    }

    int N, logN;
    std::vector<float> cosT, sinT;
    std::vector<int> rev;
};

//==============================================================================
struct Stems { std::vector<float> mel[2], drm[2], bas[2]; };

inline float medianOf (float* w, int n)
{
    std::nth_element (w, w + n / 2, w + n);
    return w[n / 2];
}

/*  Harmonic / Percussive separation (median filtering, soft masks) + bass split.
    melody + drums + bass == original signal exactly.  Processed in 20 s segments with
    cross-fades so long tracks do not need much memory.                              */
inline bool separate (const std::vector<float>* in, double sr, Stems& out,
                      const ProgressFn& prog, const StopFn& stop)
{
    const int N = 2048, HOP = 1024, K = N / 2 + 1;
    const int total = (int) in[0].size();
    FFT fft (N);

    std::vector<float> win ((size_t) N), bw ((size_t) K);
    for (int i = 0; i < N; ++i) win[(size_t) i] = (float) std::sin (kPi * (i + 0.5) / N);
    for (int k = 0; k < K; ++k)
    {
        const double f = k * sr / N;
        bw[(size_t) k] = (float) std::min (1.0, std::max (0.0, (200.0 - f) / 90.0));   // 110 Hz -> 200 Hz crossover
    }

    for (int c = 0; c < 2; ++c)
    {
        out.mel[c].assign ((size_t) total, 0.f);
        out.drm[c].assign ((size_t) total, 0.f);
        out.bas[c].assign ((size_t) total, 0.f);
    }

    const int segCore = (int) (20.0 * sr), ov = (int) (1.5 * sr), xf = 2048;
    const int nseg = std::max (1, (total + segCore - 1) / segCore);

    std::vector<float> mag, magT, Ht, Pm, Mh;
    std::vector<float> re ((size_t) N), im ((size_t) N), reM ((size_t) N), imM ((size_t) N), reB ((size_t) N), imB ((size_t) N);
    std::vector<float> segMel[2], segBas[2];
    float w[40];

    for (int seg = 0; seg < nseg; ++seg)
    {
        if (stop()) return false;
        const int c0 = seg * segCore, c1 = std::min (total, c0 + segCore);
        const int p0 = std::max (0, c0 - ov), p1 = std::min (total, c1 + ov), L = p1 - p0;
        const int T = (L - 1) / HOP + 2;

        // ---- pass 1: magnitude spectrogram of the mono mix
        mag.assign ((size_t) T * K, 0.f);
        for (int f = 0; f < T; ++f)
        {
            for (int i = 0; i < N; ++i)
            {
                const int idx = (f - 1) * HOP + i;
                const float v = (idx >= 0 && idx < L) ? 0.5f * (in[0][(size_t) (p0 + idx)] + in[1][(size_t) (p0 + idx)]) : 0.f;
                re[(size_t) i] = v * win[(size_t) i];
                im[(size_t) i] = 0.f;
            }
            fft.run (re.data(), im.data(), false);
            for (int k = 0; k < K; ++k)
                mag[(size_t) f * K + k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
        }

        // ---- median filters: harmonic = along time, percussive = along frequency
        magT.assign ((size_t) K * T, 0.f);
        for (int f = 0; f < T; ++f)
            for (int k = 0; k < K; ++k)
                magT[(size_t) k * T + f] = mag[(size_t) f * K + k];

        Ht.assign ((size_t) K * T, 0.f);
        for (int k = 0; k < K; ++k)
        {
            if ((k & 63) == 0 && stop()) return false;
            for (int t = 0; t < T; ++t)
            {
                int n = 0;
                for (int j = -6; j <= 6; ++j)
                    w[n++] = magT[(size_t) k * T + (size_t) std::min (T - 1, std::max (0, t + j))];
                Ht[(size_t) k * T + t] = medianOf (w, n);
            }
        }

        Pm.assign ((size_t) T * K, 0.f);
        for (int t = 0; t < T; ++t)
        {
            if ((t & 63) == 0 && stop()) return false;
            for (int k = 0; k < K; ++k)
            {
                int n = 0;
                for (int j = -8; j <= 8; ++j)
                    w[n++] = mag[(size_t) t * K + (size_t) std::min (K - 1, std::max (0, k + j))];
                Pm[(size_t) t * K + k] = medianOf (w, n);
            }
        }

        Mh.assign ((size_t) T * K, 0.f);
        for (int t = 0; t < T; ++t)
            for (int k = 0; k < K; ++k)
            {
                const float h = Ht[(size_t) k * T + t], p = Pm[(size_t) t * K + k];
                const float h2 = h * h, p2 = p * p;
                Mh[(size_t) t * K + k] = h2 / (h2 + p2 + 1e-12f);
            }
        prog (((float) seg + 0.35f) / (float) nseg);

        // ---- pass 2: apply masks to each channel
        for (int c = 0; c < 2; ++c)
        {
            segMel[c].assign ((size_t) L, 0.f);
            segBas[c].assign ((size_t) L, 0.f);
            const float* x = in[c].data() + p0;

            for (int f = 0; f < T; ++f)
            {
                for (int i = 0; i < N; ++i)
                {
                    const int idx = (f - 1) * HOP + i;
                    re[(size_t) i] = ((idx >= 0 && idx < L) ? x[idx] : 0.f) * win[(size_t) i];
                    im[(size_t) i] = 0.f;
                }
                fft.run (re.data(), im.data(), false);

                for (int k = 0; k < K; ++k)
                {
                    const float m = Mh[(size_t) f * K + k], b = bw[(size_t) k];
                    const float wm = m * (1.f - b), wb = m * b;
                    reM[(size_t) k] = re[(size_t) k] * wm; imM[(size_t) k] = im[(size_t) k] * wm;
                    reB[(size_t) k] = re[(size_t) k] * wb; imB[(size_t) k] = im[(size_t) k] * wb;
                }
                imM[0] = imB[0] = 0.f; imM[(size_t) (N / 2)] = imB[(size_t) (N / 2)] = 0.f;
                for (int k = 1; k < N / 2; ++k)
                {
                    reM[(size_t) (N - k)] =  reM[(size_t) k]; imM[(size_t) (N - k)] = -imM[(size_t) k];
                    reB[(size_t) (N - k)] =  reB[(size_t) k]; imB[(size_t) (N - k)] = -imB[(size_t) k];
                }
                fft.run (reM.data(), imM.data(), true);
                fft.run (reB.data(), imB.data(), true);

                for (int i = 0; i < N; ++i)
                {
                    const int idx = (f - 1) * HOP + i;
                    if (idx >= 0 && idx < L)
                    {
                        segMel[c][(size_t) idx] += reM[(size_t) i] * win[(size_t) i];
                        segBas[c][(size_t) idx] += reB[(size_t) i] * win[(size_t) i];
                    }
                }
            }
            prog (((float) seg + 0.35f + 0.325f * (float) (c + 1)) / (float) nseg);
        }

        // ---- cross-fade the segment into the output
        for (int n = std::max (0, c0 - xf); n < std::min (total, c1 + xf); ++n)
        {
            float wt = 1.f;
            if (seg > 0 && n < c0 + xf)
                wt *= std::min (1.f, std::max (0.f, (float) (n - (c0 - xf)) / (2.f * (float) xf)));
            if (seg < nseg - 1 && n >= c1 - xf)
                wt *= std::min (1.f, std::max (0.f, (float) ((c1 + xf) - n) / (2.f * (float) xf)));
            if (wt <= 0.f) continue;
            const int li = n - p0;
            for (int c = 0; c < 2; ++c)
            {
                const float mel = segMel[c][(size_t) li], bas = segBas[c][(size_t) li];
                const float drm = in[c][(size_t) n] - mel - bas;
                out.mel[c][(size_t) n] += wt * mel;
                out.bas[c][(size_t) n] += wt * bas;
                out.drm[c][(size_t) n] += wt * drm;
            }
        }
    }
    return true;
}

//==============================================================================
inline std::vector<float> monoOf (const std::vector<float>* c)
{
    std::vector<float> m (c[0].size());
    for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (c[0][i] + c[1][i]);
    return m;
}

inline std::vector<float> peaksOf (const std::vector<float>* c, int cols)
{
    std::vector<float> p ((size_t) cols, 0.f);
    const size_t n = c[0].size();
    if (n == 0) return p;
    float mx = 1e-9f;
    for (int i = 0; i < cols; ++i)
    {
        const size_t a = n * (size_t) i / (size_t) cols, b = std::min (n, n * (size_t) (i + 1) / (size_t) cols);
        float m = 0.f;
        for (size_t j = a; j < b; ++j) m = std::max (m, std::max (std::abs (c[0][j]), std::abs (c[1][j])));
        p[(size_t) i] = m; mx = std::max (mx, m);
    }
    for (auto& v : p) v = std::pow (v / mx, 0.7f);
    return p;
}

//==============================================================================
//  Onset-strength envelope (log-compressed spectral flux), unit variance, zero mean
inline std::vector<float> onsetEnvelope (const std::vector<float>& x, double sr, int hop, const StopFn& stop)
{
    const int N = 1024;
    std::vector<float> env;
    if ((int) x.size() < N * 8) return env;
    FFT fft (N);
    std::vector<float> w ((size_t) N), re ((size_t) N), im ((size_t) N), prev ((size_t) (N / 2 + 1), 0.f);
    for (int i = 0; i < N; ++i) w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));
    const int frames = ((int) x.size() - N) / hop + 1;
    env.assign ((size_t) frames, 0.f);
    const int kLo = std::max (1, (int) (30.0 * N / sr)), kHi = std::min (N / 2 - 1, (int) (9000.0 * N / sr));

    for (int f = 0; f < frames; ++f)
    {
        if ((f & 255) == 0 && stop()) return {};
        const float* p = x.data() + (size_t) f * (size_t) hop;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = p[i] * w[(size_t) i]; im[(size_t) i] = 0.f; }
        fft.run (re.data(), im.data(), false);
        float flux = 0.f;
        for (int k = kLo; k <= kHi; ++k)
        {
            const float m = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            const float lm = std::log1p (4.0f * m);
            if (f > 0) flux += std::max (0.f, lm - prev[(size_t) k]);
            prev[(size_t) k] = lm;
        }
        env[(size_t) f] = flux;
    }

    // remove slow trend, half-wave rectify, remove mean, normalise
    std::vector<double> cs ((size_t) frames + 1, 0.0);
    for (int i = 0; i < frames; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + env[(size_t) i];
    const int half = std::max (2, (int) (0.4 * sr / hop));
    std::vector<float> e2 ((size_t) frames, 0.f);
    double mean = 0.0;
    for (int i = 0; i < frames; ++i)
    {
        const int a = std::max (0, i - half), b = std::min (frames, i + half + 1);
        const double loc = (cs[(size_t) b] - cs[(size_t) a]) / (double) (b - a);
        const float v = (float) std::max (0.0, (double) env[(size_t) i] - loc);
        e2[(size_t) i] = v; mean += v;
    }
    mean /= (double) frames;
    double ss = 0.0;
    for (auto& v : e2) { v = (float) (v - mean); ss += (double) v * v; }
    const float inv = (float) (1.0 / (std::sqrt (ss / frames) + 1e-9));
    for (auto& v : e2) v *= inv;
    return e2;
}

struct TempoResult { double bpm = 0.0; float conf = 0.f; };

inline TempoResult estimateTempo (const std::vector<float>& env, double envRate)
{
    TempoResult res;
    const int N = (int) env.size();
    if (N < (int) (envRate * 6.0)) return res;

    int maxLag = (int) std::ceil (envRate * 60.0 / 50.0 * 4.0) + 4;
    maxLag = std::min (maxLag, N / 2 - 2);
    if (maxLag < 16) return res;

    std::vector<double> R ((size_t) maxLag + 2, 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < N; ++i) s += (double) env[(size_t) i] * env[(size_t) (i + lag)];
        R[(size_t) lag] = s / (double) (N - lag);
    }
    auto Rat = [&] (double lag) -> double
    {
        if (lag < 0.0 || lag >= (double) maxLag) return 0.0;
        const int i = (int) lag; const double fr = lag - i;
        return R[(size_t) i] * (1.0 - fr) + R[(size_t) i + 1] * fr;
    };
    const double r0 = std::max (R[0], 1e-9);
    auto prior = [] (double b) { const double o = std::log2 (b / 110.0) / 0.9; return std::exp (-0.5 * o * o); };
    const double wts[4] = { 1.0, 0.75, 0.6, 0.5 };

    // 1) coarse: autocorrelation comb over 50..220 BPM, with a gentle tempo prior
    double bestS = -1e30, bestB = 0.0;
    for (double b = 50.0; b <= 220.0; b += 0.05)
    {
        const double L = envRate * 60.0 / b;
        double s = 0.0;
        for (int m = 1; m <= 4; ++m)
            if (m * L < (double) maxLag) s += wts[m - 1] * Rat (m * L);
        s *= prior (b);
        if (s > bestS) { bestS = s; bestB = b; }
    }
    if (bestB <= 0.0) return res;

    // 2) fine: pulse-train fit over the whole envelope (phase optimised)
    auto pulseScore = [&] (double b) -> double
    {
        const double P = envRate * 60.0 / b;
        double best = -1e30;
        for (double ph = 0.0; ph < P; ph += 0.5)
        {
            double s = 0.0; int cnt = 0;
            for (double t = ph; t < (double) (N - 1); t += P)
            {
                const int i = (int) t; const double fr = t - i;
                s += env[(size_t) i] * (1.0 - fr) + env[(size_t) i + 1] * fr; ++cnt;
            }
            if (cnt > 0) { s /= cnt; best = std::max (best, s); }
        }
        return best;
    };
    double fineB = bestB, fineS = -1e30;
    for (double b = bestB - 1.0; b <= bestB + 1.0; b += 0.01)
    {
        if (b < 40.0) continue;
        const double s = pulseScore (b);
        if (s > fineS) { fineS = s; fineB = b; }
    }
    // snap to a whole BPM when it fits almost as well (most DAW projects use whole tempos)
    const double rI = std::round (fineB);
    if (std::abs (fineB - rI) <= 0.3 && pulseScore (rI) >= 0.985 * fineS) fineB = rI;

    res.bpm = fineB;
    const double L = envRate * 60.0 / fineB;
    res.conf = (float) std::min (99.0, std::max (30.0, 35.0 + 110.0 * Rat (L) / r0));
    return res;
}

//==============================================================================
//  Key detection: tuning-corrected peak-picked chroma + Krumhansl/Temperley profiles
inline void forEachPeak (const std::vector<float>& x, double sr, double fLo, double fHi, int frameStep,
                         const std::function<void (double, double)>& onPeak,
                         const std::function<void()>& onFrameEnd, const StopFn& stop)
{
    const int N = 8192, HOP = 4096;
    if ((int) x.size() < N * 2) return;
    FFT fft (N);
    std::vector<float> w ((size_t) N), re ((size_t) N), im ((size_t) N), mg ((size_t) (N / 2 + 1));
    for (int i = 0; i < N; ++i) w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));
    const int frames = ((int) x.size() - N) / HOP + 1;
    const int kLo = std::max (2, (int) (fLo * N / sr)), kHi = std::min (N / 2 - 2, (int) (fHi * N / sr));

    for (int f = 0; f < frames; f += frameStep)
    {
        if (stop()) return;
        const float* p = x.data() + (size_t) f * HOP;
        for (int i = 0; i < N; ++i) { re[(size_t) i] = p[i] * w[(size_t) i]; im[(size_t) i] = 0.f; }
        fft.run (re.data(), im.data(), false);
        float fm = 0.f;
        for (int k = kLo - 1; k <= kHi + 1; ++k)
        {
            mg[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
            if (k >= kLo && k <= kHi) fm = std::max (fm, mg[(size_t) k]);
        }
        if (fm < 1e-3f) continue;
        for (int k = kLo; k <= kHi; ++k)
        {
            const float m = mg[(size_t) k];
            if (m > mg[(size_t) k - 1] && m >= mg[(size_t) k + 1] && m > 0.05f * fm)
            {
                const double a = mg[(size_t) k - 1], b = m, c = mg[(size_t) k + 1];
                const double den = a - 2.0 * b + c;
                double d = (std::abs (den) > 1e-12) ? 0.5 * (a - c) / den : 0.0;
                d = std::max (-0.5, std::min (0.5, d));
                onPeak ((k + d) * sr / N, (double) (m / fm));
            }
        }
        onFrameEnd();
    }
}

inline double estimateTuning (const std::vector<float>& x, double sr, const StopFn& stop)
{
    double hist[50] = {};
    forEachPeak (x, sr, 60.0, 2500.0, 3,
        [&] (double f, double wt)
        {
            const double pitch = 69.0 + 12.0 * std::log2 (f / 440.0);
            const double dev = pitch - std::round (pitch);
            int bin = (int) ((dev + 0.5) * 50.0);
            bin = std::max (0, std::min (49, bin));
            hist[bin] += wt;
        },
        [] {}, stop);
    int bi = 0; double bv = -1.0;
    for (int i = 0; i < 50; ++i)
    {
        const double v = hist[(i + 49) % 50] + hist[i] + hist[(i + 1) % 50];
        if (v > bv) { bv = v; bi = i; }
    }
    return ((double) bi + 0.5) / 50.0 - 0.5;
}

inline void chromaOf (const std::vector<float>& x, double sr, double fLo, double fHi, double tuning,
                      double* chroma, const StopFn& stop)
{
    for (int i = 0; i < 12; ++i) chroma[i] = 0.0;
    double fr[12] = {};
    forEachPeak (x, sr, fLo, fHi, 1,
        [&] (double f, double wt)
        {
            const double pitch = 69.0 + 12.0 * std::log2 (f / 440.0) - tuning;
            const int pc = (((int) std::lround (pitch)) % 12 + 12) % 12;
            fr[pc] += std::sqrt (wt);
        },
        [&]
        {
            double s = 0.0;
            for (int i = 0; i < 12; ++i) s += fr[i];
            if (s > 1e-9) for (int i = 0; i < 12; ++i) chroma[i] += fr[i] / s;
            for (int i = 0; i < 12; ++i) fr[i] = 0.0;
        }, stop);
}

struct KeyResult { int root = -1; bool minor = false; float conf = 0.f; int relRoot = -1; };

inline double pearson (const double* a, const double* b)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
    ma /= 12.0; mb /= 12.0;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < 12; ++i)
    {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db; saa += da * da; sbb += db * db;
    }
    return sab / (std::sqrt (saa * sbb) + 1e-12);
}

inline KeyResult keyFromChroma (const double* c)
{
    static const double ksMaj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double ksMin[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    static const double tmMaj[12] = { 5.0, 2.0, 3.5, 2.0, 4.5, 4.0, 2.0, 4.5, 2.0, 3.5, 1.5, 4.0 };
    static const double tmMin[12] = { 5.0, 2.0, 3.5, 4.5, 2.0, 4.0, 2.0, 4.5, 3.5, 2.0, 1.5, 4.0 };
    KeyResult r;
    double sum = 0.0;
    for (int i = 0; i < 12; ++i) sum += c[i];
    if (sum < 1e-9) return r;

    double sc[24];
    for (int root = 0; root < 12; ++root)
    {
        double a[12], b[12], d[12], e[12];
        for (int i = 0; i < 12; ++i)
        {
            const int j = (i - root + 12) % 12;
            a[i] = ksMaj[j]; b[i] = ksMin[j]; d[i] = tmMaj[j]; e[i] = tmMin[j];
        }
        sc[root]      = 0.5 * (pearson (c, a) + pearson (c, d));
        sc[12 + root] = 0.5 * (pearson (c, b) + pearson (c, e));
    }
    int bi = 0;
    for (int i = 1; i < 24; ++i) if (sc[i] > sc[bi]) bi = i;
    const int root = bi % 12; const bool minor = bi >= 12;
    const int relIdx = minor ? (root + 3) % 12 : 12 + (root + 9) % 12;
    double second = -2.0;
    for (int i = 0; i < 24; ++i) if (i != bi && i != relIdx && sc[i] > second) second = sc[i];
    const double margin = sc[bi] - second;
    r.root = root; r.minor = minor;
    r.relRoot = minor ? (root + 3) % 12 : (root + 9) % 12;
    r.conf = (float) std::min (99.0, std::max (30.0, 40.0 + 60.0 * sc[bi] + 150.0 * margin));
    return r;
}
} // namespace ds
