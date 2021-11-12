// SPDX-FileCopyrightText: 2013 Jos� Ara�jo <josemariaaraujo@gmail.com>
// SPDX-FileCopyrightText: 2020 Jorge Maidana <jorgem.seq@gmail.com>
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// ExtIO_RTL.cpp - ExtIO wrapper for librtlsdr

#include <stdio.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <process.h>
#include <tchar.h>
#include <new>

#include "rtl-sdr.h"
#include "ExtIO_RTL.h"
#include "resource.h"

static HWND h_dialog;
static INT_PTR CALLBACK MainDlgProc(HWND, UINT, WPARAM, LPARAM);

typedef struct {
	const uint32_t value;
	const TCHAR *name;
} RtlSdrSampleRate;

static RtlSdrSampleRate RtlSdrSampleRateArr[] = {
	{ 3200000, TEXT("3200000") },
	{ 2880000, TEXT("2880000") },
	{ 2560000, TEXT("2560000") },
	{ 2400000, TEXT("2400000") },
	{ 1800000, TEXT("1800000") },
	{ 1440000, TEXT("1440000") },
	{ 1280000, TEXT("1280000") },
	{ 1024000, TEXT("1024000") },
	{  960000, TEXT("960000")  },
	{  250000, TEXT("250000")  }
};

static const TCHAR *RtlSdrDirSamplingArr[] = {
	TEXT("Disabled"),
	TEXT("I input"),
	TEXT("Q input")
};

static const TCHAR *RtlSdrTunerArr[] = {
	TEXT("Tuner Gain"),
	TEXT("E4000 Gain"),
	TEXT("FC0012 Gain"),
	TEXT("FC0013 Gain"),
	TEXT("FC2580 Gain"),
	TEXT("R820T Gain"),
	TEXT("R828D Gain")
};

// E4000
static const int RtlSdrE4KIFGainArr[] = { 1, 2, 3, 4, 5, 6 };

// E4000
static const TCHAR *RtlSdrE4KIFModeArr[] = {
	TEXT("Default"),
	TEXT("Linearity"),
	TEXT("Sensitivity")
};

// E4000
static const int8_t RtlSdrE4KIFGainModeArr[][6][6] = {
	{	// Default
		{ 60, 0, 0, 0, 90, 90 },	{ 60, 0, 0, 0, 90, 90 },
		{ 60, 0, 0, 0, 90, 90 },	{ 60, 0, 0, 0, 90, 90 },
		{ 60, 0, 0, 0, 90, 90 },	{ 60, 0, 0, 0, 90, 90 }
	}, {	// Linearity
		{ -30,  0, 0,  0,  90, 60 },	{ -30,  0, 0,  0,  90,  90 },
		{ -30,  0, 0, 10, 120, 90 },	{ -30, 30, 0, 10, 120,  90 },
		{ -30, 60, 0,  0, 120, 90 },	{ -30, 60, 0,  0, 120, 120 }
	}, {	// Sensitivity
		{ -30,  0, 0, 20, 30, 30 },	{ -30, 30, 0, 10, 60, 30 },
		{  60,  0, 0,  0, 60, 30 },	{  60,  0, 0, 20, 90, 30 },
		{  60, 30, 0, 10, 90, 60 },	{  60, 60, 0,  0, 90, 90 }
	}
};

// ExtIO Options
static int ExtIOSampleRate = 3;     // id: 00 default: 2.4 Msps
static int ExtIOTunerAGC = 1;       // id: 01 default: Enabled
static int ExtIORTLAGC = 0;         // id: 02 default: Disabled
static int ExtIOFreqCorrection = 0; // id: 03 default: 0
static int ExtIOTunerGain = 0;      // id: 04 default: 0
static int ExtIOBufferSize = 6;     // id: 05 default: 64 KiB
static int ExtIOOffsetTuning = 1;   // id: 06 default: Enabled
static int ExtIODirSampling = 0;    // id: 07 default: Disabled
static uint32_t ExtIODevIdx = 0;    // id: 08 default: 0
// E4000
static int ExtIOE4KIFGain = 0;      // id: 09 default: 0
static int ExtIOE4KIFMode = 0;      // id: 10 default: Default

static int ExtIOInitialized;
static int ExtIODataU8;

// Device
static rtlsdr_dev_t *RtlSdrDev;
static uint32_t RtlSdrDevCount;

// Tuner
static int *RtlSdrTunerGainsArr;
static int RtlSdrTunerGainsCount;
static int RtlSdrTunerGain;
static int RtlSdrPllLocked; // 0 = Locked

// E4000
static int RtlSdrE4KIFGain;

static int ExtIOSetE4KIFGain(HWND hwndDlg, HWND hE4KIFGain, int setpos,
			     int gain, int mode);

// Buffer
static const uint32_t RtlSdrBufSizeArr[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256 }; // In KiB
static uint32_t RtlSdrBufSize;
static short *RtlSdrBufShort;

static HANDLE RtlSdrHandle = INVALID_HANDLE_VALUE;
static void RtlSdrThreadProc(void * /* param */);
static int RtlSdrThreadStart(void);
static int RtlSdrThreadStop(void);
static void RtlSdrCallback(unsigned char *buf, uint32_t len, void * /* ctx */); // RTL-SDR Callback

static void (*ExtIOCallback)(int, int, float, void *); // ExtIO Callback

extern "C" void __stdcall ExtIoSDRInfo(int SDRInfo, int /* additionalValue */,
				       void * /* additionalPtr */)
{
	if (!ExtIOInitialized && SDRInfo == EXTIO_SUPPORTS_U8)
		ExtIODataU8 = 1;
}

