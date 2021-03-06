﻿#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include "AudioEngine.h"
#include <cassert>
#include <cwchar>
#include <new>
#include "mpg123.h"

namespace WrapAL {
    // load function
    template<typename T> static inline auto load_func(T& pointer, HMODULE dll, const char* name) noexcept {
        pointer = reinterpret_cast<T>(::GetProcAddress(dll, name));
    }
}

// Memory leak detector
#if defined(_DEBUG) && defined(_MSC_VER) && defined(WRAPAL_MEMDEBUG_IN_MSC)
struct MemoryLeakDetector {
    // ctor
    MemoryLeakDetector() {
        ::_CrtMemCheckpoint(memstate + 0);
        constexpr int sa = sizeof(_CrtMemState);
    }
    // dtor
    ~MemoryLeakDetector() {
        ::_CrtMemCheckpoint(memstate + 1);
        if (::_CrtMemDifference(memstate + 2, memstate + 0, memstate + 1)) {
            ::_CrtDumpMemoryLeaks();
            assert(!"OOps! Memory leak detected");
        }
    }
    // mem state
    _CrtMemState memstate[3];
} g_detector;
#endif


// the _wrapal32
constexpr uint32_t operator"" _wrapal32(const char* src, size_t len) {
    return len == 2 ?
        static_cast<uint32_t>(src[0]) << (8 * 0) |
        static_cast<uint32_t>(src[1]) << (8 * 1) :
        static_cast<uint32_t>(src[0]) << (8 * 0) |
        static_cast<uint32_t>(src[1]) << (8 * 1) |
        static_cast<uint32_t>(src[2]) << (8 * 2) |
        static_cast<uint32_t>(src[3]) << (8 * 3);
}

#ifdef WRAPAL_INCLUDE_DEFAULT_AUDIO_STREAM
// Ogg Vorbis
#include "../3rdparty/libvorbis/include/vorbis/codec.h"
#include "../3rdparty/libvorbis/include/vorbis/vorbisfile.h"

// wrapal namespace
namespace WrapAL {
    // default error code for audio stream
    enum class DefErrorCode : uint32_t {
        // Ok, No Error
        Code_Ok = 0,
        // Illegal File
        Code_IllegalFile,
        // Unsupported Format
        Code_UnsupportedFormat,
        // Decode Error
        Code_DecodeError,
        // out of memory
        Code_OutOfMemory,
    };
    // alignedsize
    constexpr auto alignedsize(const size_t n, const size_t a) noexcept { 
        return (((((n)-1) / (a)) + 1)*(a)); 
    }
    // Basic Audio Stream
    class WRAPAL_NOVTABLE CALBasicAudioStream : public XALAudioStream {
        // super class
        using Super = XALAudioStream;
    public:
        // ctor
        CALBasicAudioStream(IALFileStream* stream) noexcept : Super(stream) { 
            assert(stream); 
        }
        // dtor
        ~CALBasicAudioStream() noexcept = default;
        // get code
        auto GetErrorCode() const noexcept { return m_code; }
    public: // interface impl for XALAudioStream
        // add ref-count 
        virtual auto AddRef() noexcept ->uint32_t override final;
        // get last error infomation, return false if no error
        virtual auto GetLastErrorInfo(wchar_t info[/*ErrorInfoLength*/]) noexcept ->bool override;
    protected:
        // release helper
        template<typename T> auto release_helper(T call) noexcept ->uint32_t {
            auto count = --m_cRefCount;
            if (!count) call();
            return count;
        }
    protected:
        // last error code
        DefErrorCode            m_code = DefErrorCode::Code_Ok;
        // ref-count
        uint32_t                m_cRefCount = 1;
    };
    // Audio Stream for wave file(PCM/IEEE_FLOAT only)
    class CALWavAudioStream final : public CALBasicAudioStream {
        // super class define
        using Super = CALBasicAudioStream;
    public:
        // ctor
        CALWavAudioStream(IALFileStream*) noexcept;
        // create this
        static auto Create(IALFileStream* s) noexcept {
            using athis_t = CALWavAudioStream;
            auto c = WrapALAudioEngine.configure;
            if (const auto ptr = c->SmallAlloc<athis_t>()) {
                return new (ptr) athis_t(s);
            }
            return (CALWavAudioStream*)(nullptr);
        }
    public: // interface impl for XALAudioStream
        // release this
        virtual auto Release() noexcept ->uint32_t override {
            return this->release_helper([=]() noexcept { 
                this->~CALWavAudioStream();
                WrapALAudioEngine.configure->SmallFree(this);
            });
;        }
        // seek stream in byte, return false if out of range
        virtual auto Seek(int32_t off, Move method) noexcept ->uint32_t override;
        // read stream, return byte count read
        virtual auto ReadNext(uint32_t l, void* b) noexcept ->uint32_t override { return m_pFileStream->ReadNext(l, b); }
    private:
        // zero postion offset
        int32_t             m_zeroPosOffset = 0;
    };
    // Audio Stream for ogg file
    class CALOggAudioStream final : public CALBasicAudioStream {
        // super class define
        using Super = CALBasicAudioStream;
    public:
        // ctor
        CALOggAudioStream(IALFileStream*) noexcept;
        // dtor
        ~CALOggAudioStream() noexcept { ::ov_clear(&m_ovfile); }
        // create this
        static auto Create(IALFileStream* s) noexcept {
            using athis_t = CALOggAudioStream;
            if (const auto ptr = std::malloc(sizeof(athis_t))) {
                return new (ptr) athis_t(s);
            }
            return (CALOggAudioStream*)(nullptr);
        }
    public: // interface impl for XALAudioStream
        // release this
        virtual auto Release() noexcept ->uint32_t override;
        // seek stream in byte, return false if out of range
        virtual auto Seek(int32_t off, Move method) noexcept ->uint32_t override;
        // read stream, return byte count read
        virtual auto ReadNext(uint32_t, void*) noexcept ->uint32_t override;
    private:
        // ogg file
        OggVorbis_File          m_ovfile;
    };
    // Audio Stream for mp3 file
    class CALMp3AudioStream final : public CALBasicAudioStream {
        // super class define
        using Super = CALBasicAudioStream;
    public:
        // ctor
        CALMp3AudioStream(IALFileStream*) noexcept;
        // dtor
        ~CALMp3AudioStream() noexcept {
            if (m_hMpg123) {
                Mpg123::mpg123_delete(m_hMpg123);
                m_hMpg123 = nullptr;
            }
        }
        // create this
        static auto Create(IALFileStream* s) noexcept {
            using athis_t = CALMp3AudioStream;
            auto c = WrapALAudioEngine.configure;
            if (const auto ptr = c->SmallAlloc<athis_t>()) {
                return new (ptr) athis_t(s);
            }
            return (CALMp3AudioStream*)(nullptr);
        }
    public: // interface impl for XALAudioStream
        // release this
        virtual auto Release() noexcept ->uint32_t override;
        // seek stream in byte, return false if out of range
        virtual auto Seek(int32_t off, Move method) noexcept ->uint32_t override;
        // read stream, return byte count read
        virtual auto ReadNext(uint32_t, void*) noexcept ->uint32_t override;
    private:
        // mpg123 file
        mpg123_handle*         m_hMpg123 = nullptr;
    };
}

