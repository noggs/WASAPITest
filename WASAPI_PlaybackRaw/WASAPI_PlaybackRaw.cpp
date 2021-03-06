#include <SDKDDKVer.h>

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <math.h>

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

#define REFTIMES_PER_SEC  (100000)
#define REFTIMES_PER_MILLISEC  (100)

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);


static SIZE_T sAudioFileSize = 0;
static BYTE* sAudioFileData = NULL;

void LoadAudioFileIntoMemory()
{
	// if you change the file format you need to change a few hardcoded types/values (search for HARDCODED_FORMAT)
	const wchar_t* filename = L"../alarm_clock_f32_stereo.raw";
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
}



static SIZE_T sDataPosition = 0;

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

	size_t fileSize = sAudioFileSize / sizeof(float);	// HARDCODED_FORMAT: sizeof(source type)

	// ok now we know we can fill it with float data!
	float* output = (float*)pData;

	if (sDataPosition >= fileSize)
	{
		*outFlags = AUDCLNT_BUFFERFLAGS_SILENT;
		return S_OK;
	}

	size_t endPosition = sDataPosition + (bufferFrameCount * 2);	// HARDCODED_FORMAT: (2) for stereo, (1) for mono
	if (endPosition > fileSize)
		endPosition = fileSize;

	// Generate the samples
	for (size_t i = sDataPosition; i < endPosition;)
	{
		float l = ((float*)sAudioFileData)[i++];
		float r = ((float*)sAudioFileData)[i++];		// HARDCODED_FORMAT: Use this read for Stereo only
		
		//*output++ = (float)l / 32768.0f;	// HARDCODED_FORMAT: Use this for 16 bit PCM
		//*output++ = (float)l / 32768.0f;

		*output++ = (float)l;				// HARDCODED_FORMAT: Use this for float32 data
		*output++ = (float)r;

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

