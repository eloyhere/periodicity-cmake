// Compile-and-smoke tests for the mathematic module.
//
// The point of this suite is integration, not algorithm coverage: including the
// headers below proves that the public spelling <md/mathematic/...> and the
// md::mathematic namespace work, and every case checks an invariant simple
// enough that a failure points at the port rather than at the mathematics.
// The transform and resampling cases carry the measured tolerances from the
// development runs; the comments state why each tolerance is what it is.

#include <QTest>

#include <md/mathematic/filter.h>
#include <md/mathematic/kernel.h>
#include <md/mathematic/matrix.h>
#include <md/mathematic/sample.h>
#include <md/mathematic/sampler.h>
#include <md/mathematic/spectrum.h>
#include <md/mathematic/statistics.h>
#include <md/mathematic/window.h>
#include <md/mathematic/transforms/hilbert.h>
#include <md/mathematic/transforms/laplace.h>
#include <md/mathematic/transforms/mellin.h>
#include <md/mathematic/transforms/z_transform.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <utility>
#include <vector>

using md::mathematic::Biquad;
using md::mathematic::FilterChain;
using md::mathematic::KernelId;
using md::mathematic::Matrix;
using md::mathematic::PowerSpectrum;
using md::mathematic::Sample;
using md::mathematic::SampleIndex;
using md::mathematic::SampleRate;
using md::mathematic::WindowId;
using md::mathematic::WindowType;

namespace {

constexpr double testPi = 3.141592653589793238462643383279502884;

// Signal-to-noise ratio of a reconstruction against the analytic answer,
// ignoring `margin` samples at both ends where the kernel has no support.
double snrDb(const std::vector<double>& got, const std::vector<double>& exact,
    const std::size_t margin)
{
    double signal = 0.0;
    double error = 0.0;
    for (std::size_t index = margin; index + margin < got.size(); ++index) {
        signal += exact[index] * exact[index];
        const double difference = got[index] - exact[index];
        error += difference * difference;
    }
    return 10.0 * std::log10(signal / error);
}

std::size_t zeroCrossings(const std::vector<double>& values, const std::size_t margin)
{
    std::size_t count = 0;
    for (std::size_t index = margin + 1; index + margin < values.size(); ++index) {
        if ((values[index] < 0.0) != (values[index - 1] < 0.0)) {
            ++count;
        }
    }
    return count;
}

std::vector<double> logSpaced(const double first, const double last, const std::size_t count)
{
    std::vector<double> values(count);
    const double firstLog = std::log(first);
    const double lastLog = std::log(last);
    for (std::size_t index = 0; index < count; ++index) {
        const double fraction = static_cast<double>(index) / static_cast<double>(count - 1);
        values[index] = std::exp(firstLog + (lastLog - firstLog) * fraction);
    }
    return values;
}

} // namespace

class TestMathematicPort : public QObject
{
    Q_OBJECT

private slots:
    void matrixMultiplyTwoByTwo();
    void statisticsMeanMedianAndVariance();
    void filterPassesThroughAndScales();
    void windowIsSymmetricAndPeaksAtOne();
    void powerSpectrumFindsItsPeak();
    void sampleDescribesItsBuffer();
    void laplaceOfExponentialMatchesFormula();
    void laplaceOfSineMatchesFormulaNearImaginaryAxis();
    void inverseLaplaceRoundTripsDampedSinusoid();
    void mellinOfExponentialMatchesGamma();
    void inverseMellinRoundTripsExponential();
    void interpolationKernelsReproduceGridValues();
    void resamplingKeepsSineFrequencyAndQuality();
    void decimateInterpolateRoundTrip();
    void warpIdentityReproducesInput();
    void fractionalDelayOnRamp();
    void hilbertAndZTransformRoundTrip();
};