// mpg123 编码
enum ENCODE_ENUM {
    ENCODE_ENUM_8 = 0x00f, ENCODE_ENUM_16 = 0x040, ENCODE_ENUM_24 = 0x4000, ENCODE_ENUM_32 = 0x100,
    ENCODE_ENUM_SIGNED = 0x080, ENCODE_ENUM_FLOAT = 0xe00,
    ENCODE_ENUM_SIGNED_16 = (ENCODE_ENUM_16 | ENCODE_ENUM_SIGNED | 0x10),
    ENCODE_ENUM_UNSIGNED_16 = (ENCODE_ENUM_16 | 0x20), ENCODE_ENUM_UNSIGNED_8 = 0x01,
    ENCODE_ENUM_SIGNED_8 = (ENCODE_ENUM_SIGNED | 0x02), ENCODE_ENUM_ULAW_8 = 0x04,
    ENCODE_ENUM_ALAW_8 = 0x08, ENCODE_ENUM_SIGNED_32 = ENCODE_ENUM_32 | ENCODE_ENUM_SIGNED | 0x1000,
    ENCODE_ENUM_UNSIGNED_32 = ENCODE_ENUM_32 | 0x2000,
    ENCODE_ENUM_SIGNED_24 = ENCODE_ENUM_24 | ENCODE_ENUM_SIGNED | 0x1000,
    ENCODE_ENUM_UNSIGNED_24 = ENCODE_ENUM_24 | 0x2000,
    ENCODE_ENUM_FLOAT_32 = 0x200,
    ENCODE_ENUM_FLOAT_64 = 0x400,
    ENCODE_ENUM_ANY = (ENCODE_ENUM_SIGNED_16 | ENCODE_ENUM_UNSIGNED_16 | ENCODE_ENUM_UNSIGNED_8
    | ENCODE_ENUM_SIGNED_8 | ENCODE_ENUM_ULAW_8 | ENCODE_ENUM_ALAW_8
    | ENCODE_ENUM_SIGNED_32 | ENCODE_ENUM_UNSIGNED_32
    | ENCODE_ENUM_SIGNED_24 | ENCODE_ENUM_UNSIGNED_24
    | ENCODE_ENUM_FLOAT_32 | ENCODE_ENUM_FLOAT_64)
};