extern "C" bool __stdcall InitHW(char *name, char *model, int &hwtype)
{
	RtlSdrDevCount = rtlsdr_get_device_count();
	if (!RtlSdrDevCount) {
		EXTIO_RTL_ERROR("No RTL-SDR devices found");
		return FALSE;
	}
	if (RtlSdrDevCount > EXTIO_RTL_MAXN)
		RtlSdrDevCount = EXTIO_RTL_MAXN;

	strcpy_s(name, 16, EXTIO_RTL_NAME);
	strcpy_s(model, 16, "USB");
	if (!ExtIODataU8)
		hwtype = EXTIO_USBDATA_16;
	else
		hwtype = EXTIO_USBDATA_U8;
	ExtIOInitialized = 1;
	return TRUE;
}

extern "C" int __stdcall GetStatus(void)
{
	return 0;
}

extern "C" bool __stdcall OpenHW(void)
{
	int ret;

	if (ExtIODevIdx >= RtlSdrDevCount)
		ExtIODevIdx = 0;
	ret = rtlsdr_open(&RtlSdrDev, ExtIODevIdx);
	if (ret < 0)
		goto fail;
	ret = rtlsdr_set_sample_rate(RtlSdrDev,
				     RtlSdrSampleRateArr[ExtIOSampleRate].value);
	if (ret < 0)
		goto fail;

	h_dialog = CreateDialog(hInst, MAKEINTRESOURCE(DLG_MAIN),
				NULL, (DLGPROC)MainDlgProc);
	ShowWindow(h_dialog, SW_HIDE);
	return TRUE;

fail:
	EXTIO_RTL_ERROR("Couldn't open device!");
	return FALSE;
}

extern "C" int64_t __stdcall SetHWLO64(int64_t LOfreq)
{
	int ExtIOPllLocked = 1;
	int64_t ret = 0;

	if ((LOfreq < 1) || (LOfreq > (int64_t)0xffffffff)) {
		ret = -1;
	} else {
		ExtIOPllLocked = rtlsdr_set_center_freq(RtlSdrDev,
					     (uint32_t)(LOfreq & 0xffffffff));
		if (ExtIOPllLocked)
			ret = -1;
	}

	if (ExtIOPllLocked != RtlSdrPllLocked) {
		RtlSdrPllLocked = ExtIOPllLocked;
		if (!RtlSdrPllLocked)
			Static_SetText(GetDlgItem(h_dialog, IDC_TUNER_PLL),
				       TEXT("PLL LOCKED"));
		else
			Static_SetText(GetDlgItem(h_dialog, IDC_TUNER_PLL),
				       TEXT("PLL NOT LOCKED"));

		InvalidateRect(h_dialog, NULL, TRUE);
		UpdateWindow(h_dialog);
	}
	if (ret)
		return ret;
	if (LOfreq != GetHWLO64())
		EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_LO);
	return 0;
}

extern "C" long __stdcall SetHWLO(long LOfreq)
{
	int64_t ret = SetHWLO64((int64_t)(unsigned long)LOfreq);

	return (long)(ret & 0xffffffff);
}

extern "C" int __stdcall StartHW64(int64_t LOfreq)
{
	if (!RtlSdrDev)
		return -1;

	if (!ExtIODataU8) {
		RtlSdrBufShort = new(std::nothrow) short[RtlSdrBufSize];
		if (!RtlSdrBufShort) {
			EXTIO_RTL_ERROR("Couldn't allocate buffer!");
			return -1;
		}
	}

	if (RtlSdrThreadStart() < 0) {
		if (!ExtIODataU8) {
			delete[] RtlSdrBufShort;
			RtlSdrBufShort = NULL;
		}
		return -1;
	}

	SetHWLO64(LOfreq);
	EnableWindow(GetDlgItem(h_dialog, IDC_RTL_BUFFER), FALSE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RTL_DEVICE), FALSE);
	return (int)(RtlSdrBufSize / 2);
}

extern "C" int __stdcall StartHW(long LOfreq)
{
	return StartHW64((int64_t)(unsigned long)LOfreq);
}

extern "C" int64_t __stdcall GetHWLO64(void)
{
	int64_t LOfreq;
	static int64_t LastLOfreq = 100000000;

	LOfreq = (int64_t)rtlsdr_get_center_freq(RtlSdrDev);
	if (!LOfreq)
		return LastLOfreq;
	LastLOfreq = LOfreq;
	return LOfreq;
}

extern "C" long __stdcall GetHWLO(void)
{
	int64_t LOfreq = GetHWLO64();

	return (long)(LOfreq & 0x7fffffff);
}

extern "C" long __stdcall GetHWSR(void)
{
	return (long)rtlsdr_get_sample_rate(RtlSdrDev);
}

extern "C" int __stdcall ExtIoGetSrates(int idx, double *samplerate)
{
	if (idx < 0 || idx >= ARRAY_SSIZE(RtlSdrSampleRateArr))
		return -1;

	*samplerate = (double)(RtlSdrSampleRateArr[idx].value * 1.0);
	return 0;
}

extern "C" int __stdcall ExtIoGetActualSrateIdx(void)
{
	return ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_RTL_SAMPLE_RATE));
}

extern "C" int __stdcall ExtIoSetSrate(int idx)
{
	if (idx < 0 || idx >= ARRAY_SSIZE(RtlSdrSampleRateArr))
		return -1;

	rtlsdr_set_sample_rate(RtlSdrDev, RtlSdrSampleRateArr[idx].value);
	ComboBox_SetCurSel(GetDlgItem(h_dialog, IDC_RTL_SAMPLE_RATE), idx);
	EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
	return 0;
}

