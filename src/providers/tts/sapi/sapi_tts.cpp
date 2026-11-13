#include "sapi_tts.h"
#include "utils.h"

#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <sapi.h>
// sphelper.h is intentionally NOT included — it unconditionally pulls in
// atlbase.h which is absent from the VS Build Tools VCTools-only install.
// The three Sp* helpers we need are re-implemented below using raw COM.

#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace HoundTTS {

namespace {

template<typename T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    explicit ComPtr(T* raw) : p(raw) {}
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() { return p; }
    T* get() { return p; }
    explicit operator bool() const { return p != nullptr; }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T* detach() { T* tmp = p; p = nullptr; return tmp; }
};

static bool IEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

static std::wstring GenderAttr(const std::string& gender) {
    if (IEq(gender, "male"))   return L"Gender=Male";
    if (IEq(gender, "female")) return L"Gender=Female";
    return L"";
}

static std::wstring LangAttr(const std::string& culture) {
    if (culture.empty()) return L"";
    struct { const wchar_t* bcp47; const wchar_t* lcid; } kMap[] = {
        { L"en-US", L"409" }, { L"en-GB", L"809" }, { L"en-AU", L"C09" },
        { L"fr-FR", L"40C" }, { L"de-DE", L"407" }, { L"es-ES", L"C0A" },
        { L"it-IT", L"410" }, { L"ru-RU", L"419" }, { L"zh-CN", L"804" },
        { L"ja-JP", L"411" },
    };
    std::wstring wc = Utils::Utf8ToWide(culture);
    for (auto& e : kMap)
        if (_wcsicmp(wc.c_str(), e.bcp47) == 0)
            return std::wstring(L"Language=") + e.lcid;
    return L"";
}

static HRESULT RawSpEnumTokens(const WCHAR* pszCatId, const WCHAR* pszReqAttribs,
                                 const WCHAR* pszOptAttribs, IEnumSpObjectTokens** ppEnum)
{
    ISpObjectTokenCategory* pCat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                  IID_ISpObjectTokenCategory, (void**)&pCat);
    if (FAILED(hr)) return hr;
    hr = pCat->SetId(pszCatId, FALSE);
    if (SUCCEEDED(hr)) hr = pCat->EnumTokens(pszReqAttribs, pszOptAttribs, ppEnum);
    pCat->Release();
    return hr;
}

static HRESULT RawSpGetDescription(ISpObjectToken* pToken, WCHAR** ppszDesc) {
    return pToken->GetStringValue(nullptr, ppszDesc);
}

static HRESULT RawSpFindBestToken(const WCHAR* pszCatId, const WCHAR* pszReqAttribs,
                                   const WCHAR* pszOptAttribs, ISpObjectToken** ppToken)
{
    IEnumSpObjectTokens* pEnum = nullptr;
    HRESULT hr = RawSpEnumTokens(pszCatId, pszReqAttribs, pszOptAttribs, &pEnum);
    if (FAILED(hr)) return hr;
    hr = pEnum->Next(1, ppToken, nullptr);
    pEnum->Release();
    return (hr == S_OK) ? S_OK : E_FAIL;
}

static HRESULT FindVoiceByName(const std::wstring& name, ISpObjectToken** ppToken) {
    IEnumSpObjectTokens* pEnum = nullptr;
    HRESULT hr = RawSpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &pEnum);
    if (FAILED(hr)) return hr;
    ULONG count = 0;
    pEnum->GetCount(&count);
    std::wstring lName = name;
    std::transform(lName.begin(), lName.end(), lName.begin(), ::towlower);
    HRESULT found = E_FAIL;
    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* pToken = nullptr;
        if (FAILED(pEnum->Next(1, &pToken, nullptr))) break;
        WCHAR* descRaw = nullptr;
        if (SUCCEEDED(RawSpGetDescription(pToken, &descRaw))) {
            std::wstring lDesc(descRaw);
            std::transform(lDesc.begin(), lDesc.end(), lDesc.begin(), ::towlower);
            CoTaskMemFree(descRaw);
            if (lDesc.find(lName) != std::wstring::npos) {
                *ppToken = pToken;
                found = S_OK;
                break;
            }
        }
        pToken->Release();
    }
    pEnum->Release();
    return found;
}