/// <summary>
/// Adds the reference.
/// </summary>
/// <returns></returns>
auto WrapAL::CALBasicAudioStream::AddRef() noexcept -> uint32_t {
    auto count = ++m_cRefCount;
    assert(count < 255 && "too large");
    return count;
}


/// <summary>
/// Gets the last error information.
/// 获取最近一次错误
/// </summary>
/// <param name="des_info">The des_info.</param>
/// <returns></returns>
bool WrapAL::CALBasicAudioStream::GetLastErrorInfo(wchar_t des_info[]) noexcept {
    if (m_code != DefErrorCode::Code_Ok) {
        const wchar_t* info = nullptr;
        switch (m_code)
        {
        case WrapAL::DefErrorCode::Code_Ok:
            info = L"WrapAL::DefErrorCode::Code_Ok";
            break;
        case WrapAL::DefErrorCode::Code_IllegalFile:
            info = L"Illegal File";
            break;
        case WrapAL::DefErrorCode::Code_UnsupportedFormat:
            info = L"Unsupported Format";
            break;
        case WrapAL::DefErrorCode::Code_DecodeError:
            info = L"Decode Error";
            break;
        case WrapAL::DefErrorCode::Code_OutOfMemory:
            info = WrapALAudioEngine.GetRuntimeMessage(Message_OOM);
            break;
        default:
            info = L"Unknown Error";
        }
        std::wcscpy(des_info, info);
        return true;
    }
    return false;
}

/// <summary>
/// Initializes a new instance of the <see cref="CALWavAudioStream"/> class.
/// <see cref="CALWavAudioStream"/> 构造函数
/// </summary>
/// <param name="file_stream">The file_stream.</param>
WrapAL::CALWavAudioStream::CALWavAudioStream(IALFileStream* file_stream) noexcept : Super(file_stream) {
    // 检查错误
    if (m_code != DefErrorCode::Code_Ok) return;
    // 解析数据结构
#pragma pack(push)
    // 保存对齐状态
#pragma pack(1)
    // WAVE 文件头
    struct {
        // RIFF 文件头
        char    szRiffID[4];        // 'R','I','F','F'
        DWORD   dwRiffSize;         // 整个wav文件大小减去 "RIFF"以及本对象(文件大小-8)
        char    szRiffFormat[4];    // 'W','A','V','E'
        // FMT 区块
        char    szFmtID[4];         // 'f','m','t', 0x20
        DWORD   dwFmtSize;          // fmt 区块大小(16?18?)
        // WAVE格式
        WORD    wFormatTag;         // format type
        WORD    nChannels;          // number of channels (i.e. mono, stereo...)
        DWORD   nSamplesPerSec;     // sample rate
        DWORD   nAvgBytesPerSec;    // for buffer estimation
        WORD    nBlockAlign;        // block size of data 
        WORD    wBitsPerSample;     // number of bits per sample of mono data
    } header;
    // FACT 块 可选 / 数据大小
    union {       
        struct {
            char    szFactID[4];    // 'f','a','c','t'
            DWORD   dwFactSize;     // fact 大小(一般为4?)
        };
        struct {
            char    szDataID[4];    // 'd','a','t','a'
            DWORD   dwDataSize;     // 数据大小
        } ;
    } fact_data;
    // 恢复对齐状态
#pragma pack(pop)
    auto code = DefErrorCode::Code_Ok;
    // 读取文件头
    if (!m_pFileStream->ReadNext(sizeof(header), &header)) {
        code = DefErrorCode::Code_IllegalFile;
    }
    // 检查合法性
    if (code == DefErrorCode::Code_Ok) {
        if ((*reinterpret_cast<uint32_t*>(header.szRiffID) !=  "RIFF"_wrapal32)   ||
            (*reinterpret_cast<uint32_t*>(header.szRiffFormat) != "WAVE"_wrapal32) ||
            (*reinterpret_cast<uint32_t*>(header.szFmtID) != "fmt\x20"_wrapal32))
        code = DefErrorCode::Code_IllegalFile;
    }
    // 检查格式支持
    if (code == DefErrorCode::Code_Ok) {
        if (!(header.wFormatTag == Wave_PCM || header.wFormatTag == Wave_IEEEFloat)) {
            code = DefErrorCode::Code_UnsupportedFormat;
        }
    }
    // 检查剩余部分
    if (code == DefErrorCode::Code_Ok) {
        m_pFileStream->Seek(int32_t(header.dwFmtSize - 16), IALStream::Move_Current);
        if (!m_pFileStream->ReadNext(sizeof(fact_data), &fact_data)) {
            code = DefErrorCode::Code_IllegalFile;
        }
    }
    // fact 还是 data
    if (code == DefErrorCode::Code_Ok) {
        // fact 块?
        if ((*reinterpret_cast<uint32_t*>(fact_data.szFactID) == "face"_wrapal32)) {
            m_pFileStream->Seek(int32_t(fact_data.dwFactSize - 4), IALStream::Move_Current);
            m_pFileStream->ReadNext(sizeof(fact_data), &fact_data);
        }
        // data 块?
        if ((*reinterpret_cast<uint32_t*>(fact_data.szDataID) == "data"_wrapal32)) {
            m_cTotalSize = fact_data.dwDataSize;
            m_zeroPosOffset = m_pFileStream->Tell();
        }
        else {
            code = DefErrorCode::Code_IllegalFile;
        }
    }
    // 复制数据
    if (code == DefErrorCode::Code_Ok) {
        m_audioFormat.nSamplesPerSec = header.nSamplesPerSec;
        //m_audioFormat.nAvgBytesPerSec = header.nAvgBytesPerSec;
        m_audioFormat.nBlockAlign = header.nBlockAlign;
        m_audioFormat.nChannels = uint8_t(header.nChannels);
        m_audioFormat.nFormatTag = FormatWave(header.wFormatTag);
    }
    m_code = code;
}

