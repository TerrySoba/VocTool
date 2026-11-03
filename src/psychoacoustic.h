#ifndef PSYCHOACOUSTIC_H
#define PSYCHOACOUSTIC_H

#include <cstdint>
#include <vector>

/*
 * A simple psychoacoustic model that estimates how audible the coding noise is.
 *
 * The audio is analysed in short overlapping frames (about 23 ms, Hann window). For every frame
 * the masking threshold is computed from the reference signal, similar to the MPEG-1 psychoacoustic
 * model / Johnston's perceptual coder:
 *   - the spectrum is grouped into critical bands of 1 Bark width,
 *   - the band energies are spread over neighbouring bands with the Schroeder spreading function,
 *   - an offset depending on the tonality of the frame (spectral flatness) is subtracted, as tones
 *     mask noise less than noise does,
 *   - the threshold is never below the absolute threshold of hearing (Terhardt), assuming that a
 *     full scale sine is played at kFullScaleSplDb.
 *
 * The coding noise (decoded - reference) is analysed the same way, and the noise-to-mask ratio
 * (NMR) of every band is computed. The result is the "Total NMR" known from PEAQ (ITU-R BS.1387):
 * the NMR averaged over all bands and frames, in dB. Values below 0 dB mean that the noise is
 * mostly masked, lower is better.
 */

/** Assumed playback level of a full scale sine in dB SPL, used for the absolute threshold of hearing. */
constexpr double kFullScaleSplDb = 90.0;

/** Lowest value returned by the model (for noise that is practically zero). */
constexpr double kMinimumPerceptualNoiseDb = -120.0;

/**
 * Psychoacoustic model of one reference signal. The masking thresholds are computed once in the
 * constructor, so many decoded versions can be rated cheaply. noiseToMaskRatio() is thread safe.
 */
class PerceptualModel
{
public:
    /**
     * @param reference  Original samples in range -1.0 .. 1.0.
     * @param sampleRate Sample rate in Hz.
     */
    PerceptualModel(const std::vector<double>& reference, uint32_t sampleRate);

    /**
     * Returns the perceptual noise (total noise-to-mask ratio in dB) of decoded compared to the reference.
     *
     * @param decoded Decoded unsigned 8-bit samples (128 = silence). Must have at least as many samples
     *                as the reference, additional samples (padding) are ignored.
     */
    double noiseToMaskRatio(const std::vector<uint8_t>& decoded) const;

private:
    std::vector<double> powerSpectrum(const std::vector<double>& signal, size_t start) const;
    std::vector<double> bandEnergies(const std::vector<double>& power) const;

    std::vector<double> m_reference;      // in 8-bit units, -128 .. 127
    size_t m_frameLength;
    std::vector<double> m_window;
    std::vector<int> m_bandOfBin;         // critical band of every FFT bin, -1 for ignored bins
    int m_bandCount;
    std::vector<double> m_thresholds;     // masking threshold per frame and band
};

/** Convenience function, see PerceptualModel. */
double perceptualNoise(const std::vector<double>& reference, const std::vector<uint8_t>& decoded, uint32_t sampleRate);

#endif
