#include "sapi_tts.h"
#include "../../utils.h"

#include <windows.h>
#include <objbase.h>
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

// Minimal RAII wrapper for COM interface pointers — avoids ATL dependency.
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

// Case-insensitive ASCII compare
static bool IEq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// Map gender string → SAPI attribute query value
static std::wstring GenderAttr(const std::string& gender) {
    if (IEq(gender, "male"))   return L"Gender=Male";
    if (IEq(gender, "female")) return L"Gender=Female";
    return L"";
}

// Map BCP-47 culture tag → SAPI "Language=<LCID_hex>" attribute string
static std::wstring LangAttr(const std::string& culture) {
    if (culture.empty()) return L"";

    struct { const wchar_t* bcp47; const wchar_t* lcid; } kMap[] = {
        { L"en-US", L"409"  },
        { L"en-GB", L"809"  },
        { L"en-AU", L"C09"  },
        { L"en-CA", L"1009" },
        { L"fr-FR", L"40C"  },
        { L"de-DE", L"407"  },
        { L"es-ES", L"C0A"  },
        { L"it-IT", L"410"  },
        { L"ru-RU", L"419"  },
        { L"zh-CN", L"804"  },
        { L"ja-JP", L"411"  },
    };

    std::wstring wc = Utils::Utf8ToWide(culture);
    for (auto& e : kMap) {
        if (_wcsicmp(wc.c_str(), e.bcp47) == 0)
            return std::wstring(L"Language=") + e.lcid;
    }
    return L"";
}

// Raw-COM replacement for SpEnumTokens (avoids sphelper.h / atlbase.h).
// Uses ISpObjectTokenCategory::EnumTokens which handles attribute filtering.
static HRESULT RawSpEnumTokens(
    const WCHAR* pszCatId,
    const WCHAR* pszReqAttribs,
    const WCHAR* pszOptAttribs,
    IEnumSpObjectTokens** ppEnum)
{
    ISpObjectTokenCategory* pCat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr,
                                  CLSCTX_ALL,
                                  IID_ISpObjectTokenCategory,
                                  (void**)&pCat);
    if (FAILED(hr)) return hr;
    hr = pCat->SetId(pszCatId, FALSE);
    if (SUCCEEDED(hr))
        hr = pCat->EnumTokens(pszReqAttribs, pszOptAttribs, ppEnum);
    pCat->Release();
    return hr;
}

// Raw-COM replacement for SpGetDescription.
// Reads the token's default string value (its display name).
static HRESULT RawSpGetDescription(ISpObjectToken* pToken, WCHAR** ppszDesc) {
    return pToken->GetStringValue(nullptr, ppszDesc);
}

// Raw-COM replacement for SpFindBestToken.
// Returns the first token from the filtered enum.
static HRESULT RawSpFindBestToken(
    const WCHAR* pszCatId,
    const WCHAR* pszReqAttribs,
    const WCHAR* pszOptAttribs,
    ISpObjectToken** ppToken)
{
    IEnumSpObjectTokens* pEnum = nullptr;
    HRESULT hr = RawSpEnumTokens(pszCatId, pszReqAttribs, pszOptAttribs, &pEnum);
    if (FAILED(hr)) return hr;
    hr = pEnum->Next(1, ppToken, nullptr);
    pEnum->Release();
    return (hr == S_OK) ? S_OK : E_FAIL;
}

// Find a voice token by case-insensitive substring match on its description.
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
                *ppToken = pToken; // caller owns ref
                found = S_OK;
                break;
            }
        }
        pToken->Release();
    }
    pEnum->Release();
    return found;
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

    std::vector<int16_t> result;

    do { // break-on-error block

        ComPtr<ISpVoice> pVoice;
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                    IID_ISpVoice, (void**)&pVoice)))
            break;

        // --- Voice selection ---
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

        // --- Rate: speed is SAPI rate -10..+10 (0=normal), matching STTS/HOUND convention ---
        long sapiRate = static_cast<long>(speed);
        sapiRate = (std::max)(-10L, (std::min)(10L, sapiRate));
        pVoice->SetRate(sapiRate);

        // --- Volume: 0.0..1.0 → SAPI 0..100 ---
        USHORT sapiVol = static_cast<USHORT>(
            (std::max)(0.0, (std::min)(1.0, volume)) * 100.0);
        pVoice->SetVolume(sapiVol);

        // --- Output: IStream backed by HGLOBAL (ole32 — already linked) ---
        IStream* pRawStream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &pRawStream))) break;
        ComPtr<IStream> pMemStream(pRawStream);

        ComPtr<ISpStream> pSpStream;
        if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL,
                                    IID_ISpStream, (void**)&pSpStream)))
            break;

        WAVEFORMATEX wfx = {};
        wfx.wFormatTag      = WAVE_FORMAT_PCM;
        wfx.nChannels       = 1;
        wfx.nSamplesPerSec  = 16000;
        wfx.wBitsPerSample  = 16;
        wfx.nBlockAlign     = 2;   // nChannels * wBitsPerSample / 8
        wfx.nAvgBytesPerSec = 32000; // nSamplesPerSec * nBlockAlign
        wfx.cbSize          = 0;
        if (FAILED(pSpStream->SetBaseStream(pMemStream.get(),
                                            SPDFID_WaveFormatEx,
                                            &wfx)))
            break;

        if (FAILED(pVoice->SetOutput(pSpStream.get(), TRUE)))
            break;

        // --- Synthesize (synchronous) ---
        std::wstring wtext = Utils::Utf8ToWide(text);
        DWORD flags = (text.find("<speak>") != std::string::npos)
                      ? SPF_IS_XML : SPF_DEFAULT;
        if (FAILED(pVoice->Speak(wtext.c_str(), flags, nullptr)))
            break;

        pVoice->WaitUntilDone(INFINITE);

        // --- Read PCM from HGLOBAL stream ---
        HGLOBAL hg = nullptr;
        if (FAILED(GetHGlobalFromStream(pMemStream.get(), &hg))) break;

        SIZE_T byteCount = GlobalSize(hg);
        if (byteCount < 2) break;

        void* pData = GlobalLock(hg);
        if (!pData) break;

        size_t sampleCount = byteCount / 2;
        result.resize(sampleCount);
        std::memcpy(result.data(), pData, sampleCount * 2);
        GlobalUnlock(hg);

    } while (false);

    if (coInited) CoUninitialize();
    return result;
}

} // namespace HoundTTS
