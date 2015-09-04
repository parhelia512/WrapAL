﻿#pragma once
/**
* Copyright (c) 2014-2015 dustpg   mailto:dustpg@gmail.com
*
* Permission is hereby granted, free of charge, to any person
* obtaining a copy of this software and associated documentation
* files (the "Software"), to deal in the Software without
* restriction, including without limitation the rights to use,
* copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following
* conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
* HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*/


// wrapal namespace
namespace WrapAL {
    // Stream for reading PCM data
    class XALPCMStream;
    // WrapAL Interface
    struct WRAPAL_NOVTABLE IALInterface {
        // release this
        virtual auto Release() noexcept->int32_t = 0;
    };
    // Stream Interface for read less than 4GB stream
    struct WRAPAL_NOVTABLE IALStream : IALInterface {
        // method to move
        enum Move : uint32_t { Move_Begin = 0, Move_Current, Move_End };
        // seek stream in byte, return current position
        virtual auto Seek(int32_t off, Move method = IALStream::Move_Begin) noexcept->uint32_t = 0;
        // read stream, return byte count read
        virtual auto ReadNext(uint32_t len, void* buf) noexcept->uint32_t = 0;
        // get total size in byte in 32-bit
        virtual auto GetSizeInByte() noexcept ->uint32_t = 0;
        // tell position
        auto Tell() noexcept { return this->Seek(int32_t(0), IALStream::Move_Current); }
    };
    // Read Only File Stream
    struct WRAPAL_NOVTABLE IALFileStream : IALStream { 
        // OK?
        virtual bool OK() noexcept = 0;
    };
    // Audio Configure
    struct WRAPAL_NOVTABLE IALConfigure : IALInterface {
    public:
        // auto update? return true if you want and must undef "WRAPAL_SAME_THREAD_UPDATE"
        virtual auto IsAutoUpdate() noexcept ->bool = 0;
        // alloc a small space less than WrapAL::SmallSpaceThreshold
        virtual auto SmallAlloc(size_t length) noexcept ->void* = 0;
        // free space that alloced via "SmallAlloc"
        virtual auto SmallFree(void* address) noexcept ->void = 0;
        // choose device, return index, if out of range, choose default device
        virtual auto ChooseDevice(const AudioDeviceInfo devices[/*count*/], UINT count/* <= DeviceMaxCount*/) noexcept ->UINT = 0;
        // create pcm audio stream from file stream, stream won't be null
        virtual auto CreatePCMStream(AudioFormat format, IALFileStream* stream) noexcept->XALPCMStream* =0;
        // get last error infomation, return false if no error
        virtual auto GetLastErrorInfo(wchar_t info[/*ErrorInfoLength*/])noexcept->bool = 0;
        // output error infomation
        virtual auto OutputError(const wchar_t*)noexcept->void = 0;
        // get message in runtime
        virtual auto GetRuntimeMessage(RuntimeMessage msg)noexcept->const wchar_t* = 0;
        // get the "libmpg123.dll" path
        virtual auto GetLibmpg123Path(wchar_t path[/*MAX_PATH*/])noexcept->void = 0;
    };
    // PCM Audio Stream, X for "Mixed but like a interface"
    class WRAPAL_NOVTABLE XALPCMStream : public IALStream {
    public:
        // get size in byte, final override
        auto GetSizeInByte() noexcept ->uint32_t override final { return m_cTotalSize; }
    public:
        // get last error infomation, return false if no error
        virtual auto GetLastErrorInfo(wchar_t info[/*ErrorInfoLength*/])noexcept->bool = 0;
        // recreate
        virtual auto Recreate(IALFileStream* stream)noexcept->void {
            WrapAL::SafeRelease(m_pFileStream);
            m_pFileStream = stream;
        }
    public:
        // ctor
        XALPCMStream(IALFileStream* stream) noexcept : m_pFileStream(stream) {};
        // dtor
        ~XALPCMStream() noexcept { WrapAL::SafeRelease(m_pFileStream); };
        // get format
        auto GetFormat() const noexcept -> const PCMFormat& { return m_pcmFormat; }
    protected:
        // audio file
        IALFileStream*          m_pFileStream = nullptr;
        // the format of pcm
        PCMFormat               m_pcmFormat = { 0 };
        // total size in byte
        uint32_t                m_cTotalSize = 0;
    };
}