extern "C" int __stdcall GetAttenuators(int idx, float *attenuation)
{
	if (idx < 0 || idx >= RtlSdrTunerGainsCount)
		return -1;

	*attenuation = (float)(RtlSdrTunerGainsArr[idx] / 10.0);
	return 0;
}

extern "C" int __stdcall GetActualAttIdx(void)
{
	for (int i = 0; i < RtlSdrTunerGainsCount; i++) {
		if (RtlSdrTunerGain == RtlSdrTunerGainsArr[i])
			return i;
	}
	return -1;
}

extern "C" int __stdcall SetAttenuator(int idx)
{
	int pos;

	if (idx < 0 || idx >= RtlSdrTunerGainsCount)
		return -1;

	pos = RtlSdrTunerGainsArr[idx];
	SendMessage(GetDlgItem(h_dialog, IDC_TUNER_GAIN_CTL), TBM_SETPOS,
		   (WPARAM)TRUE, (LPARAM)-pos);
	if (Button_GetCheck(GetDlgItem(h_dialog, IDC_TUNER_AGC)) == BST_UNCHECKED) {
		TCHAR tunergain[256];

		_stprintf_s(tunergain, 256, TEXT("%2.1f dB"), (float)(pos / 10.0));
		Static_SetText(GetDlgItem(h_dialog, IDC_TUNER_GAIN), tunergain);
		if (pos != RtlSdrTunerGain) {
			rtlsdr_set_tuner_gain(RtlSdrDev, pos);
			ExtIOSetE4KIFGain(h_dialog, GetDlgItem(h_dialog, IDC_E4K_IF_GAIN_CTL), 0,
				(int)-SendMessage(GetDlgItem(h_dialog, IDC_E4K_IF_GAIN_CTL),
						  TBM_GETPOS, (WPARAM)0, (LPARAM)0),
				ComboBox_GetCurSel(GetDlgItem
				(h_dialog, IDC_E4K_IF_GAIN_MODE)));
		}
	}
	RtlSdrTunerGain = pos;
	return 0;
}

extern "C" int __stdcall ExtIoGetMGCs(int idx, float *gain)
{
	if (idx < 0 || idx >= ARRAY_SSIZE(RtlSdrE4KIFGainArr))
		return -1;

	*gain = (float)(RtlSdrE4KIFGainArr[idx] * 5.0);
	return 0;
}

extern "C" int __stdcall ExtIoGetActualMgcIdx(void)
{
	for (int i = 0; i < ARRAY_SSIZE(RtlSdrE4KIFGainArr); i++) {
		if (RtlSdrE4KIFGain == RtlSdrE4KIFGainArr[i])
			return i;
	}
	return -1;
}

extern "C" int __stdcall ExtIoSetMGC(int idx)
{
	if (idx < 0 || idx >= ARRAY_SSIZE(RtlSdrE4KIFGainArr))
		return -1;

	ExtIOSetE4KIFGain(h_dialog, GetDlgItem(h_dialog, IDC_E4K_IF_GAIN_CTL), 1, idx,
			  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_E4K_IF_GAIN_MODE)));
	RtlSdrE4KIFGain = RtlSdrE4KIFGainArr[idx];
	return 0;
}

extern "C" int __stdcall ExtIoGetSetting(int idx, char *description, char *value)
{
	switch (idx) {
	case 0:
		_snprintf(description, 1024, "%s", "SampleRate");
		_snprintf(value, 1024, "%d",
			  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_RTL_SAMPLE_RATE)));
		return 0;
	case 1:
		_snprintf(description, 1024, "%s", "TunerAGC");
		_snprintf(value, 1024, "%d",
			  Button_GetCheck(GetDlgItem(h_dialog, IDC_TUNER_AGC)) ==
			  BST_CHECKED ? 1 : 0);
		return 0;
	case 2:
		_snprintf(description, 1024, "%s", "RTLAGC");
		_snprintf(value, 1024, "%d",
			  Button_GetCheck(GetDlgItem(h_dialog, IDC_RTL_AGC)) ==
			  BST_CHECKED ? 1 : 0);
		return 0;
	case 3: {
		int oldppm;
		TCHAR ppm[256];

		Edit_GetText(GetDlgItem(h_dialog, IDC_RTL_PPM), ppm, 256);
		oldppm = ppm_validate(_ttoi(ppm));
		_snprintf(description, 1024, "%s", "FreqCorrection");
		_snprintf(value, 1024, "%d", oldppm);
		return 0;
	}
	case 4: {
		int pos = (int)-SendMessage(GetDlgItem(h_dialog, IDC_TUNER_GAIN_CTL),
					    TBM_GETPOS, (WPARAM)0, (LPARAM)0);

		_snprintf(description, 1024, "%s", "TunerGain");
		_snprintf(value, 1024, "%d", pos);
		return 0;
	}
	case 5:
		_snprintf(description, 1024, "%s", "BufferSize");
		_snprintf(value, 1024, "%d",
			  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_RTL_BUFFER)));
		return 0;
	case 6:
		_snprintf(description, 1024, "%s", "OffsetTuning");
		_snprintf(value, 1024, "%d",
			  Button_GetCheck(GetDlgItem(h_dialog, IDC_TUNER_OFF_TUNING)) ==
			  BST_CHECKED ? 1 : 0);
		return 0;
	case 7:
		_snprintf(description, 1024, "%s", "DirectSampling");
		_snprintf(value, 1024, "%d",
			  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_RTL_DIR_SAMPLING)));
		return 0;
	case 8:
		_snprintf(description, 1024, "%s", "Device");
		_snprintf(value, 1024, "%d",
			  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_RTL_DEVICE)));
		return 0;
	case 9: {
		int pos = (int)-SendMessage(GetDlgItem(h_dialog, IDC_E4K_IF_GAIN_CTL),
					    TBM_GETPOS, (WPARAM)0, (LPARAM)0);

		_snprintf(description, 1024, "%s", "E4000IFGain");
		_snprintf(value, 1024, "%d", pos);
		return 0;
	}
	case 10:
		_snprintf(description, 1024, "%s", "E4000IFGainMode");
		_snprintf(value, 1024, "%d",
			  ComboBox_GetCurSel(GetDlgItem(h_dialog, IDC_E4K_IF_GAIN_MODE)));
		return 0;
	}
	return -1;
}