/// <summary>
/// Seeks the specified offset.
/// 设置读取位置
/// </summary>
/// <param name="off">The off.</param>
/// <param name="method">The method.</param>
/// <returns></returns>
auto WrapAL::CALWavAudioStream::Seek(int32_t off, Move method) noexcept -> uint32_t {
    if (method == Move_Begin) {
        off += m_zeroPosOffset;
    }
    return m_pFileStream->Seek(off, method) - uint32_t(m_zeroPosOffset);
}

// wrapal namespace
namespace WrapAL {
    // ogg read call back
    static ov_callbacks OggAudioStreamCallback = {
        // size_t (*read_func)  (void *ptr, size_t size, size_t nmemb, void *datasource);
        [](void* buf, size_t e, size_t c, void* s) noexcept ->size_t {
            register auto stream = reinterpret_cast<IALFileStream*>(s);
            return size_t(stream->ReadNext(uint32_t(e*c), buf) / e);
        },
        // int    (*seek_func)  (void *datasource, ogg_int64_t offset, int whence);
        [](void* s, ogg_int64_t offset, int whence) noexcept ->int {
            register auto stream = reinterpret_cast<IALFileStream*>(s);
            stream->Seek(int32_t(offset), IALStream::Move(whence));
            return 0;
        },
        // int    (*close_func) (void *datasource);
        nullptr,
        // long   (*tell_func)  (void *datasource);
        [](void* s) noexcept ->long {
            register auto stream = reinterpret_cast<IALFileStream*>(s);
            return long(stream->Tell());
        },
    };
}

/// <summary>
/// Initializes a new instance of the <see cref="CALOggAudioStream"/> class.
/// <see cref="CALOggAudioStream"/> 构造函数
/// </summary>
/// <param name="file_stream">The file_stream.</param>
WrapAL::CALOggAudioStream::CALOggAudioStream(IALFileStream* file_stream) noexcept : Super(file_stream) {
    // 检查错误
    if (m_code != DefErrorCode::Code_Ok) return;
    if (::ov_open_callbacks(m_pFileStream, &m_ovfile, nullptr, 0, WrapAL::OggAudioStreamCallback) >= 0) {
        //char **ptr = ov_comment(&ovfile, -1)->user_comments;
        vorbis_info *vi = ov_info(&m_ovfile, -1);
        // 获取声道数
        m_audioFormat.nChannels = vi->channels;
        // 获取采样率
        m_audioFormat.nSamplesPerSec = vi->rate;
        // 区块对齐
        m_audioFormat.nBlockAlign = m_audioFormat.nChannels * sizeof(int16_t);
        // bps
        //m_audioFormat.nAvgBytesPerSec = m_audioFormat.nSamplesPerSec * m_audioFormat.nBlockAlign;
        // 编码: PCM
        m_audioFormat.nFormatTag = Wave_PCM;
        // 数据大小
        m_cTotalSize = static_cast<decltype(m_cTotalSize)>(::ov_pcm_total(&m_ovfile, -1)) *
            static_cast<decltype(m_cTotalSize)>(m_audioFormat.nBlockAlign);
    }
    // 非法文件
    else {
        m_code = DefErrorCode::Code_IllegalFile;
    }
}

/// <summary>
/// Releases this instance.
/// CALOggAudioStream 释放数据
/// </summary>
/// <returns></returns>
auto WrapAL::CALOggAudioStream::Release() noexcept -> uint32_t {
    return this->release_helper([this]() noexcept { 
        this->~CALOggAudioStream(); 
        std::free(this);
    });
}