void TestMathematicPort::matrixMultiplyTwoByTwo()
{
    const Matrix<double> left(2, 2, std::vector<double>{1.0, 2.0, 3.0, 4.0});
    const Matrix<double> right(2, 2, std::vector<double>{5.0, 6.0, 7.0, 8.0});

    const Matrix<double> product = left * right;

    QCOMPARE(int(product.rows()), 2);
    QCOMPARE(int(product.columns()), 2);
    QCOMPARE(product(0, 0), 19.0);
    QCOMPARE(product(0, 1), 22.0);
    QCOMPARE(product(1, 0), 43.0);
    QCOMPARE(product(1, 1), 50.0);
}

void TestMathematicPort::statisticsMeanMedianAndVariance()
{
    const std::vector<double> samples{1.0, 2.0, 3.0};

    QCOMPARE(md::mathematic::mean(samples), 2.0);
    QCOMPARE(md::mathematic::median(samples), 2.0);
    // The unbiased variance of {1, 2, 3} is ((1)^2 + 0 + (1)^2) / 2.
    QCOMPARE(md::mathematic::variance(samples), 1.0);
}

void TestMathematicPort::filterPassesThroughAndScales()
{
    // A default biquad is the identity: b0 = 1 and every other coefficient 0.
    Biquad<double> identity;
    QCOMPARE(identity.process(0.25), 0.25);
    QCOMPARE(identity.process(-2.0), -2.0);

    const Biquad<double>::Coefficients halving{0.5, 0.0, 0.0, 0.0, 0.0};
    FilterChain<double> chain(std::vector<Biquad<double>>{Biquad<double>(halving)});

    QCOMPARE(int(chain.order()), 2);
    QCOMPARE(chain.process(0.5), 0.25);
    QCOMPARE(chain.gainAt(md::mathematic::Frequency<double>::fromHertz(0.0), 250.0), 0.5);
}

void TestMathematicPort::windowIsSymmetricAndPeaksAtOne()
{
    const std::vector<double> legacy = md::mathematic::makeWindow(WindowType::Hann, 9);
    const std::vector<double> compact = md::mathematic::window(WindowId::Hann, 9);

    QCOMPARE(int(legacy.size()), 9);
    QCOMPARE(int(compact.size()), 9);

    for (std::size_t index = 0; index < legacy.size(); ++index) {
        QVERIFY(std::abs(legacy[index] - legacy[legacy.size() - 1 - index]) < 1.0e-12);
        QVERIFY(std::abs(compact[index] - compact[compact.size() - 1 - index]) < 1.0e-12);
        QVERIFY(std::abs(compact[index] - legacy[index]) < 1.0e-12);
    }

    // The symmetric convention puts a zero at both ends and the peak in the middle.
    QVERIFY(std::abs(compact.front()) < 1.0e-12);
    QVERIFY(std::abs(compact.back()) < 1.0e-12);
    QVERIFY(std::abs(compact[4] - 1.0) < 1.0e-12);
    QVERIFY(std::abs(*std::max_element(compact.begin(), compact.end()) - 1.0) < 1.0e-12);

    QCOMPARE(md::mathematic::coherentGain(WindowId::Hann, 9),
        md::mathematic::coherentGain(compact));
}

void TestMathematicPort::powerSpectrumFindsItsPeak()
{
    const PowerSpectrum<double> spectrum(
        std::vector<double>{1.0, 2.0, 3.0},
        std::vector<double>{0.5, 1.0, 4.0});

    QCOMPARE(int(spectrum.size()), 3);
    QCOMPARE(spectrum.peakIndex(), std::size_t(2));
    QCOMPARE(spectrum.peakFrequency(), 3.0);
    // Bins are one hertz apart, so the integral is the plain sum.
    QCOMPARE(spectrum.totalPower(), 5.5);
}

void TestMathematicPort::sampleDescribesItsBuffer()
{
    const SampleRate<double> rate = SampleRate<double>::fromHertz(250.0);
    const Sample<double> buffer = Sample<double>::fromRateAndCount(rate, 1000);

    QCOMPARE(buffer.rate().hertz(), 250.0);
    QCOMPARE(int(buffer.count()), 1000);
    QCOMPARE(buffer.duration(), 4.0);
    QVERIFY(buffer.contains(SampleIndex::fromOffset(999)));
    QVERIFY(!buffer.contains(SampleIndex::fromOffset(1000)));
}