// ISpStream with SPDFID_WaveFormatEx writes a RIFF/WAVE header into the base
// stream. Locate the 'data' chunk payload: returns the offset and sets
// pcmBytes to the payload length. A buffer with no RIFF/WAVE signature is raw
// PCM starting at offset 0.
static size_t PcmDataOffset(const void* data, size_t byteCount, size_t& pcmBytes) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    pcmBytes = byteCount;
    if (byteCount < 12 || std::memcmp(bytes, "RIFF", 4) != 0 ||
        std::memcmp(bytes + 8, "WAVE", 4) != 0) {
        return 0;
    }
    size_t pos = 12;
    while (pos + 8 <= byteCount) {
        uint32_t chunkSize = 0;
        std::memcpy(&chunkSize, bytes + pos + 4, sizeof(chunkSize));
        const size_t payload = pos + 8;
        if (std::memcmp(bytes + pos, "data", 4) == 0) {
            // The writer may not have back-patched the size yet (0/stale), so
            // fall back to whatever is left in the buffer.
            const size_t remaining = byteCount - payload;
            pcmBytes = (chunkSize > 0 && chunkSize <= remaining) ? chunkSize : remaining;
            return payload;
        }
        pos = payload + chunkSize + (chunkSize & 1u);  // odd-sized chunks are padded
    }
    pcmBytes = 0;  // RIFF container without a data chunk: emit no audio
    return byteCount;
}

} // anonymous namespace

std::vector<int16_t> SapiTTS::Synthesize(
    const std::string& text,
    const std::string& voice,
    const std::string& gender,
    const std::string& culture,
    double speed,
    double volume)
{
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool coInited = (hrCo == S_OK || hrCo == S_FALSE);
    // Uninitialise on every path: an exception below must not leak the
    // apartment for this thread.
    struct CoGuard {
        bool active;
        ~CoGuard() {
            if (active) {
                CoFreeUnusedLibrariesEx(0, 0);
                CoUninitialize();
            }
        }
    } coGuard{coInited};
    std::vector<int16_t> result;

    do {
        ComPtr<ISpVoice> pVoice;
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                    IID_ISpVoice, (void**)&pVoice))) break;

        if (!voice.empty()) {
            ISpObjectToken* pToken = nullptr;
            if (SUCCEEDED(FindVoiceByName(Utils::Utf8ToWide(voice), &pToken))) {
                pVoice->SetVoice(pToken);
                pToken->Release();
            }
        } else {
            std::wstring lAttr = LangAttr(culture);
            std::wstring gAttr = GenderAttr(gender);
            if (!lAttr.empty() || !gAttr.empty()) {
                ISpObjectToken* pToken = nullptr;
                if (SUCCEEDED(RawSpFindBestToken(SPCAT_VOICES,
                                                 lAttr.empty() ? nullptr : lAttr.c_str(),
                                                 gAttr.empty() ? nullptr : gAttr.c_str(),
                                                 &pToken))) {
                    pVoice->SetVoice(pToken);
                    pToken->Release();
                }
            }
        }

        long sapiRate = static_cast<long>(speed);
        sapiRate = (std::max)(-10L, (std::min)(10L, sapiRate));
        pVoice->SetRate(sapiRate);

        USHORT sapiVol = static_cast<USHORT>((std::max)(0.0, (std::min)(1.0, volume)) * 100.0);
        pVoice->SetVolume(sapiVol);

        IStream* pRawStream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &pRawStream))) break;
        ComPtr<IStream> pMemStream(pRawStream);

        ComPtr<ISpStream> pSpStream;
        if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL,
                                    IID_ISpStream, (void**)&pSpStream))) break;

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = 1;
        wfx.nSamplesPerSec  = 16000;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = 2;
        wfx.nAvgBytesPerSec = 32000;
        wfx.cbSize          = 0;
        if (FAILED(pSpStream->SetBaseStream(pMemStream.get(), SPDFID_WaveFormatEx, &wfx))) break;
        if (FAILED(pVoice->SetOutput(pSpStream.get(), TRUE))) break;

        std::wstring wtext = Utils::Utf8ToWide(text);
        DWORD flags = (text.find("<speak>") != std::string::npos) ? SPF_IS_XML : SPF_DEFAULT;
        if (FAILED(pVoice->Speak(wtext.c_str(), flags, nullptr))) break;
        pVoice->WaitUntilDone(INFINITE);

        HGLOBAL hg = nullptr;
        if (FAILED(GetHGlobalFromStream(pMemStream.get(), &hg))) break;
        SIZE_T byteCount = GlobalSize(hg);
        if (byteCount < 2) break;
        void* pData = GlobalLock(hg);
        if (!pData) break;
        // Strip the RIFF/WAVE container that ISpStream prepends; keep raw PCM only.
        size_t pcmBytes = 0;
        const size_t pcmOffset = PcmDataOffset(pData, byteCount, pcmBytes);
        // Unlock on every path: the sample copy allocates and can throw.
        struct UnlockGuard {
            HGLOBAL h;
            ~UnlockGuard() { GlobalUnlock(h); }
        } unlockGuard{hg};
        result = Utils::PcmBytesToSamples(
            static_cast<const uint8_t*>(pData) + pcmOffset, pcmBytes);
    } while (false);

    return result;
}

} // namespace HoundTTS