/// <summary>
/// Seeks the specified off.
/// CALOggAudioStream 定位
/// </summary>
/// <param name="off">The off.</param>
/// <param name="method">The method.</param>
/// <returns></returns>
auto WrapAL::CALOggAudioStream::Seek(int32_t off, Move method) noexcept -> uint32_t {
    // 不用移动
    if (!(off == 0 && method == Move_Current)) {
        auto code = ::ov_pcm_seek(&m_ovfile, off / m_audioFormat.nBlockAlign);
        // TODO: ERROR
        if (code) {

        }
    }
    ::ov_pcm_tell(&m_ovfile);
    return 0;
}

/// <summary>
/// Reads the next.
/// CALOggAudioStream 读取数据
/// </summary>
/// <param name="len">The length.</param>
/// <param name="buf">The buf.</param>
/// <returns></returns>
auto WrapAL::CALOggAudioStream::ReadNext(uint32_t len, void* buf) noexcept -> uint32_t {
#ifndef NDEBUG
    const auto oldbuf = reinterpret_cast<char*>(buf);
    const auto oldlen = size_t(len);
#endif
    uint32_t read = 0;
    // 循环读取数据
    while (len) {
        int bitstream = 0;
        auto charbuf = reinterpret_cast<char*>(buf);
#ifndef NDEBUG
        auto bk = charbuf - oldbuf;
#endif
        auto code = ::ov_read(&m_ovfile, charbuf, len, 0, 2, 1, &bitstream);
        // EOF?
        if (!code) break;
        // Error?
        if (code < 0) {
            m_code = DefErrorCode::Code_DecodeError;
            break;
        }
        // OK!
        else {
            len -= code;
            buf = (reinterpret_cast<char*>(buf) + code);
            read += code;
        }
    }
    return read;
}

/// <summary>
/// Initializes a new instance of the <see cref="CALMp3AudioStream"/> class.
/// <see cref="CALMp3AudioStream"/> 构造函数
/// </summary>
/// <param name="file_stream">The file_stream.</param>
WrapAL::CALMp3AudioStream::CALMp3AudioStream(IALFileStream* file_stream) noexcept : Super(file_stream) {
    // 检查错误
    if (m_code != DefErrorCode::Code_Ok) return;
    // 检查dll
    assert(Mpg123::mpg123_init);;
    // 创建新的句柄
    int error_code = 0;
    m_hMpg123 = Mpg123::mpg123_new(nullptr, &error_code);
    // 替换新的READ/SEEK函数
    if (!error_code) {
        error_code = Mpg123::mpg123_replace_reader_handle(
            m_hMpg123,
            [](void* file, void* buf, size_t len) ->ssize_t {
                register auto stream = reinterpret_cast<IALFileStream*>(file);
                return ssize_t(stream->ReadNext(uint32_t(len), buf));
            },
            [](void* file, off_t off, int org) ->off_t {
                register auto stream = reinterpret_cast<IALFileStream*>(file);
                return off_t(stream->Seek(int32_t(off), IALStream::Move(org)));
            },
            nullptr
            );
    }
    // 以句柄方式打开mpg123
    if (!error_code) {
        error_code = Mpg123::mpg123_open_handle(m_hMpg123, m_pFileStream);
    }
    // 获取格式
    int encoding = 0;
    if (!error_code) {
        long rate = 0; int ch = 0;
        error_code = Mpg123::mpg123_getformat(m_hMpg123, &rate, &ch, &encoding);
        m_audioFormat.nChannels = ch;
        m_audioFormat.nSamplesPerSec = rate;
    }
    // 检查编码支持
    if (!error_code) {
        if (encoding != ENCODE_ENUM_SIGNED_16 && encoding != ENCODE_ENUM_FLOAT_32) {
            m_code = DefErrorCode::Code_UnsupportedFormat;
            return;
        }
    }
    // 保证编码不再改变
    if (!error_code) {
        error_code = Mpg123::mpg123_format_none(m_hMpg123);
    }
    // 保证编码不再改变
    if (!error_code) {
        error_code = Mpg123::mpg123_format(
            m_hMpg123, 
            m_audioFormat.nSamplesPerSec,
            m_audioFormat.nChannels,
            encoding
            );
    }
    // 填写格式
    if (!error_code) {
        // 获取深度
        if (encoding & ENCODE_ENUM_FLOAT_64) {
            m_audioFormat.nBlockAlign = 8 * m_audioFormat.nChannels;
            m_audioFormat.nFormatTag = Wave_IEEEFloat;
        }
        else if (encoding & ENCODE_ENUM_FLOAT_32) {
            m_audioFormat.nBlockAlign = 4 * m_audioFormat.nChannels;
            m_audioFormat.nFormatTag = Wave_IEEEFloat;
        }
        else if (encoding & ENCODE_ENUM_16) {
            m_audioFormat.nBlockAlign = 2 * m_audioFormat.nChannels;
            m_audioFormat.nFormatTag = Wave_PCM;
        }
        else {
            m_audioFormat.nBlockAlign = 1 * m_audioFormat.nChannels;
            m_audioFormat.nFormatTag = Wave_PCM;
        }
        // 数据率
        //m_audioFormat.nAvgBytesPerSec = m_audioFormat.nSamplesPerSec * m_audioFormat.nBlockAlign;
    }
    // 计算长度
    if (!error_code) {
        auto length_in_sample = Mpg123::mpg123_length(m_hMpg123);
        // 有效
        if (length_in_sample > 0) {
            m_cTotalSize = length_in_sample * m_audioFormat.nBlockAlign / m_audioFormat.nChannels;
        }
        // 错误
        else {
            m_code = DefErrorCode::Code_IllegalFile;
            return;
        }
    }
    // 检查错误
    if (error_code == MPG123_OUT_OF_MEM) {
        m_code = DefErrorCode::Code_OutOfMemory;
    }
    else if(error_code) {
        m_code = DefErrorCode::Code_IllegalFile;
    }
    else {
        unsigned char buf[1024];
        size_t done = 0;
        auto code = Mpg123::mpg123_read(m_hMpg123, buf, 1024, &done);
        code = 0;
    }
}