void TestMathematicPort::laplaceOfExponentialMatchesFormula()
{
    // L{exp(-a t)} = 1 / (s + a). The samples are dense enough that the cubic
    // panel interpolant, not the sampling, limits the error; the measured
    // worst case over the grid below is 2.1e-8.
    const double a = 0.5;
    std::vector<double> times;
    std::vector<std::complex<double>> values;
    for (int index = 0; index <= 3000; ++index) {
        const double time = 0.01 * index;
        times.push_back(time);
        values.push_back(std::exp(-a * time));
    }

    double worst = 0.0;
    for (const double sigma : {0.2, 0.5, 1.0, 2.0}) {
        for (const double omega : {-5.0, -1.0, 0.0, 1.0, 5.0}) {
            const std::complex<double> s(sigma, omega);
            const std::complex<double> exact = 1.0 / (s + a);
            worst = std::max(worst,
                std::abs(md::mathematic::laplace(times, values, s) - exact));
        }
    }
    qInfo("laplace exp(-0.5 t): worst grid error %.3e", worst);
    QVERIFY(worst < 1.0e-6);

    const md::mathematic::LaplaceRegion<double> region =
        md::mathematic::estimateLaplaceRegion(times, values);
    QVERIFY(region.estimated);
    QVERIFY(std::abs(region.abscissa + a) < 1.0e-3);
    QVERIFY(region.contains(std::complex<double>(0.0, 1.0)));
    QVERIFY(!region.contains(std::complex<double>(-1.0, 0.0)));
}

void TestMathematicPort::laplaceOfSineMatchesFormulaNearImaginaryAxis()
{
    // L{sin(w0 t)} = w0 / (s^2 + w0^2). The one-sided integral does not
    // converge exactly on the imaginary axis, so the check sits a small
    // distance to its right (Re s = 0.05) and compares the formula at that
    // same s. The measured error is 4.4e-8.
    const double w0 = 2.0;
    std::vector<double> times;
    std::vector<std::complex<double>> values;
    for (int index = 0; index <= 20000; ++index) {
        const double time = 0.02 * index;
        times.push_back(time);
        values.push_back(std::sin(w0 * time));
    }

    const std::complex<double> s(0.05, 3.0);
    const std::complex<double> exact = w0 / (s * s + w0 * w0);
    const std::complex<double> got = md::mathematic::laplace(times, values, s);
    const double error = std::abs(got - exact);
    qInfo("laplace sin(2 t) at 0.05+3j: error %.3e", error);
    QVERIFY(error < 1.0e-6);
}

void TestMathematicPort::inverseLaplaceRoundTripsDampedSinusoid()
{
    // f(t) = exp(-a t) cos(w0 t) has F(s) = (s+a) / ((s+a)^2 + w0^2).
    const double a = 0.5;
    const double w0 = 2.0;
    const auto transform = [a, w0](const std::complex<double>& s) {
        return (s + a) / ((s + a) * (s + a) + w0 * w0);
    };

    double worst = 0.0;
    for (const double time : {0.2, 0.5, 1.0, 2.0, 4.0}) {
        const double exact = std::exp(-a * time) * std::cos(w0 * time);
        const double got = md::mathematic::inverseLaplace(transform, time, 24).real();
        worst = std::max(worst, std::abs(got - exact));
    }
    qInfo("inverse Laplace: worst error %.3e (fixed Talbot, 24 nodes)", worst);
    QVERIFY(worst < 1.0e-9);
}

void TestMathematicPort::mellinOfExponentialMatchesGamma()
{
    // M{exp(-x)}(s) = Gamma(s). The logarithmic grid resolves the x -> 0 end;
    // the measured worst case over the grid is 3.4e-7.
    const std::vector<double> x = logSpaced(1.0e-7, 60.0, 20001);
    std::vector<std::complex<double>> values(x.size());
    for (std::size_t index = 0; index < x.size(); ++index) {
        values[index] = std::exp(-x[index]);
    }

    double worst = 0.0;
    for (const double c : {0.5, 1.0, 2.0}) {
        for (const double t : {-10.0, -2.0, 0.0, 2.0, 10.0}) {
            const std::complex<double> s(c, t);
            worst = std::max(worst,
                std::abs(md::mathematic::mellin(x, values, s)
                    - md::mathematic::complexGamma(s)));
        }
    }
    qInfo("Mellin exp(-x): worst gamma error %.3e", worst);
    QVERIFY(worst < 1.0e-5);

    QVERIFY(std::abs(md::mathematic::complexGamma(std::complex<double>(1.5, 0.0)).real()
        - std::tgamma(1.5)) < 1.0e-13);
}

