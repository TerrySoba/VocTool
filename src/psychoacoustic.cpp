#include "psychoacoustic.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace {

const double kPi = 3.14159265358979323846;

/** In-place iterative radix-2 FFT, size must be a power of two. */
void fft(std::vector<std::complex<double>>& data)
{
    const size_t n = data.size();

    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    for (size_t length = 2; length <= n; length <<= 1)
    {
        const std::complex<double> rotation = std::polar(1.0, -2.0 * kPi / length);
        for (size_t i = 0; i < n; i += length)
        {
            std::complex<double> w = 1.0;
            for (size_t k = 0; k < length / 2; ++k)
            {
                const std::complex<double> u = data[i + k];
                const std::complex<double> v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= rotation;
            }
        }
    }
}

/** Critical band rate in Bark (Zwicker & Terhardt). */
double hertzToBark(double frequency)
{
    return 13.0 * std::atan(0.00076 * frequency) + 3.5 * std::atan(std::pow(frequency / 7500.0, 2.0));
}

/** Absolute threshold of hearing in dB SPL (Terhardt). */
double thresholdInQuiet(double frequency)
{
    const double f = std::max(frequency, 20.0) / 1000.0;
    return 3.64 * std::pow(f, -0.8) - 6.5 * std::exp(-0.6 * std::pow(f - 3.3, 2.0)) + 1e-3 * std::pow(f, 4.0);
}

/**
 * Centered moving average over length samples (length / 2 on each side). At the ends only the
 * available samples are averaged.
 */
std::vector<double> movingAverage(const std::vector<double>& input, size_t length)
{
    const size_t half = length / 2;
    std::vector<double> prefix(input.size() + 1, 0.0);
    for (size_t i = 0; i < input.size(); ++i)
    {
        prefix[i + 1] = prefix[i] + input[i];
    }

    std::vector<double> output(input.size());
    for (size_t i = 0; i < input.size(); ++i)
    {
        const size_t begin = (i > half) ? i - half : 0;
        const size_t end = std::min(i + half + 1, input.size());
        output[i] = (prefix[end] - prefix[begin]) / static_cast<double>(end - begin);
    }
    return output;
}

/** Schroeder spreading function as energy factor, dz = maskee - masker in Bark. */
double spreading(double dz)
{
    const double x = dz + 0.474;
    return std::pow(10.0, (15.81 + 7.5 * x - 17.5 * std::sqrt(1.0 + x * x)) / 10.0);
}

} // namespace


PerceptualModel::PerceptualModel(const std::vector<double>& reference, uint32_t sampleRate)
    : m_sampleRate(sampleRate)
{
    if (sampleRate == 0)
    {
        throw std::runtime_error("Invalid sample rate");
    }

    // same mapping as the encoder: -1.0 .. 1.0 => 0 .. 255, then centered
    m_reference.reserve(reference.size());
    for (double sample : reference)
    {
        m_reference.push_back(std::clamp(sample * 128.0 + 128.0, 0.0, 255.0) - 128.0);
    }

    // about 23 ms frames, as in MPEG-1 layer 3 at 44.1 kHz
    m_frameLength = 64;
    while (m_frameLength < 0.023 * sampleRate) m_frameLength *= 2;

    m_window.resize(m_frameLength);
    for (size_t i = 0; i < m_frameLength; ++i)
    {
        m_window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * (i + 0.5) / m_frameLength);
    }

    // Bin 0 (DC) is ignored, DC errors are not audible (see lowFrequencyDrift() for slow drift).
    const size_t binCount = m_frameLength / 2 + 1;
    m_bandOfBin.assign(binCount, -1);
    m_bandCount = 0;
    for (size_t k = 1; k < binCount; ++k)
    {
        const double frequency = static_cast<double>(k) * sampleRate / m_frameLength;
        m_bandOfBin[k] = static_cast<int>(hertzToBark(frequency));
        m_bandCount = std::max(m_bandCount, m_bandOfBin[k] + 1);
    }

    // absolute threshold per band: the most sensitive frequency in the band, relative to full scale
    std::vector<double> quietThreshold(m_bandCount, std::numeric_limits<double>::max());
    for (size_t k = 1; k < binCount; ++k)
    {
        const double frequency = static_cast<double>(k) * sampleRate / m_frameLength;
        const double energy = std::pow(10.0, (thresholdInQuiet(frequency) - kFullScaleSplDb) / 10.0);
        quietThreshold[m_bandOfBin[k]] = std::min(quietThreshold[m_bandOfBin[k]], energy);
    }

    // Spreading matrix, normalized so that a flat spectrum keeps its level (Johnston).
    std::vector<double> spread(m_bandCount * m_bandCount);
    std::vector<double> spreadGain(m_bandCount, 0.0);
    for (int maskee = 0; maskee < m_bandCount; ++maskee)
    {
        for (int masker = 0; masker < m_bandCount; ++masker)
        {
            spread[maskee * m_bandCount + masker] = spreading(maskee - masker);
            spreadGain[maskee] += spread[maskee * m_bandCount + masker];
        }
    }

    const size_t hop = m_frameLength / 2;
    for (size_t start = 0; start < m_reference.size(); start += hop)
    {
        const std::vector<double> power = powerSpectrum(m_reference, start);
        const std::vector<double> energies = bandEnergies(power);

        // Tonality from the spectral flatness of the frame: 0 dB = noise, -60 dB or less = pure tone.
        double logSum = 0.0, sum = 0.0;
        for (size_t k = 1; k < binCount; ++k)
        {
            logSum += std::log(power[k] + 1e-20);
            sum += power[k] + 1e-20;
        }
        const double flatnessDb = 10.0 * std::log10(std::exp(logSum / (binCount - 1)) / (sum / (binCount - 1)));
        const double tonality = std::clamp(flatnessDb / -60.0, 0.0, 1.0);

        for (int band = 0; band < m_bandCount; ++band)
        {
            double excitation = 0.0;
            for (int masker = 0; masker < m_bandCount; ++masker)
            {
                excitation += spread[band * m_bandCount + masker] * energies[masker];
            }
            excitation /= spreadGain[band];

            const double offsetDb = tonality * (14.5 + band) + (1.0 - tonality) * 5.5;
            const double threshold = excitation * std::pow(10.0, -offsetDb / 10.0);
            m_thresholds.push_back(std::max(threshold, quietThreshold[band]));
        }
    }
}