/// <summary>
/// Releases this instance.
/// CALMp3AudioStream 释放
/// </summary>
/// <returns></returns>
auto WrapAL::CALMp3AudioStream::Release() noexcept -> uint32_t {
    return this->release_helper([this]() noexcept { 
        this->~CALMp3AudioStream();
        WrapALAudioEngine.configure->SmallFree(this);
    });
}

/// <summary>
/// Seeks the specified off.
/// 定位
/// </summary>
/// <param name="off">The off.</param>
/// <param name="method">The method.</param>
/// <returns></returns>
auto WrapAL::CALMp3AudioStream::Seek(int32_t off, Move method) noexcept -> uint32_t {
    // 不用移动
    if (!(off == 0 && method == Move_Current)) {
        off_t pos_in_sample = off / m_audioFormat.nBlockAlign;
        Mpg123::mpg123_seek(m_hMpg123, pos_in_sample, SEEK_SET);
    }
    return uint32_t(Mpg123::mpg123_tell(m_hMpg123));
}

/// <summary>
/// Reads the next.
/// 读取下一部分
/// </summary>
/// <param name="len">The length.</param>
/// <param name="buf">The buf.</param>
/// <returns></returns>
auto WrapAL::CALMp3AudioStream::ReadNext(uint32_t len, void* buf) noexcept -> uint32_t {
    size_t real_size = 0;
    if (auto i = Mpg123::mpg123_read(m_hMpg123, reinterpret_cast<unsigned char*>(buf), len, &real_size)) {
        if (i == MPG123_ERR || i > 0) {
            m_code = DefErrorCode::Code_DecodeError;
#ifndef NDEBUG
            std::wprintf(L"Code_DecodeError\r\n");
#endif
        }
    }
    return uint32_t(real_size);
}

/// <summary>
/// Definitions the create audio stream.
/// 默认音频流创建函数
/// </summary>
/// <param name="audio_format">The audio_format.</param>
/// <param name="file_stream">The file_stream.</param>
/// <param name="error_info">The error_info.</param>
/// <returns></returns>
auto WrapAL::DefCreateAudioStream(
    EncodingFormat audio_format, 
    IALFileStream* file_stream, 
    wchar_t error_info[WrapAL::ErrorInfoLength]
    ) noexcept -> XALAudioStream* {
    bool lenok = int(audio_format) < int(EncodingFormat::Format_UserDefined);
    assert(lenok && file_stream && error_info && "bad arguments");
    XALAudioStream* astream = nullptr;
    switch (audio_format)
    {
    case WrapAL::EncodingFormat::Format_Wave:
        astream = CALWavAudioStream::Create(file_stream);
        break;
    case WrapAL::EncodingFormat::Format_OggVorbis:
        astream = CALOggAudioStream::Create(file_stream);
        break;
    case WrapAL::EncodingFormat::Format_Mpg123:
        astream = CALMp3AudioStream::Create(file_stream);
        break;
    default:
        // 格式不支持
        std::swprintf(error_info, ErrorInfoLength, L"Unsupported Format : 0x%08X", audio_format);
        return nullptr;
    }
    // OOM
    if (!astream) {
        CALAudioEngine::FormatErrorOOM(error_info, __FUNCTION__);
    }
    // ERROR
    else if (static_cast<CALBasicAudioStream*>(astream)->GetErrorCode()
        != DefErrorCode::Code_Ok) {
        astream->GetLastErrorInfo(error_info);
    }
    return astream;
}


/// <summary>
/// Creates the audio stream.
/// 创建音频流
/// </summary>
/// <param name="format">The format.</param>
/// <param name="stream">The stream.</param>
/// <returns></returns>
auto WrapAL::CALDefConfigure::CreateAudioStream(
    EncodingFormat format, IALFileStream* stream
    ) noexcept -> XALAudioStream* {
    return WrapAL::DefCreateAudioStream(format, stream, m_szLastError);
}

