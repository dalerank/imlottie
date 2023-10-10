#pragma once

namespace imlottie {

struct VDebug {
    template<typename Args>
    VDebug &operator<<(const Args &) { return *this; }
};

#define vDebug VDebug()
#define vWarning VDebug()
#define vCritical VDebug()

using uint = uint32_t;
using ushort = uint16_t;
using uchar = uint8_t;

#ifndef __has_attribute
# define __has_attribute(x) 0
#endif /* !__has_attribute */

#if __has_attribute(unused)
# define V_UNUSED __attribute__((__unused__))
#else
# define V_UNUSED
#endif /* V_UNUSED */

#if __has_attribute(warn_unused_result)
# define V_REQUIRED_RESULT __attribute__((__warn_unused_result__))
#else
# define V_REQUIRED_RESULT
#endif /* V_REQUIRED_RESULT */

#define V_CONSTEXPR constexpr
#define V_NOTHROW noexcept

#if __GNUC__ >= 7
#define VECTOR_FALLTHROUGH __attribute__ ((fallthrough));
#else
#define VECTOR_FALLTHROUGH
#endif

struct RjValue
{
    RjValue();
    ~RjValue();

    void SetNull();
    void SetBool(bool b);
    bool GetBool() const;
    void SetInt(int i);
    int GetInt() const;
    void SetUint(unsigned u);
    void SetInt64(int64_t i64);
    void SetUint64(uint64_t u64);
    void SetDouble(double d);
    double GetDouble() const;
    void SetFloat(float f);
    void SetString(const char* str,size_t length);
    const char* GetString() const;

    int GetType()  const;
    bool IsNull()   const;
    bool IsFalse()  const;
    bool IsTrue()   const;
    bool IsBool()   const;
    bool IsObject() const;
    bool IsArray()  const;
    bool IsNumber() const;
    bool IsInt()    const;
    bool IsUint()   const;
    bool IsInt64()  const;
    bool IsUint64() const;
    bool IsDouble() const;
    bool IsString() const;

    void* v_ = nullptr;
};

struct RjInsituStringStream
{
    RjInsituStringStream(char* str);
    void* ss_ = nullptr;
};

struct LookaheadParserHandlerBase
{
    virtual bool Null() = 0;
    virtual bool Bool(bool b) = 0;
    virtual bool Int(int i) = 0;
    virtual bool Uint(unsigned u) = 0;
    virtual bool Int64(int64_t i) = 0;
    virtual bool Uint64(int64_t u) = 0;
    virtual bool Double(double d) = 0;
    virtual bool RawNumber(const char *, unsigned length, bool) = 0;
    virtual bool String(const char *str, unsigned length, bool) = 0;
    virtual bool StartObject() = 0;
    virtual bool Key(const char *str, unsigned length, bool) = 0;
    virtual bool EndObject(unsigned) = 0;
    virtual bool StartArray() = 0;
    virtual bool EndArray(unsigned) = 0;
};

struct RjReader
{
    RjReader();

    void IterativeParseInit();
    bool HasParseError() const;
    bool IterativeParseNext(int parseFlags, RjInsituStringStream& ss_, LookaheadParserHandlerBase& handler);

    void* r_ = nullptr;
};


}