void TestMathematicPort::inverseMellinRoundTripsExponential()
{
    // F(s) = Gamma(s) inverts to exp(-x); the Bromwich trapezoid reaches
    // 5.5e-16 for these points.
    auto transform = [](const std::complex<double>& s) {
        return md::mathematic::complexGamma(s);
    };

    double worst = 0.0;
    for (const double point : {0.5, 1.0, 1.5, 2.5}) {
        const double got = md::mathematic::inverseMellin(transform, point).real();
        worst = std::max(worst, std::abs(got - std::exp(-point)));
    }
    qInfo("inverse Mellin: worst error %.3e", worst);
    QVERIFY(worst < 1.0e-9);
}

void TestMathematicPort::interpolationKernelsReproduceGridValues()
{
    const std::vector<KernelId> kernels{
        KernelId::Nearest, KernelId::Linear, KernelId::CubicHermite,
        KernelId::CatmullRom, KernelId::BSpline, KernelId::Lanczos2,
        KernelId::Lanczos3, KernelId::BlackmanSinc, KernelId::WindowedSinc };

    std::vector<double> x;
    std::vector<double> y;
    for (int index = 0; index < 21; ++index) {
        x.push_back(0.25 * index);
        y.push_back(std::sin(0.3 * index));
    }

    for (const KernelId kernel : kernels) {
        for (int index = 0; index < 21; ++index) {
            QCOMPARE(md::mathematic::Sampler<double>::sampleAt(x, y, x[index], kernel), y[index]);
        }
    }
}

void TestMathematicPort::resamplingKeepsSineFrequencyAndQuality()
{
    // 44.1 kHz -> 48 kHz for a 1 kHz sine over 0.1 s. The output grid is
    // exact, so the frequency check counts zero crossings (2 per cycle) and
    // the quality check measures SNR against the analytic sine.
    const double inputRate = 44100.0;
    const double outputRate = 48000.0;
    const double frequency = 1000.0;
    const std::size_t inputLength = 4410;

    std::vector<double> input(inputLength);
    for (std::size_t index = 0; index < inputLength; ++index) {
        input[index] = std::sin(2.0 * testPi * frequency * index / inputRate);
    }

    const std::vector<std::pair<KernelId, double>> required{
        { KernelId::Lanczos3, 45.0 },
        { KernelId::CatmullRom, 70.0 } };

    for (const auto& entry : required) {
        const std::vector<double> output = md::mathematic::Sampler<double>::resampleRatio(
            input, 48000, 44100, entry.first);
        std::vector<double> ideal(output.size());
        for (std::size_t index = 0; index < output.size(); ++index) {
            ideal[index] = std::sin(2.0 * testPi * frequency * index / outputRate);
        }

        const double measured = snrDb(output, ideal, 100);
        const std::size_t crossings = zeroCrossings(output, 100);
        const double expectedCrossings = 2.0 * frequency
            * static_cast<double>(output.size() - 200) / outputRate;
        qInfo("resample kernel %d: %zu samples, SNR %.2f dB, %zu zero crossings (expected %.1f)",
            int(entry.first), output.size(), measured, crossings, expectedCrossings);

        QCOMPARE(int(output.size()), 4800);
        QVERIFY(std::abs(static_cast<double>(crossings) - expectedCrossings) <= 3.0);
        QVERIFY(measured > entry.second);
    }
}