/// <summary>
/// Gets the last error information.
/// 获取最近一次错误
/// </summary>
/// <param name="info">The information.</param>
/// <returns></returns>
auto WrapAL::CALDefConfigure::GetLastErrorInfo(wchar_t info[]) noexcept -> bool {
    if (*m_szLastError) {
        std::wcscpy(info, m_szLastError);
        return true;
    }
    return false;
}

/// <summary>
/// Gets the libmpg123 path.
/// 获取默认libmpg123位置
/// </summary>
/// <param name="path">The path.</param>
/// <returns></returns>
auto WrapAL::CALDefConfigure::GetLibmpg123Path(wchar_t path[]) noexcept -> void {
#if 0
    path += ::GetCurrentDirectoryW(MAX_PATH, path);
    const auto* __restrict real = L"\\libmpg123.dll";
    while ((*path = *real)) { ++path; ++real; }
#else
    path[0] = 0;
#endif
}


/// <summary>
/// Outputs the error.
/// </summary>
/// <param name="err">The error.</param>
/// <returns></returns>
void WrapAL::CALDefConfigure::OutputError(const wchar_t* err) noexcept {
    ::MessageBoxW(nullptr, err, L"Error!", MB_ICONERROR);
}

/// <summary>
/// Smalls the alloc.
/// </summary>
/// <param name="length">The length.</param>
/// <returns></returns>
void*WrapAL::CALDefConfigure::SmallAlloc(size_t length) noexcept {
    return std::malloc(length);
}

/// <summary>
/// Smalls the free.
/// </summary>
/// <param name="ptr">The PTR.</param>
/// <returns></returns>
void WrapAL::CALDefConfigure::SmallFree(void* ptr) noexcept {
    std::free(ptr);
}

#endif

// wrapal namespace
namespace WrapAL {
    // small single object
    class CALSingleSmallAlloc {
    public:
        // nothrow new 
        auto operator new (size_t size, std::nothrow_t) noexcept ->void* {
            assert(size < WrapAL::SmallSpaceThreshold);
            return WrapALAudioEngine.configure->SmallAlloc(size);
        }
        // delete
        auto operator delete(void* address) noexcept ->void {
            return WrapALAudioEngine.configure->SmallFree(address);
        }
        // throw new []
        auto operator new (size_t size) noexcept ->void* = delete;
        // throw new []
        auto operator new[](size_t size) noexcept ->void* = delete;
        // delete []
        void operator delete[](void*, size_t size) noexcept = delete;
        // nothrow new []
        auto operator new[](size_t size, std::nothrow_t) noexcept ->void* = delete;
    };
    // 文件流
    class CALFileStream final : public IALFileStream, public CALSingleSmallAlloc {
    public:
        // OK?
        bool OK() noexcept { return m_hFile != INVALID_HANDLE_VALUE; }
    public:
        // release this
        auto AddRef() noexcept ->uint32_t override { return 1; };
        // release this
        auto Release() noexcept ->uint32_t override { delete this; return 0; };
        // seek stream in byte, return false if out of range
        auto Seek(int32_t pos, Move method) noexcept ->uint32_t override {
            // tell
            if (method == Move_Current && pos == 0) return m_cOffset;
            // sad
            if (this->OK()) {
                ::SetFilePointer(m_hFile, pos, nullptr, method);
                switch (method)
                {
                case WrapAL::IALStream::Move_Begin:
                    m_cOffset = pos;
                    break;
                case WrapAL::IALStream::Move_Current:
                {
                    auto now = int64_t(m_cOffset) + int64_t(pos);
                    if (now < 0) now = 0;
                    m_cOffset = uint32_t(now);
                }
                    break;
                case WrapAL::IALStream::Move_End:
                {
                    auto now = uint32_t(int64_t(m_cLength) + int64_t(pos));
                    if (now < 0) now = 0;
                    m_cOffset = uint32_t(now);
                }
                    break;
                }
                // over?
                if (m_cOffset > m_cLength) m_cOffset = m_cLength;
                return m_cOffset;
            }
            else {
                assert(!"abort");
                return m_cOffset;
            }
        }
        // read stream, return false if end of file/stream
        auto ReadNext(uint32_t len, void* buf) noexcept ->uint32_t override {
            if (this->OK()) {
                DWORD read = 0;
                ::ReadFile(m_hFile, buf, len, &read, nullptr);
                m_cOffset += read;
                if (m_cOffset > m_cLength) {
                    m_cOffset = m_cLength;
                }
                return read;
            }
            else {
                assert(!"abort");
                return 0;
            }
        }
        // get total size in byte in 32-bit(sorry for file over 4GB :) )
        auto GetSizeInByte() noexcept ->uint32_t override { return m_cLength; }
    public:
        // ctor
        CALFileStream(const wchar_t* file_name) noexcept;
        // dtor
        ~CALFileStream() noexcept { if (this->OK()) ::CloseHandle(m_hFile); m_hFile = nullptr; }
    private:
        // file handle
        HANDLE              m_hFile = INVALID_HANDLE_VALUE;
        // file length
        uint32_t            m_cLength = 0;
        // file offset now
        uint32_t            m_cOffset = 0;
    };
    // 从文件创建流
    auto CALAudioEngine::CreatStreamFromFile(const wchar_t * file_name) noexcept -> IALFileStream* {
        return new (std::nothrow) CALFileStream(file_name);
    }
    // CALFileStream 构造函数
    WrapAL::CALFileStream::CALFileStream(const wchar_t* file_name) noexcept {
        assert(file_name && "bad argument");
        m_hFile = ::CreateFileW(
            file_name,
            GENERIC_READ, 
            FILE_SHARE_READ, 
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
            );
        if (m_hFile == INVALID_HANDLE_VALUE) {
            WrapALAudioEngine.OutputErrorFoF(__FUNCTION__, file_name);
            return;
        }
        m_cLength = ::GetFileSize(m_hFile, nullptr);
    }
    // IStream!
#ifdef WRAPAL_COM_ISTREAM_SUPPORT
    // 流流
    /*class CALStreamStream final : public IALStream {
    public:
        // release this
        auto Release() noexcept ->int32_t override { delete this; return 0; };
        // seek stream in byte, return false if out of range
        auto Seek(int32_t pos, Move method) noexcept ->bool override;
        // read stream, return false if end of file/stream
        auto ReadNext(uint32_t, uint8_t*) noexcept ->bool override;
        // get total size in byte in 32-bit(sorry for file over 4GB :) )
        auto GetSizeInByte() noexcept ->uint32_t override;
    public:
        // ctor
        CALStreamStream(IStream* stream) noexcept;
        // dtor
        ~CALStreamStream() noexcept { WrapAL::SafeRelease(m_pStream); }
    private:
        // file stream
        IStream*                m_pStream = nullptr;
    };
    // 从流创建流
    auto CALAudioEngine::CreatStreamFromStream(IStream * stream) noexcept -> IALStream* {
        return new (std::nothrow) CALStreamStream(stream);
    }*/
#endif
}