/**
 * Returns the power spectrum (bins 0 .. N/2) of the frame starting at start, relative to the
 * energy of a full scale sine. Samples after the end of the signal are zero.
 */
std::vector<double> PerceptualModel::powerSpectrum(const std::vector<double>& signal, size_t start) const
{
    std::vector<std::complex<double>> spectrum(m_frameLength);
    for (size_t i = 0; i < m_frameLength && start + i < signal.size(); ++i)
    {
        spectrum[i] = signal[start + i] * m_window[i];
    }
    fft(spectrum);

    // Parseval: a sine with amplitude A has a one-sided energy of N * sum(w^2) * A^2 / 4
    const double windowEnergy = std::inner_product(m_window.begin(), m_window.end(), m_window.begin(), 0.0);
    const double fullScale = m_frameLength * windowEnergy * 128.0 * 128.0 / 4.0;

    std::vector<double> power(m_frameLength / 2 + 1);
    for (size_t k = 0; k < power.size(); ++k)
    {
        power[k] = std::norm(spectrum[k]) / fullScale;
    }
    return power;
}

/** Sums the power spectrum into critical bands. */
std::vector<double> PerceptualModel::bandEnergies(const std::vector<double>& power) const
{
    std::vector<double> energies(m_bandCount, 0.0);
    for (size_t k = 1; k < m_bandOfBin.size(); ++k)
    {
        energies[m_bandOfBin[k]] += power[k];
    }
    return energies;
}

/** Returns decoded - reference in 8-bit units, padding samples of decoded are ignored. */
std::vector<double> PerceptualModel::noise(const std::vector<uint8_t>& decoded) const
{
    if (decoded.size() < m_reference.size())
    {
        throw std::logic_error("Decoded signal is shorter than reference");
    }

    std::vector<double> noise(m_reference.size());
    for (size_t i = 0; i < noise.size(); ++i)
    {
        noise[i] = (static_cast<double>(decoded[i]) - 128.0) - m_reference[i];
    }
    return noise;
}

double PerceptualModel::noiseToMaskRatio(const std::vector<uint8_t>& decoded) const
{
    const std::vector<double> noise = this->noise(decoded);

    const size_t hop = m_frameLength / 2;
    double sum = 0.0;
    size_t frames = 0;
    for (size_t start = 0, frame = 0; start < noise.size(); start += hop, ++frame)
    {
        const std::vector<double> energies = bandEnergies(powerSpectrum(noise, start));
        double frameSum = 0.0;
        for (int band = 0; band < m_bandCount; ++band)
        {
            frameSum += energies[band] / m_thresholds[frame * m_bandCount + band];
        }
        sum += frameSum / m_bandCount;
        ++frames;
    }

    if (frames == 0)
    {
        return kMinimumPerceptualNoiseDb;
    }
    return std::max(10.0 * std::log10(sum / frames), kMinimumPerceptualNoiseDb);
}

double PerceptualModel::lowFrequencyDrift(const std::vector<uint8_t>& decoded) const
{
    // A 50 ms moving average has its first zero at 20 Hz. Two of them in a row (a triangular window)
    // suppress the side lobes, so audible low frequencies hardly contribute.
    const size_t length = std::max<size_t>(1, m_sampleRate / 20);
    const std::vector<double> drift = movingAverage(movingAverage(noise(decoded), length), length);

    double maximum = 0.0;
    for (double value : drift)
    {
        maximum = std::max(maximum, std::abs(value));
    }
    return maximum;
}

double lowFrequencyDrift(const std::vector<double>& reference, const std::vector<uint8_t>& decoded, uint32_t sampleRate)
{
    return PerceptualModel(reference, sampleRate).lowFrequencyDrift(decoded);
}

double perceptualNoise(const std::vector<double>& reference, const std::vector<uint8_t>& decoded, uint32_t sampleRate)
{
    return PerceptualModel(reference, sampleRate).noiseToMaskRatio(decoded);
}