void TestMathematicPort::decimateInterpolateRoundTrip()
{
    // A 200 Hz sine at 48 kHz, decimated by four and interpolated back. The
    // interpolator pins the original samples, so the remaining error is the
    // decimation low-pass ripple; the measured maximum is 1.5e-4.
    std::vector<double> signal(4800);
    for (std::size_t index = 0; index < signal.size(); ++index) {
        signal[index] = std::sin(2.0 * testPi * 200.0 * index / 48000.0);
    }

    const std::vector<double> decimated = md::mathematic::Sampler<double>::decimate(signal, 4);
    const std::vector<double> restored = md::mathematic::Sampler<double>::interpolate(decimated, 4);

    QCOMPARE(int(decimated.size()), 1200);
    QCOMPARE(int(restored.size()), 4797);

    double worst = 0.0;
    for (std::size_t index = 20; index < 1180; ++index) {
        worst = std::max(worst, std::abs(restored[4 * index] - signal[4 * index]));
    }
    qInfo("decimate/interpolate: worst error %.3e", worst);
    QVERIFY(worst < 1.0e-3);
}

void TestMathematicPort::warpIdentityReproducesInput()
{
    std::vector<double> x;
    std::vector<double> y;
    for (int index = 0; index < 100; ++index) {
        x.push_back(0.1 * index);
        y.push_back(std::cos(0.2 * index));
    }

    const std::vector<double> warped = md::mathematic::Sampler<double>::warp(
        x, y, [](double value) { return value; }, KernelId::Lanczos3);

    double worst = 0.0;
    for (std::size_t index = 0; index < y.size(); ++index) {
        worst = std::max(worst, std::abs(warped[index] - y[index]));
    }
    QVERIFY(worst < 1.0e-12);
}

void TestMathematicPort::fractionalDelayOnRamp()
{
    std::vector<double> ramp(20);
    for (int index = 0; index < 20; ++index) {
        ramp[index] = index;
    }

    // A half-sample delay of a ramp is the midpoint for every interpolating
    // cubic; both the kernel form and the four-point Lagrange form reproduce
    // it exactly (measured 0.0).
    for (const KernelId kernel : {KernelId::Linear, KernelId::CatmullRom}) {
        const std::vector<double> delayed =
            md::mathematic::Sampler<double>::fractionalDelay(ramp, 0.5, kernel);
        for (int index = 2; index < 18; ++index) {
            QVERIFY(std::abs(delayed[index] - (index - 0.5)) < 1.0e-9);
        }
    }

    const std::vector<double> lagrange = md::mathematic::Sampler<double>::fractionalDelay(
        ramp, 0.5, KernelId::CatmullRom, true);
    for (int index = 2; index < 18; ++index) {
        QVERIFY(std::abs(lagrange[index] - (index - 0.5)) < 1.0e-9);
    }
}

void TestMathematicPort::hilbertAndZTransformRoundTrip()
{
    // An integer-bin cosine has an exact analytic signal exp(j w n) away from
    // the DFT wrap, so the envelope is one and the phase step is w.
    const std::size_t count = 256;
    std::vector<double> cosine(count);
    for (std::size_t index = 0; index < count; ++index) {
        cosine[index] = std::cos(2.0 * testPi * 8.0 * index / count);
    }
    const std::vector<double> analytic = md::mathematic::envelope(cosine);
    for (std::size_t index = 40; index < 216; ++index) {
        QVERIFY(std::abs(analytic[index] - 1.0) < 1.0e-9);
    }

    const std::vector<double> sequence{1.0, 2.0, 3.0, 4.0, 0.0, 0.0, 0.0, 0.0};
    std::vector<double> angles(sequence.size());
    for (std::size_t index = 0; index < angles.size(); ++index) {
        angles[index] = 2.0 * testPi * index / angles.size();
    }
    const auto spectrum = md::mathematic::zTransformSpectrum(sequence, 0.8, angles);
    const std::vector<double> restored = md::mathematic::inverseZTransform(spectrum, 0.8);
    for (std::size_t index = 0; index < sequence.size(); ++index) {
        QVERIFY(std::abs(restored[index] - sequence[index]) < 1.0e-12);
    }
}

QTEST_GUILESS_MAIN(TestMathematicPort)

#include "tst_mathematic_port.moc"