// 初始化
void WrapAL::Mpg123::Init(HMODULE hModule) noexcept {
    assert(hModule); if (!hModule) return;
    load_func(Mpg123::mpg123_init, hModule, "mpg123_init");
    load_func(Mpg123::mpg123_exit, hModule, "mpg123_exit");
    load_func(Mpg123::mpg123_new, hModule, "mpg123_new");
    load_func(Mpg123::mpg123_delete, hModule, "mpg123_delete");
    load_func(Mpg123::mpg123_read, hModule, "mpg123_read");
    load_func(Mpg123::mpg123_seek, hModule, "mpg123_seek");
    load_func(Mpg123::mpg123_tell, hModule, "mpg123_tell");
    load_func(Mpg123::mpg123_length, hModule, "mpg123_length");
    load_func(Mpg123::mpg123_format, hModule, "mpg123_format");
    load_func(Mpg123::mpg123_getformat, hModule, "mpg123_getformat");
    load_func(Mpg123::mpg123_format_none, hModule, "mpg123_format_none");
    load_func(Mpg123::mpg123_open_handle, hModule, "mpg123_open_handle");
    load_func(Mpg123::mpg123_replace_reader_handle, hModule, "mpg123_replace_reader_handle");
}


// 初始化静态变量
#define InitStaticVar(v)  decltype(v) v = nullptr
InitStaticVar(WrapAL::Mpg123::mpg123_init);
InitStaticVar(WrapAL::Mpg123::mpg123_exit);
InitStaticVar(WrapAL::Mpg123::mpg123_new);
InitStaticVar(WrapAL::Mpg123::mpg123_delete);
InitStaticVar(WrapAL::Mpg123::mpg123_read);
InitStaticVar(WrapAL::Mpg123::mpg123_seek);
InitStaticVar(WrapAL::Mpg123::mpg123_tell);
InitStaticVar(WrapAL::Mpg123::mpg123_length);
InitStaticVar(WrapAL::Mpg123::mpg123_format);
InitStaticVar(WrapAL::Mpg123::mpg123_getformat);
InitStaticVar(WrapAL::Mpg123::mpg123_format_none);
InitStaticVar(WrapAL::Mpg123::mpg123_open_handle);
InitStaticVar(WrapAL::Mpg123::mpg123_replace_reader_handle);



// 单例类
WrapAL::CALAudioEngine WrapALAudioEngine;