#pragma once

namespace imlottie {

struct VDebug {
    template<typename Args>
    VDebug &operator<<(const Args &) { return *this; }
};

#define vDebug VDebug()
#define vWarning VDebug()
#define vCritical VDebug()

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

}