extern "C" void __stdcall ExtIoSetSetting(int idx, const char *value)
{
	int ExtIOVal;

	switch (idx) {
	case 0:
		ExtIOVal = atoi(value);
		if (ExtIOVal >= 0 && ExtIOVal < ARRAY_SSIZE(RtlSdrSampleRateArr))
			ExtIOSampleRate = ExtIOVal;
		break;
	case 1:
		ExtIOVal = atoi(value);
		ExtIOTunerAGC = ExtIOVal ? 1 : 0;
		break;
	case 2:
		ExtIOVal = atoi(value);
		ExtIORTLAGC = ExtIOVal ? 1 : 0;
		break;
	case 3:
		ExtIOVal = ppm_validate(atoi(value));
		ExtIOFreqCorrection = ExtIOVal;
		break;
	case 4:
		ExtIOVal = atoi(value);
		ExtIOTunerGain = ExtIOVal;
		break;
	case 5:
		ExtIOVal = atoi(value);
		if (ExtIOVal >= 0 && ExtIOVal < ARRAY_SSIZE(RtlSdrBufSizeArr))
			ExtIOBufferSize = ExtIOVal;
		break;
	case 6:
		ExtIOVal = atoi(value);
		ExtIOOffsetTuning = ExtIOVal ? 1 : 0;
		break;
	case 7:
		ExtIOVal = atoi(value);
		if (ExtIOVal >= 0 && ExtIOVal < ARRAY_SSIZE(RtlSdrDirSamplingArr))
			ExtIODirSampling = ExtIOVal;
		break;
	case 8:
		ExtIOVal = atoi(value);
		ExtIODevIdx = ExtIOVal;
		break;
	case 9:
		ExtIOVal = atoi(value);
		if (ExtIOVal >= 0 && ExtIOVal < ARRAY_SSIZE(RtlSdrE4KIFGainArr))
			ExtIOE4KIFGain = ExtIOVal;
		break;
	case 10:
		ExtIOVal = atoi(value);
		if (ExtIOVal >= 0 && ExtIOVal < ARRAY_SSIZE(RtlSdrE4KIFModeArr))
			ExtIOE4KIFMode = ExtIOVal;
		break;
	}
}

extern "C" void __stdcall StopHW(void)
{
	RtlSdrThreadStop();
	if (!ExtIODataU8) {
		delete[] RtlSdrBufShort;
		RtlSdrBufShort = NULL;
	}
	EnableWindow(GetDlgItem(h_dialog, IDC_RTL_BUFFER), TRUE);
	EnableWindow(GetDlgItem(h_dialog, IDC_RTL_DEVICE), TRUE);
}

extern "C" void __stdcall CloseHW(void)
{
	rtlsdr_close(RtlSdrDev);
	RtlSdrDev = NULL;
	if (h_dialog)
		DestroyWindow(h_dialog);
}

extern "C" void __stdcall ShowGUI(void)
{
	ShowWindow(h_dialog, SW_SHOW);
	SetForegroundWindow(h_dialog);
}

extern "C" void __stdcall HideGUI(void)
{
	ShowWindow(h_dialog, SW_HIDE);
}

extern "C" void __stdcall SwitchGUI(void)
{
	if (IsWindowVisible(h_dialog))
		ShowWindow(h_dialog, SW_HIDE);
	else
		ShowWindow(h_dialog, SW_SHOW);
}

extern "C" void __stdcall SetCallback(void (*ParentCallback)(int, int, float, void *))
{
	ExtIOCallback = ParentCallback;
}

static void RtlSdrCallback(unsigned char *buf, uint32_t len, void * /* ctx */)
{
	if (!ExtIODataU8 && RtlSdrBufShort && buf && len == RtlSdrBufSize) {
		short *short_buf = RtlSdrBufShort;
		unsigned char *char_buf = buf;

		for (uint32_t i = 0; i < len; i++)
			*short_buf++ = ((short)(*char_buf++)) - 128;

		ExtIOCallback(RtlSdrBufSize, 0, 0, (void *)RtlSdrBufShort);
	} else if (ExtIODataU8 && buf && len == RtlSdrBufSize) {
		ExtIOCallback(RtlSdrBufSize, 0, 0, (void *)buf);
	}
}

static int RtlSdrThreadStart(void)
{
	// Exit if already running
	if (RtlSdrHandle != INVALID_HANDLE_VALUE)
		return -1;

	// Reset endpoint
	if (rtlsdr_reset_buffer(RtlSdrDev) < 0)
		return -1;

	RtlSdrHandle = (HANDLE)_beginthread(RtlSdrThreadProc, 0, NULL);
	if (RtlSdrHandle == INVALID_HANDLE_VALUE)
		return -1;

	SetThreadPriority(RtlSdrHandle, THREAD_PRIORITY_TIME_CRITICAL);
	return 0;
}

