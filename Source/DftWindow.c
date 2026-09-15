
#include <math.h>

#include "DftWindow.h"

static const double gfPi = 0x1.921FB54442D18p1;
static inline double SincPi(double X) {
	double R = sin(gfPi * X) / (gfPi * X);
	return (X == 0.0) ? 1.0 : R;
}
static inline double CommonMainLobe(double X, double A, double B) {
	if (fabs(X) > (1.0 / A))
		return 0.0;
	return exp(-A * (X * X)) * fabs(SincPi(B * X));
}

// Rectangular

static double Rectangular(double X) {
	return 1.0;
}

static double RectangularMainLobe(double X) {
	if (fabs(X) > 1.0)
		return 0.0;
	return fabs(SincPi(X));
}

// Parabolic

static double Parabolic(double X) {
	return -4.0 * (X * X) + 4.0 * X;
}

static double ParabolicMainLobe(double X) {
	// Estimated
	return CommonMainLobe(X, 0.19, 0.699);
}

// Sine

static double Sine(double X) {
	return sin(gfPi * X);
}

static double SineMainLobe(double X) {
	// Estimated
	return CommonMainLobe(X, 0.2, 2.0 / 3.0);
}

// Sinc

static double Sinc(double X) {
	return SincPi(2.0 * X - 1.0);
}

static double SincMainLobe(double X) {
	// Estimated
	return CommonMainLobe(X, 0.24, 0.61);
}

// Hann

static double Hann(double X) {
	double Result = sin(gfPi * X);
	return Result * Result;
}

static double HannMainLobe(double X) {
	// Estimated
	return CommonMainLobe(X, 0.24, 0.5);
}

// Gaussian

static double Gaussian(double X) {
	X -= 0.5;
	return exp(-0x1.390DB36B282C8p4 * (X * X));
}

static double GaussianMainLobe(double X) {
	// Estimated
	return exp(-0.5 * (X * X));
}

// Hann-Poisson

static double HannPoisson(double X) {
	double Sin = sin(gfPi * X);
	return exp(-2.0 * fabs(X - 0.5)) * (Sin * Sin);
}

static double HannPoissonMainLobe(double X) {
	// Estimated
	const double Intersect = 2.45167;
	const double ParaCoeff = 0.518;
	const double LogCoeff = 2.0;
	const double LogOffset = 1.32;

	X = fabs(X);
	double Sigmoid = 1.0 / (1.0 + exp(-4.0 * (X - Intersect)));
	double Log = -LogCoeff * log(X) - LogOffset;
	double LogFix = -X + (LogCoeff + LogCoeff * log(LogCoeff) - LogOffset);
	Log = (X > LogCoeff) ? Log : LogFix;
	double Para = -ParaCoeff * (X * X);
	return exp(Sigmoid * Log + (1.0 - Sigmoid) * Para);
}

dft_window_info gaDftWindowInfo[DftWindowId_EnumCount] = {
	{
		1.0,
		2.0,
		0x1.2DD19DD527867p-1 * 2.0, // 2.0 * Si(pi) / pi
		Rectangular,
		RectangularMainLobe
	},
	{
		2.0 / 3.0,
		2.8606, // Estimated
		1.582, // Estimated
		Parabolic,
		ParabolicMainLobe
	},
	{
		0x1.45F306DC9C883p-1, // 2 / pi
		3.0,
		1.643, // Estimated
		Sine,
		SineMainLobe
	},
	{
		0x1.2DD19DD527867p-1, // Si(pi) / pi
		3.277, // Estimated
		1.743, // Estimated
		Sinc,
		SincMainLobe
	},
	{
		0.5, // Si(pi) / pi
		4,
		2.031, // Estimated
		Hann,
		HannMainLobe
	},
	{
		0.4,
		7.302, // Estimated
		2.507, // Estimated
		Gaussian,
		GaussianMainLobe
	},
	{
		0x1.8413FD8338362p-2, // (1 - 1 / e) / 2 + (1 + 1 / e) / (2 * (1 + pi ^ 2))
		1.0 / 0.0, // Inf
		2.7, // Estimated
		HannPoisson,
		HannPoissonMainLobe
	},
};

