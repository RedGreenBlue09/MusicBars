
typedef enum {
	DftWindowId_Rectangular,
	DftWindowId_Parabolic,
	DftWindowId_Sine,
	DftWindowId_Sinc,
	DftWindowId_Hann,
	DftWindowId_Gauss,
	DftWindowId_HannPoisson,
	DftWindowId_EnumCount
} dft_window_id;

typedef struct {
	double Area;          // Integral from 0 to 1 of the window
	double MainLobeRange; // Estimated range of the main lobe
	double MainLobeArea;  // Integral from -1 to 1 of the main lobe
	double (*Compute)(double X);
	double (*ComputeMainLobe)(double X);
} dft_window_info;

dft_window_info gaDftWindowInfo[];