static int RtlSdrThreadStop(void)
{
	if (RtlSdrHandle == INVALID_HANDLE_VALUE)
		return -1;
	rtlsdr_cancel_async(RtlSdrDev);
	WaitForSingleObject(RtlSdrHandle, INFINITE);
	CloseHandle(RtlSdrHandle);
	RtlSdrHandle = INVALID_HANDLE_VALUE;
	return 0;
}

static void RtlSdrThreadProc(void * /* param */)
{
	// Blocks until rtlsdr_cancel_async() is called
	// Use default number of buffers
	rtlsdr_read_async(RtlSdrDev, (rtlsdr_read_async_cb_t)&RtlSdrCallback,
			  NULL, 0, RtlSdrBufSize);
	_endthread();
}

static void ExtIOTunerGainsConf(HWND hwndDlg, HWND hGain)
{
	int TunerIdx, TunerGainIdx = 0;

	TunerIdx = (int)rtlsdr_get_tuner_type(RtlSdrDev);
	if (TunerIdx < 0 || TunerIdx >= ARRAY_SSIZE(RtlSdrTunerArr))
		TunerIdx = 0;
	Static_SetText(GetDlgItem(hwndDlg, IDC_TUNER_GAIN_NAME),
		       RtlSdrTunerArr[TunerIdx]);

	RtlSdrTunerGainsCount = rtlsdr_get_tuner_gains(RtlSdrDev, RtlSdrTunerGainsArr);
	if (RtlSdrTunerGainsCount < 1)
		RtlSdrTunerGainsCount = 1;
	else if (RtlSdrTunerGainsCount > RTLSDR_MAXTUNERGAINS)
		RtlSdrTunerGainsCount = RTLSDR_MAXTUNERGAINS;
	SendMessage(hGain, TBM_SETRANGEMIN, (WPARAM)TRUE,
		   (LPARAM)-RtlSdrTunerGainsArr[RtlSdrTunerGainsCount - 1]);
	SendMessage(hGain, TBM_SETRANGEMAX, (WPARAM)TRUE,
		   (LPARAM)-RtlSdrTunerGainsArr[0]);
	SendMessage(hGain, TBM_CLEARTICS, (WPARAM)FALSE, (LPARAM)0);
	for (int i = 0; i < RtlSdrTunerGainsCount; i++) {
		SendMessage(hGain, TBM_SETTIC, (WPARAM)0,
			   (LPARAM)-RtlSdrTunerGainsArr[i]);
		if (ExtIOTunerGain == RtlSdrTunerGainsArr[i])
			TunerGainIdx = i;
	}
	SendMessage(hGain, TBM_SETPOS, (WPARAM)TRUE,
		   (LPARAM)-RtlSdrTunerGainsArr[TunerGainIdx]);
	RtlSdrTunerGain = RtlSdrTunerGainsArr[TunerGainIdx];
}

// E4000
static int ExtIOSetE4KIFGain(HWND hwndDlg, HWND hE4KIFGain, int setpos,
			     int gain, int mode)
{
	if (mode < E4K_GAIN_MODE_DEFAULT)
		mode = E4K_GAIN_MODE_DEFAULT;
	else if (mode > E4K_GAIN_MODE_SENSITIVITY)
		mode = E4K_GAIN_MODE_SENSITIVITY;

	if (gain < 0)
		gain = 0;
	else if (gain >= ARRAY_SSIZE(RtlSdrE4KIFGainArr))
		gain = ARRAY_SSIZE(RtlSdrE4KIFGainArr) - 1;

	if (setpos)
		SendMessage(hE4KIFGain, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)-gain);
	if (!mode) {
		EnableWindow(hE4KIFGain, FALSE);
		Static_SetText(GetDlgItem(hwndDlg, IDC_E4K_IF_GAIN), TEXT("Default"));
	} else {
		TCHAR e4k_ifgain[256];

		EnableWindow(hE4KIFGain, TRUE);
		_stprintf_s(e4k_ifgain, 256, TEXT("%2.1f dB"),
			   (float)(RtlSdrE4KIFGainArr[gain] * 5.0));
		Static_SetText(GetDlgItem(hwndDlg, IDC_E4K_IF_GAIN), e4k_ifgain);
	}

	for (int i = 0; i < 6; i++)
		rtlsdr_set_tuner_if_gain(RtlSdrDev, i + 1,
					 RtlSdrE4KIFGainModeArr[mode][gain][i]);
	return 0;
}

