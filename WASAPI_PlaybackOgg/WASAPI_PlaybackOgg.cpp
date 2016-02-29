#include <SDKDDKVer.h>

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <math.h>

#include "stb_vorbis.c"

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

#define REFTIMES_PER_SEC  (1000)
#define REFTIMES_PER_MILLISEC  (1)

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);


static SIZE_T sAudioFileSize = 0;
static BYTE* sAudioFileData = NULL;

static short* sDecodedData = NULL;
static int sDecodedNumChannels = 0;
static int sDecodedNumSamples = 0;

void LoadAudioFileIntoMemory()
{
	const wchar_t* filename = L"../alarm_clock_stereo.ogg";
	HANDLE fileHandle;
	
	fileHandle = CreateFile(filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (INVALID_HANDLE_VALUE == fileHandle)
		return;

	sAudioFileSize = GetFileSize(fileHandle, NULL);

	sAudioFileData = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sAudioFileSize);

	if (TRUE != ReadFile(fileHandle, sAudioFileData, sAudioFileSize, NULL, NULL))
	{
		sAudioFileSize = 0;
		HeapFree(GetProcessHeap(), 0, sAudioFileData);
	}

	CloseHandle(fileHandle);

	int channels = 0, sampleRate = 0, numSamples = 0;
	short* decodedAudio = NULL;
	numSamples = stb_vorbis_decode_memory(sAudioFileData, sAudioFileSize, &channels, &sampleRate, &decodedAudio);

	if (numSamples > 0)
	{
		sDecodedData = decodedAudio;
		sDecodedNumChannels = channels;
		sDecodedNumSamples = numSamples * channels;
	}
}



static int sDataPosition = 0;

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

	int sampleRate = pwfx->nSamplesPerSec;
	int numChannels = pwfx->nChannels;

	// ok now we know we can fill it with float data!
	float* output = (float*)pData;

	if (sDataPosition >= sDecodedNumSamples)
	{
		*outFlags = AUDCLNT_BUFFERFLAGS_SILENT;
		return S_OK;
	}

	int endPosition = sDataPosition + (bufferFrameCount * sDecodedNumChannels);
	if (endPosition > sDecodedNumSamples)
		endPosition = sDecodedNumSamples;

	// Generate the samples
	for (int i = sDataPosition; i < endPosition;)
	{
		short l = sDecodedData[i++];
		short r = sDecodedData[i++];

		*output++ = (float)l / 32768.0f;
		*output++ = (float)l / 32768.0f;
	}

	sDataPosition = endPosition;

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
	CoInitialize(NULL);

	LoadAudioFileIntoMemory();
	PlayAudio();

	CoUninitialize();

    return 0;
}

