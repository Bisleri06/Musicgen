#include <iostream>
#include <cmath>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <condition_variable>
#include<algorithm>
using namespace std;

#include <Windows.h>

const double PI = 2.0 * acos(0.0);

template<class T>
class olcNoiseMaker
{
public:
	olcNoiseMaker(string sOutputDevice, unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
	{
		Create(sOutputDevice, nSampleRate, nChannels, nBlocks, nBlockSamples);
	}

	~olcNoiseMaker()
	{
		Destroy();
	}

	bool Create(string sOutputDevice, unsigned int nSampleRate = 44100, unsigned int nChannels = 1, unsigned int nBlocks = 8, unsigned int nBlockSamples = 512)
	{
		m_bReady = false;
		m_nSampleRate = nSampleRate;
		m_nChannels = nChannels;
		m_nBlockCount = nBlocks;
		m_nBlockSamples = nBlockSamples;
		m_nBlockFree = m_nBlockCount;
		m_nBlockCurrent = 0;
		m_pBlockMemory = nullptr;
		m_pWaveHeaders = nullptr;

		m_userFunction = nullptr;

		// Validate device
		vector<string> devices = Enumerate();
		auto d = find(devices.begin(), devices.end(), sOutputDevice);
		if (d != devices.end())
		{
			// Device is available
			int nDeviceID = distance(devices.begin(), d);
			WAVEFORMATEX waveFormat;
			waveFormat.wFormatTag = WAVE_FORMAT_PCM;
			waveFormat.nSamplesPerSec = m_nSampleRate;
			waveFormat.wBitsPerSample = sizeof(T) * 8;
			waveFormat.nChannels = m_nChannels;
			waveFormat.nBlockAlign = (waveFormat.wBitsPerSample / 8) * waveFormat.nChannels;
			waveFormat.nAvgBytesPerSec = waveFormat.nSamplesPerSec * waveFormat.nBlockAlign;
			waveFormat.cbSize = 0;

			// Open Device if valid
			if (waveOutOpen(&m_hwDevice, nDeviceID, &waveFormat, (DWORD_PTR)waveOutProcWrap, (DWORD_PTR)this, CALLBACK_FUNCTION) != S_OK)
				return Destroy();
		}

		// Allocate Wave|Block Memory
		m_pBlockMemory = new T[m_nBlockCount * m_nBlockSamples];
		if (m_pBlockMemory == nullptr)
			return Destroy();
		ZeroMemory(m_pBlockMemory, sizeof(T) * m_nBlockCount * m_nBlockSamples);

		m_pWaveHeaders = new WAVEHDR[m_nBlockCount];
		if (m_pWaveHeaders == nullptr)
			return Destroy();
		ZeroMemory(m_pWaveHeaders, sizeof(WAVEHDR) * m_nBlockCount);

		// Link headers to block memory
		for (unsigned int n = 0; n < m_nBlockCount; n++)
		{
			m_pWaveHeaders[n].dwBufferLength = m_nBlockSamples * sizeof(T);
			m_pWaveHeaders[n].lpData = (LPSTR)(m_pBlockMemory + (n * m_nBlockSamples));
		}

		m_bReady = true;

		m_thread = thread(&olcNoiseMaker::MainThread, this);

		// Start the ball rolling
		unique_lock<mutex> lm(m_muxBlockNotZero);
		m_cvBlockNotZero.notify_one();

		return true;
	}

	bool Destroy()
	{
		return false;
	}

	void Stop()
	{
		m_bReady = false;
		m_thread.join();
	}

	// Override to process current sample
	virtual double UserProcess(double dTime)
	{
		return 0.0;
	}

	double GetTime()
	{
		return m_dGlobalTime;
	}

	

public:
	static vector<string> Enumerate()
	{
		int nDeviceCount = waveOutGetNumDevs();
		vector<string> sDevices;
		WAVEOUTCAPS woc;
		for (int n = 0; n < nDeviceCount; n++)
			if (waveOutGetDevCaps(n, &woc, sizeof(WAVEOUTCAPS)) == S_OK)
				sDevices.push_back(woc.szPname);
		return sDevices;
	}

	void SetUserFunction(double(*func)(double))
	{
		m_userFunction = func;
	}

	double clip(double dSample, double dMax)
	{
		if (dSample >= 0.0)
			return fmin(dSample, dMax);
		else
			return fmax(dSample, -dMax);
	}


private:
	double(*m_userFunction)(double);

	unsigned int m_nSampleRate;
	unsigned int m_nChannels;
	unsigned int m_nBlockCount;
	unsigned int m_nBlockSamples;
	unsigned int m_nBlockCurrent;

	T* m_pBlockMemory;
	WAVEHDR *m_pWaveHeaders;
	HWAVEOUT m_hwDevice;

	thread m_thread;
	atomic<bool> m_bReady;
	atomic<unsigned int> m_nBlockFree;
	condition_variable m_cvBlockNotZero;
	mutex m_muxBlockNotZero;

	atomic<double> m_dGlobalTime;

	// Handler for soundcard request for more data
	void waveOutProc(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwParam1, DWORD dwParam2)
	{
		if (uMsg != WOM_DONE) return;

		m_nBlockFree++;
		unique_lock<mutex> lm(m_muxBlockNotZero);
		m_cvBlockNotZero.notify_one();
	}

	// Static wrapper for sound card handler
	static void CALLBACK waveOutProcWrap(HWAVEOUT hWaveOut, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
	{
		((olcNoiseMaker*)dwInstance)->waveOutProc(hWaveOut, uMsg, dwParam1, dwParam2);
	}

	// Main thread. This loop responds to requests from the soundcard to fill 'blocks'
	// with audio data. If no requests are available it goes dormant until the sound
	// card is ready for more data. The block is fille by the "user" in some manner
	// and then issued to the soundcard.
	void MainThread()
	{
		m_dGlobalTime = 0.0;
		double dTimeStep = 1.0 / (double)m_nSampleRate;

		// Goofy hack to get maximum integer for a type at run-time
		T nMaxSample = (T)pow(2, (sizeof(T) * 8) - 1) - 1;
		double dMaxSample = (double)nMaxSample;
		T nPreviousSample = 0;

		while (m_bReady)
		{
			// Wait for block to become available
			if (m_nBlockFree == 0)
			{
				unique_lock<mutex> lm(m_muxBlockNotZero);
				m_cvBlockNotZero.wait(lm);
			}

			// Block is here, so use it
			m_nBlockFree--;

			// Prepare block for processing
			if (m_pWaveHeaders[m_nBlockCurrent].dwFlags & WHDR_PREPARED)
				waveOutUnprepareHeader(m_hwDevice, &m_pWaveHeaders[m_nBlockCurrent], sizeof(WAVEHDR));

			T nNewSample = 0;
			int nCurrentBlock = m_nBlockCurrent * m_nBlockSamples;
			
			for (unsigned int n = 0; n < m_nBlockSamples; n++)
			{
				// User Process
				if (m_userFunction == nullptr)
					nNewSample = (T)(clip(UserProcess(m_dGlobalTime), 1.0) * dMaxSample);
				else
					nNewSample = (T)(clip(m_userFunction(m_dGlobalTime), 1.0) * dMaxSample);

				m_pBlockMemory[nCurrentBlock + n] = nNewSample;
				nPreviousSample = nNewSample;
				m_dGlobalTime = m_dGlobalTime + dTimeStep;
			}

			// Send block to sound device
			waveOutPrepareHeader(m_hwDevice, &m_pWaveHeaders[m_nBlockCurrent], sizeof(WAVEHDR));
			waveOutWrite(m_hwDevice, &m_pWaveHeaders[m_nBlockCurrent], sizeof(WAVEHDR));
			m_nBlockCurrent++;
			m_nBlockCurrent %= m_nBlockCount;
		}
	}
};