static INT_PTR CALLBACK MainDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static HWND hGain;
	static HWND hE4KIFGain;
	static HBRUSH BRUSH_RED = CreateSolidBrush(RGB(255, 0, 0));
	static HBRUSH BRUSH_GREEN = CreateSolidBrush(RGB(0, 255, 0));

	switch (uMsg) {
	case WM_INITDIALOG: {
		TCHAR ppm[256];

		for (int i = 0; i < ARRAY_SSIZE(RtlSdrDirSamplingArr); i++)
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RTL_DIR_SAMPLING),
					   RtlSdrDirSamplingArr[i]);

		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_RTL_DIR_SAMPLING),
				   ExtIODirSampling);
		rtlsdr_set_direct_sampling(RtlSdrDev, ExtIODirSampling);

		for (int i = 0; i < ARRAY_SSIZE(RtlSdrE4KIFModeArr); i++)
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_E4K_IF_GAIN_MODE),
					   RtlSdrE4KIFModeArr[i]);

		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_E4K_IF_GAIN_MODE),
				   ExtIOE4KIFMode);

		Button_SetCheck(GetDlgItem(hwndDlg, IDC_TUNER_AGC),
				ExtIOTunerAGC ? BST_CHECKED : BST_UNCHECKED);
		rtlsdr_set_tuner_gain_mode(RtlSdrDev, ExtIOTunerAGC ? 0 : 1);

		Button_SetCheck(GetDlgItem(hwndDlg, IDC_RTL_AGC),
				ExtIORTLAGC ? BST_CHECKED : BST_UNCHECKED);
		rtlsdr_set_agc_mode(RtlSdrDev, ExtIORTLAGC ? 1 : 0);

		Button_SetCheck(GetDlgItem(hwndDlg, IDC_TUNER_OFF_TUNING),
				ExtIOOffsetTuning ? BST_CHECKED : BST_UNCHECKED);
		rtlsdr_set_offset_tuning(RtlSdrDev, ExtIOOffsetTuning ? 1 : 0);

		SendMessage(GetDlgItem(hwndDlg, IDC_RTL_PPM_CTL),
			    UDM_SETRANGE, (WPARAM)TRUE,
			    MAKELPARAM(RTLSDR_MAXPPM, RTLSDR_MINPPM));
		SendMessage(GetDlgItem(hwndDlg, IDC_RTL_PPM),
			    EM_SETLIMITTEXT, (WPARAM)4, (LPARAM)0);
		ExtIOFreqCorrection = ppm_validate(ExtIOFreqCorrection);
		_stprintf_s(ppm, 256, TEXT("%d"), ExtIOFreqCorrection);
		Edit_SetText(GetDlgItem(hwndDlg, IDC_RTL_PPM), ppm);
		rtlsdr_set_freq_correction(RtlSdrDev, ExtIOFreqCorrection);

		for (uint32_t i = 0; i < RtlSdrDevCount; i++) {
			TCHAR devid[256];

			_stprintf_s(devid, 256, TEXT("%d: %S"), i,
				    rtlsdr_get_device_name(i));
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RTL_DEVICE), devid);
		}
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_RTL_DEVICE), ExtIODevIdx);

		for (int i = 0; i < ARRAY_SSIZE(RtlSdrSampleRateArr); i++)
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RTL_SAMPLE_RATE),
					   RtlSdrSampleRateArr[i].name);
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_RTL_SAMPLE_RATE), ExtIOSampleRate);

		for (int i = 0; i < ARRAY_SSIZE(RtlSdrBufSizeArr); i++) {
			TCHAR bufsize[256];

			_stprintf_s(bufsize, 256, TEXT("%d "), RtlSdrBufSizeArr[i]);
			ComboBox_AddString(GetDlgItem(hwndDlg, IDC_RTL_BUFFER), bufsize);
		}
		ComboBox_SetCurSel(GetDlgItem(hwndDlg, IDC_RTL_BUFFER), ExtIOBufferSize);
		RtlSdrBufSize = RtlSdrBufSizeArr[ExtIOBufferSize] * 1024;

		hGain = GetDlgItem(hwndDlg, IDC_TUNER_GAIN_CTL);
		RtlSdrTunerGainsArr = new int[RTLSDR_MAXTUNERGAINS];
		for (int i = 0; i < RTLSDR_MAXTUNERGAINS; i++)
			RtlSdrTunerGainsArr[i] = 0;
		ExtIOTunerGainsConf(hwndDlg, hGain);

		if (ExtIOTunerAGC) {
			EnableWindow(hGain, FALSE);
			Static_SetText(GetDlgItem(hwndDlg, IDC_TUNER_GAIN), TEXT("AGC"));
		} else {
			int pos = (int)-SendMessage(hGain, TBM_GETPOS, (WPARAM)0, (LPARAM)0);
			TCHAR tunergain[256];

			_stprintf_s(tunergain, 256, TEXT("%2.1f dB"), (float)(pos / 10.0));
			Static_SetText(GetDlgItem(hwndDlg, IDC_TUNER_GAIN), tunergain);
			rtlsdr_set_tuner_gain(RtlSdrDev, pos);
		}

		hE4KIFGain = GetDlgItem(hwndDlg, IDC_E4K_IF_GAIN_CTL);
		SendMessage(hE4KIFGain, TBM_SETRANGE, (WPARAM)TRUE,
			    MAKELPARAM(-(ARRAY_SSIZE(RtlSdrE4KIFGainArr) - 1), 0));
		SendMessage(hE4KIFGain, TBM_CLEARTICS, (WPARAM)FALSE, (LPARAM)0);
		for (int i = 0; i < ARRAY_SSIZE(RtlSdrE4KIFGainArr); i++)
			SendMessage(hE4KIFGain, TBM_SETTIC, (WPARAM)0, (LPARAM)-i);
		ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 1, ExtIOE4KIFGain, ExtIOE4KIFMode);
		RtlSdrE4KIFGain = RtlSdrE4KIFGainArr[ExtIOE4KIFGain];
		return TRUE;
	}
	case WM_COMMAND:
		switch (GET_WM_COMMAND_ID(wParam, lParam)) {
		case IDC_RTL_PPM:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == EN_CHANGE) {
				int newppm;
				TCHAR ppm[256];

				Edit_GetText((HWND)lParam, ppm, 256);
				newppm = ppm_validate(_ttoi(ppm));
				if (!rtlsdr_set_freq_correction(RtlSdrDev, newppm))
					EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_LO);
			}
			return TRUE;
		case IDC_RTL_AGC:
			if (Button_GetCheck
				(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED)
				rtlsdr_set_agc_mode(RtlSdrDev, 1);
			else
				rtlsdr_set_agc_mode(RtlSdrDev, 0);
			return TRUE;
		case IDC_TUNER_OFF_TUNING:
			if (Button_GetCheck
				(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED)
				rtlsdr_set_offset_tuning(RtlSdrDev, 1);
			else
				rtlsdr_set_offset_tuning(RtlSdrDev, 0);
			return TRUE;
		case IDC_TUNER_AGC:
			if (Button_GetCheck
				(GET_WM_COMMAND_HWND(wParam, lParam)) == BST_CHECKED) {
				rtlsdr_set_tuner_gain_mode(RtlSdrDev, 0);
				EnableWindow(hGain, FALSE);
				Static_SetText(GetDlgItem
					      (hwndDlg, IDC_TUNER_GAIN), TEXT("AGC"));
			} else {
				int pos = (int)-SendMessage(hGain, TBM_GETPOS,
							   (WPARAM)0, (LPARAM)0);
				TCHAR tunergain[256];

				rtlsdr_set_tuner_gain_mode(RtlSdrDev, 1);
				EnableWindow(hGain, TRUE);
				_stprintf_s(tunergain, 256, TEXT("%2.1f dB"),
					   (float)(pos / 10.0));
				Static_SetText(GetDlgItem
					      (hwndDlg, IDC_TUNER_GAIN), tunergain);
				rtlsdr_set_tuner_gain(RtlSdrDev, pos);
			}

			ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 0,
				(int)-SendMessage(hE4KIFGain, TBM_GETPOS,
						 (WPARAM)0, (LPARAM)0),
				ComboBox_GetCurSel(GetDlgItem
				(hwndDlg, IDC_E4K_IF_GAIN_MODE)));
			return TRUE;
		case IDC_RTL_SAMPLE_RATE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				rtlsdr_set_sample_rate(RtlSdrDev,
					RtlSdrSampleRateArr[ComboBox_GetCurSel
					(GET_WM_COMMAND_HWND(wParam, lParam))].value);
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			}
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_EDITUPDATE) {
				uint32_t newsrate;
				TCHAR srate[256];

				ComboBox_GetText((HWND)lParam, srate, 256);
				newsrate = srate_validate(_ttoi(srate));
				if (!rtlsdr_set_sample_rate(RtlSdrDev, newsrate)) {
					_itot(newsrate, srate, 10);
					ComboBox_SetText((HWND)lParam, srate);
					EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
				}
			}
			return TRUE;
		case IDC_RTL_BUFFER:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				RtlSdrBufSize = RtlSdrBufSizeArr[ComboBox_GetCurSel
						(GET_WM_COMMAND_HWND(wParam, lParam))] * 1024;
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);
			}
			return TRUE;
		case IDC_RTL_DIR_SAMPLING:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				rtlsdr_set_direct_sampling(RtlSdrDev,
				    ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam)));
				if (ComboBox_GetCurSel(GET_WM_COMMAND_HWND(wParam, lParam)) == 0) {
					if (Button_GetCheck(GetDlgItem
						(hwndDlg, IDC_TUNER_OFF_TUNING)) == BST_CHECKED)
						rtlsdr_set_offset_tuning(RtlSdrDev, 1);
					else
						rtlsdr_set_offset_tuning(RtlSdrDev, 0);

					if (Button_GetCheck(GetDlgItem
						(hwndDlg, IDC_TUNER_AGC)) == BST_CHECKED) {
						rtlsdr_set_tuner_gain_mode(RtlSdrDev, 0);
					} else {
						int pos = (int)-SendMessage(hGain,
								TBM_GETPOS, (WPARAM)0, (LPARAM)0);

						rtlsdr_set_tuner_gain_mode(RtlSdrDev, 1);
						rtlsdr_set_tuner_gain(RtlSdrDev, pos);
					}

					ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 0,
						(int)-SendMessage(hE4KIFGain, TBM_GETPOS,
								 (WPARAM)0, (LPARAM)0),
						ComboBox_GetCurSel(GetDlgItem
						(hwndDlg, IDC_E4K_IF_GAIN_MODE)));
				}
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_LO);
			}
			return TRUE;
		case IDC_RTL_DEVICE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE) {
				int currppm;
				int curr_e4k_ifgain;
				uint32_t currsrate;
				TCHAR ppm[256];
				TCHAR srate[256];

				Edit_GetText(GetDlgItem
						(hwndDlg, IDC_RTL_PPM), ppm, 256);
				currppm = ppm_validate(_ttoi(ppm));
				ComboBox_GetText(GetDlgItem
						(hwndDlg, IDC_RTL_SAMPLE_RATE), srate, 256);
				currsrate = srate_validate(_ttoi(srate));
				curr_e4k_ifgain = (int)-SendMessage(hE4KIFGain,
							TBM_GETPOS, (WPARAM)0, (LPARAM)0);

				rtlsdr_close(RtlSdrDev);
				RtlSdrDev = NULL;
				if (rtlsdr_open(&RtlSdrDev, (uint32_t)ComboBox_GetCurSel
						(GET_WM_COMMAND_HWND(wParam, lParam))) < 0) {
					EXTIO_RTL_ERROR("Couldn't open device!");
					return TRUE;
				}
				rtlsdr_set_sample_rate(RtlSdrDev, currsrate);
				rtlsdr_set_freq_correction(RtlSdrDev, currppm);
				rtlsdr_set_direct_sampling(RtlSdrDev, ComboBox_GetCurSel
					(GetDlgItem(hwndDlg, IDC_RTL_DIR_SAMPLING)));
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_SR);

				if (Button_GetCheck(GetDlgItem
					(hwndDlg, IDC_TUNER_AGC)) == BST_CHECKED)
					rtlsdr_set_tuner_gain_mode(RtlSdrDev, 0);
				else
					rtlsdr_set_tuner_gain_mode(RtlSdrDev, 1);

				if (Button_GetCheck(GetDlgItem
					(hwndDlg, IDC_RTL_AGC)) == BST_CHECKED)
					rtlsdr_set_agc_mode(RtlSdrDev, 1);
				else
					rtlsdr_set_agc_mode(RtlSdrDev, 0);

				if (Button_GetCheck(GetDlgItem
					(hwndDlg, IDC_TUNER_OFF_TUNING)) == BST_CHECKED)
					rtlsdr_set_offset_tuning(RtlSdrDev, 1);
				else
					rtlsdr_set_offset_tuning(RtlSdrDev, 0);

				ExtIOTunerGainsConf(hwndDlg, hGain);
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_RF_IF);

				if (Button_GetCheck(GetDlgItem
					(hwndDlg, IDC_TUNER_AGC)) == BST_CHECKED) {
					Static_SetText(GetDlgItem
						      (hwndDlg, IDC_TUNER_GAIN), TEXT("AGC"));
				} else {
					int pos = (int)-SendMessage(hGain, TBM_GETPOS,
								   (WPARAM)0, (LPARAM)0);
					TCHAR tunergain[256];

					_stprintf_s(tunergain, 256, TEXT("%2.1f dB"),
						   (float)(pos / 10.0));
					Static_SetText(GetDlgItem
						      (hwndDlg, IDC_TUNER_GAIN), tunergain);
					rtlsdr_set_tuner_gain(RtlSdrDev, pos);
				}

				ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 1, curr_e4k_ifgain,
						  ComboBox_GetCurSel(GetDlgItem
						  (hwndDlg, IDC_E4K_IF_GAIN_MODE)));
				RtlSdrE4KIFGain = RtlSdrE4KIFGainArr[curr_e4k_ifgain];
			}
			return TRUE;
		case IDC_E4K_IF_GAIN_MODE:
			if (GET_WM_COMMAND_CMD(wParam, lParam) == CBN_SELCHANGE)
				ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 0,
					(int)-SendMessage(hE4KIFGain, TBM_GETPOS,
							 (WPARAM)0, (LPARAM)0),
					ComboBox_GetCurSel(GET_WM_COMMAND_HWND
					(wParam, lParam)));
			return TRUE;
		}
		break;
	case WM_VSCROLL:
		if ((HWND)lParam == hGain) {
			int pos = (int)-SendMessage(hGain, TBM_GETPOS,
						   (WPARAM)0, (LPARAM)0);
			TCHAR tunergain[256];

			for (int i = 0; i < RtlSdrTunerGainsCount - 1; i++) {
				if (pos > RtlSdrTunerGainsArr[i] &&
				    pos < RtlSdrTunerGainsArr[i + 1]) {
					if (((pos - RtlSdrTunerGainsArr[i]) <
					     (RtlSdrTunerGainsArr[i + 1] - pos) &&
					     (LOWORD(wParam) != TB_LINEUP)) ||
					    (LOWORD(wParam) == TB_LINEDOWN))
						pos = RtlSdrTunerGainsArr[i];
					else
						pos = RtlSdrTunerGainsArr[i + 1];
				}
			}

			SendMessage(hGain, TBM_SETPOS, (WPARAM)TRUE, (LPARAM)-pos);
			_stprintf_s(tunergain, 256, TEXT("%2.1f dB"), (float)(pos / 10.0));
			Static_SetText(GetDlgItem(hwndDlg, IDC_TUNER_GAIN), tunergain);

			if (pos != RtlSdrTunerGain) {
				RtlSdrTunerGain = pos;
				rtlsdr_set_tuner_gain(RtlSdrDev, pos);
				ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 0,
					(int)-SendMessage(hE4KIFGain, TBM_GETPOS,
							 (WPARAM)0, (LPARAM)0),
					ComboBox_GetCurSel(GetDlgItem
					(hwndDlg, IDC_E4K_IF_GAIN_MODE)));
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_ATT);
			}
			return TRUE;
		}
		if ((HWND)lParam == hE4KIFGain) {
			int pos = (int)-SendMessage(hE4KIFGain, TBM_GETPOS,
						   (WPARAM)0, (LPARAM)0);

			if (RtlSdrE4KIFGainArr[pos] != RtlSdrE4KIFGain) {
				ExtIOSetE4KIFGain(hwndDlg, hE4KIFGain, 0, pos,
						  ComboBox_GetCurSel(GetDlgItem
						  (hwndDlg, IDC_E4K_IF_GAIN_MODE)));
				RtlSdrE4KIFGain = RtlSdrE4KIFGainArr[pos];
				EXTIO_SET_STATUS(ExtIOCallback, EXTIO_CHANGED_ATT);
			}
			return TRUE;
		}
		if ((HWND)lParam == GetDlgItem(hwndDlg, IDC_RTL_PPM_CTL))
			return TRUE;
		break;
	case WM_CLOSE:
		ShowWindow(h_dialog, SW_HIDE);
		return TRUE;
	case WM_DESTROY:
		delete[] RtlSdrTunerGainsArr;
		RtlSdrTunerGainsArr = NULL;
		h_dialog = NULL;
		return TRUE;
	case WM_CTLCOLORSTATIC:
		if (IDC_TUNER_PLL == GetDlgCtrlID((HWND)lParam)) {
			HDC hdc = (HDC)wParam;

			if (!RtlSdrPllLocked) {
				SetBkColor(hdc, RGB(0, 255, 0));
				return (INT_PTR)BRUSH_GREEN;
			} else {
				SetBkColor(hdc, RGB(255, 0, 0));
				return (INT_PTR)BRUSH_RED;
			}
		}
		break;
	}
	return FALSE;
}
