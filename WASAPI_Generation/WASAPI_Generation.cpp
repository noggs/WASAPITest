#include <SDKDDKVer.h>

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <math.h>
#include <stdio.h>

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

#define REFTIMES_PER_SEC  (10000)
#define REFTIMES_PER_MILLISEC  (10)

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);


#define M_PI (3.14159265358979323846)

static double sFrequency = 4000.0;		// 4 kHz
static double sPhase = 0.0;


HRESULT LoadAudioBuffer(UINT32 bufferFrameCount, BYTE* pData, WAVEFORMATEX* pwfx, DWORD* outFlags)
{
	if (pwfx->wFormatTag != WAVE_FORMAT_EXTENSIBLE)
	{
		*outFlags = AUDCLNT_BUFFERFLAGS_SILENT;
		return S_OK;
	}
		
	WAVEFORMATEXTENSIBLE* pwfxe = (WAVEFORMATEXTENSIBLE*)pwfx;
	if (pwfxe->SubFormat != KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
	{
		*outFlags = AUDCLNT_BUFFERFLAGS_SILENT;
		return S_OK;
	}

	if (GetAsyncKeyState(VK_UP))
	{
		sFrequency += 100.0;
	}
	if (GetAsyncKeyState(VK_DOWN))
	{
		sFrequency -= 100.0;
	}
	if (GetAsyncKeyState(VK_ESCAPE))
	{
		*outFlags = AUDCLNT_BUFFERFLAGS_SILENT;
		return S_OK;
	}

	if (sFrequency < 10.0)
		sFrequency = 10.0;
	if (sFrequency > 8000.0)
		sFrequency = 8000.0;

	int sampleRate = pwfx->nSamplesPerSec;
	int numChannels = pwfx->nChannels;

	// ok now we know we can fill it with float data!
	float* output = (float*)pData;

	// Compute the phase increment for the current frequency
	double phaseInc = 2 * M_PI * sFrequency / sampleRate;

	// Generate the samples
	for (UINT32 i = 0; i < bufferFrameCount; i++)
	{
		float x = float(0.1 * sin(sPhase));
		for (int ch = 0; ch < numChannels; ch++)
			*output++ = x;
		sPhase += phaseInc;
	}

	// Bring phase back into range [0, 2pi]
	sPhase = fmod(sPhase, 2 * M_PI);

	return S_OK;
}


void PlayAudio()
{
	REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;	// microseconds, so this is 1 seconds
	REFERENCE_TIME hnsActualDuration;

	HRESULT hr;

	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	IAudioClient *pAudioClient = NULL;
	IAudioRenderClient *pRenderClient = NULL;
	WAVEFORMATEX *pwfx = NULL;
	UINT32 bufferFrameCount;
	UINT32 numFramesAvailable;
	UINT32 numFramesPadding;
	BYTE *pData;
	DWORD flags = 0;


	hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
	EXIT_ON_ERROR(hr);

	hr = pEnumerator->GetDefaultAudioEndpoint(
		eRender, eConsole, &pDevice);
	EXIT_ON_ERROR(hr);

	hr = pDevice->Activate(
			IID_IAudioClient, CLSCTX_ALL,
			NULL, (void**)&pAudioClient);
	EXIT_ON_ERROR(hr);

	hr = pAudioClient->GetMixFormat(&pwfx);
	EXIT_ON_ERROR(hr);

	hr = pAudioClient->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			0,
			hnsRequestedDuration,
			0,
			pwfx,
			NULL);
	EXIT_ON_ERROR(hr);

	// Get the actual size of the allocated buffer.
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
	EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetService(
                         IID_IAudioRenderClient,
                         (void**)&pRenderClient);
	EXIT_ON_ERROR(hr);

    // Grab the entire buffer for the initial fill operation.
    hr = pRenderClient->GetBuffer(bufferFrameCount, &pData);
	EXIT_ON_ERROR(hr);

	// load initial data
	hr = LoadAudioBuffer(bufferFrameCount, pData, pwfx, &flags);
	EXIT_ON_ERROR(hr);

	hr = pRenderClient->ReleaseBuffer(bufferFrameCount, flags);
	EXIT_ON_ERROR(hr);

	// Calculate the actual duration of the allocated buffer.
    hnsActualDuration = (REFERENCE_TIME)((double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec);

    hr = pAudioClient->Start();  // Start playing.
	EXIT_ON_ERROR(hr);

    // Each loop fills about half of the shared buffer.
	while (flags != AUDCLNT_BUFFERFLAGS_SILENT)
	{
		        // Sleep for half the buffer duration.
        Sleep((DWORD)(hnsActualDuration/REFTIMES_PER_MILLISEC/2));

        // See how much buffer space is available.
        hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
        EXIT_ON_ERROR(hr)

        numFramesAvailable = bufferFrameCount - numFramesPadding;

        // Grab all the available space in the shared buffer.
        hr = pRenderClient->GetBuffer(numFramesAvailable, &pData);
        EXIT_ON_ERROR(hr)

        // Get next 1/2-second of data from the audio source.
		hr = LoadAudioBuffer(numFramesAvailable, pData, pwfx, &flags);
        EXIT_ON_ERROR(hr)

        hr = pRenderClient->ReleaseBuffer(numFramesAvailable, flags);
        EXIT_ON_ERROR(hr)
    }

    // Wait for last data in buffer to play before stopping.
    Sleep((DWORD)(hnsActualDuration/REFTIMES_PER_MILLISEC/2));

    hr = pAudioClient->Stop();  // Stop playing.
	EXIT_ON_ERROR(hr);


Exit:
	CoTaskMemFree(pwfx);
	SAFE_RELEASE(pEnumerator);
	SAFE_RELEASE(pDevice);
	SAFE_RELEASE(pAudioClient);
	SAFE_RELEASE(pRenderClient);
}


int main()
{
	printf("Simple sine wave generator. UP/DOWN arrows to change frequency, ESC to quit\n");

	CoInitialize(NULL);

	PlayAudio();

	CoUninitialize();

    return 0;
}

