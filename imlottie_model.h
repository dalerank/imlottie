/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#pragma once

#include "rapidjson/rapidjson.h"
#include "imlottie_renderer.h"
#include "imlottie_rasterizer.h"

#include "imlottie_model.h"

#include <functional>

namespace imlottie {

enum class MatteType: uchar
{
    None = 0,
    Alpha = 1,
    AlphaInv,
    Luma,
    LumaInv
};

enum class LayerType: uchar
{
    Precomp = 0,
    Solid = 1,
    Image = 2,
    Null = 3,
    Shape = 4,
    Text = 5
};

// Naive way to implement std::variant
// refactor it when we move to c++17
// users should make sure proper combination
// of id and value are passed while creating the object.
class LOTVariant
{
public:
    using ValueFunc = std::function<float(const imlottie::FrameInfo &)>;
    using ColorFunc = std::function<imlottie::Color(const imlottie::FrameInfo &)>;
    using PointFunc = std::function<imlottie::Point(const imlottie::FrameInfo &)>;
    using SizeFunc = std::function<imlottie::Size(const imlottie::FrameInfo &)>;

    LOTVariant(imlottie::Property prop, const ValueFunc &v):mPropery(prop), mTag(Value)
    {
        construct(impl.valueFunc, v);
    }

    LOTVariant(imlottie::Property prop, ValueFunc &&v):mPropery(prop), mTag(Value)
    {
        moveConstruct(impl.valueFunc, std::move(v));
    }

    LOTVariant(imlottie::Property prop, const ColorFunc &v):mPropery(prop), mTag(Color)
    {
        construct(impl.colorFunc, v);
    }

    LOTVariant(imlottie::Property prop, ColorFunc &&v):mPropery(prop), mTag(Color)
    {
        moveConstruct(impl.colorFunc, std::move(v));
    }

    LOTVariant(imlottie::Property prop, const PointFunc &v):mPropery(prop), mTag(Point)
    {
        construct(impl.pointFunc, v);
    }

    LOTVariant(imlottie::Property prop, PointFunc &&v):mPropery(prop), mTag(Point)
    {
        moveConstruct(impl.pointFunc, std::move(v));
    }

    LOTVariant(imlottie::Property prop, const SizeFunc &v):mPropery(prop), mTag(Size)
    {
        construct(impl.sizeFunc, v);
    }

    LOTVariant(imlottie::Property prop, SizeFunc &&v):mPropery(prop), mTag(Size)
    {
        moveConstruct(impl.sizeFunc, std::move(v));
    }

    imlottie::Property property() const { return mPropery; }

    const ColorFunc& color() const
    {
        assert(mTag == Color);
        return impl.colorFunc;
    }

    const ValueFunc& value() const
    {
        assert(mTag == Value);
        return impl.valueFunc;
    }

    const PointFunc& point() const
    {
        assert(mTag == Point);
        return impl.pointFunc;
    }

    const SizeFunc& size() const
    {
        assert(mTag == Size);
        return impl.sizeFunc;
    }

    LOTVariant() = default;
    ~LOTVariant() noexcept {Destroy();}
    LOTVariant(const LOTVariant& other) { Copy(other);}
    LOTVariant(LOTVariant&& other) noexcept { Move(std::move(other));}
    LOTVariant& operator=(LOTVariant&& other) { Destroy(); Move(std::move(other)); return *this;}
    LOTVariant& operator=(const LOTVariant& other) { Destroy(); Copy(other); return *this;}
private:
    template <typename T>
    void construct(T& member, const T& val)
    {
        new (&member) T(val);
    }

    template <typename T>
    void moveConstruct(T& member, T&& val)
    {
        new (&member) T(std::move(val));
    }

    void Move(LOTVariant&& other)
    {
        switch (other.mTag) {
        case Type::Value:
            moveConstruct(impl.valueFunc, std::move(other.impl.valueFunc));
            break;
        case Type::Color:
            moveConstruct(impl.colorFunc, std::move(other.impl.colorFunc));
            break;
        case Type::Point:
            moveConstruct(impl.pointFunc, std::move(other.impl.pointFunc));
            break;
        case Type::Size:
            moveConstruct(impl.sizeFunc, std::move(other.impl.sizeFunc));
            break;
        default:
            break;
        }
        mTag = other.mTag;
        mPropery = other.mPropery;
        other.mTag = MonoState;
    }

    void Copy(const LOTVariant& other)
    {
        switch (other.mTag) {
        case Type::Value:
            construct(impl.valueFunc, other.impl.valueFunc);
            break;
        case Type::Color:
            construct(impl.colorFunc, other.impl.colorFunc);
            break;
        case Type::Point:
            construct(impl.pointFunc, other.impl.pointFunc);
            break;
        case Type::Size:
            construct(impl.sizeFunc, other.impl.sizeFunc);
            break;
        default:
            break;
        }
        mTag = other.mTag;
        mPropery = other.mPropery;
    }

    void Destroy()
    {
        switch(mTag) {
        case MonoState: {
            break;
        }
        case Value: {
            impl.valueFunc.~ValueFunc();
            break;
        }
        case Color: {
            impl.colorFunc.~ColorFunc();
            break;
        }
        case Point: {
            impl.pointFunc.~PointFunc();
            break;
        }
        case Size: {
            impl.sizeFunc.~SizeFunc();
            break;
        }
        }
    }

    enum Type {MonoState, Value, Color, Point , Size};
    imlottie::Property mPropery;
    Type              mTag{MonoState};
    union details{
      ColorFunc   colorFunc;
      ValueFunc   valueFunc;
      PointFunc   pointFunc;
      SizeFunc    sizeFunc;
      details(){}
      ~details(){}
    }impl;
};

class LOTCompositionData;
class LOTLayerData;
class LOTTransformData;
class LOTShapeGroupData;
class LOTShapeData;
class LOTRectData;
class LOTEllipseData;
class LOTTrimData;
class LOTRepeaterData;
class LOTFillData;
class LOTStrokeData;
class LOTGroupData;
class LOTGFillData;
class LOTGStrokeData;
class LottieShapeData;
class LOTPolystarData;
class LOTMaskData;

struct LOTModelStat
{
    uint16_t precompLayerCount{0};
    uint16_t solidLayerCount{0};
    uint16_t shapeLayerCount{0};
    uint16_t imageLayerCount{0};
    uint16_t nullLayerCount{0};

};

template <typename T>
struct LOTKeyFrameValue {
    T mStartValue;
    T mEndValue;
    T value(float t) const { return lerp(mStartValue, mEndValue, t); }
    float angle(float ) const { return 0;}
};

class LottieColor
{
public:
    LottieColor() = default;
    LottieColor(float red, float green , float blue):r(red), g(green),b(blue){}
    VColor toColor(float a=1){ return VColor(uchar(255 * r),
                                             uchar(255 * g),
                                             uchar(255 * b),
                                             uchar(255 * a));}
    friend inline LottieColor operator+(const LottieColor &c1, const LottieColor &c2);
    friend inline LottieColor operator-(const LottieColor &c1, const LottieColor &c2);
public:
    float r{1};
    float g{1};
    float b{1};
};

class LOTKeyPath{
public:
    LOTKeyPath(const std::string &keyPath);
    bool matches(const std::string &key, uint depth);
    uint nextDepth(const std::string key, uint depth);
    bool fullyResolvesTo(const std::string key, uint depth);

    bool propagate(const std::string key, uint depth) {
        return skip(key) ? true : (depth < size()) || (mKeys[depth] == "**");
    }
    bool skip(const std::string &key) const { return key == "__";}
private:
    bool isGlobstar(uint depth) const {return mKeys[depth] == "**";}
    bool isGlob(uint depth) const {return mKeys[depth] == "*";}
    bool endsWithGlobstar() const { return mKeys.back() == "**"; }
    size_t size() const {return mKeys.size() - 1;}
private:
    std::vector<std::string> mKeys;
};


class LOTFilter
{
public:
    void addValue(LOTVariant &value)
    {
        uint index = static_cast<uint>(value.property());
        if (mBitset.test(index)) {
            std::replace_if(mFilters.begin(),
                            mFilters.end(),
                            [&value](const LOTVariant &e) {return e.property() == value.property();},
                            value);
        } else {
            mBitset.set(index);
            mFilters.push_back(value);
        }
    }

    void removeValue(LOTVariant &value)
    {
        uint index = static_cast<uint>(value.property());
        if (mBitset.test(index)) {
            mBitset.reset(index);
            mFilters.erase(std::remove_if(mFilters.begin(),
                                          mFilters.end(),
                                          [&value](const LOTVariant &e) {return e.property() == value.property();}),
                           mFilters.end());
        }
    }
    bool hasFilter(imlottie::Property prop) const
    {
        return mBitset.test(static_cast<uint>(prop));
    }
    LottieColor color(imlottie::Property prop, int frame) const
    {
        imlottie::FrameInfo info(frame);
        imlottie::Color col = data(prop).color()(info);
        return LottieColor(col.r(), col.g(), col.b());
    }
    VPointF point(imlottie::Property prop, int frame) const
    {
        imlottie::FrameInfo info(frame);
        imlottie::Point pt = data(prop).point()(info);
        return VPointF(pt.x(), pt.y());
    }
    VSize scale(imlottie::Property prop, int frame) const
    {
        imlottie::FrameInfo info(frame);
        imlottie::Size sz = data(prop).size()(info);
        return VSize(sz.w(), sz.h());
    }
    float opacity(imlottie::Property prop, int frame) const
    {
        imlottie::FrameInfo info(frame);
        float val = data(prop).value()(info);
        return val/100;
    }
    float value(imlottie::Property prop, int frame) const
    {
        imlottie::FrameInfo info(frame);
        return data(prop).value()(info);
    }
private:
    const LOTVariant& data(imlottie::Property prop) const
    {
        auto result = std::find_if(mFilters.begin(),
                                   mFilters.end(),
                                   [prop](const LOTVariant &e){return e.property() == prop;});
        return *result;
    }
    std::bitset<32>            mBitset{0};
    std::vector<LOTVariant>    mFilters;
};

template<typename T>
class LOTKeyFrame
{
public:
    float progress(int frameNo) const {
        return mInterpolator ? mInterpolator->value((frameNo - mStartFrame) / (mEndFrame - mStartFrame)) : 0;
    }
    T value(int frameNo) const {
        return mValue.value(progress(frameNo));
    }
    float angle(int frameNo) const {
        return mValue.angle(progress(frameNo));
    }

public:
    float                 mStartFrame{0};
    float                 mEndFrame{0};
    VInterpolator        *mInterpolator{nullptr};
    LOTKeyFrameValue<T>   mValue;
};


template<typename T>
class LOTAnimInfo
{
public:
    T value(int frameNo) const {
        if (mKeyFrames.front().mStartFrame >= frameNo)
            return mKeyFrames.front().mValue.mStartValue;
        if(mKeyFrames.back().mEndFrame <= frameNo)
            return mKeyFrames.back().mValue.mEndValue;

        for(const auto &keyFrame : mKeyFrames) {
            if (frameNo >= keyFrame.mStartFrame && frameNo < keyFrame.mEndFrame)
                return keyFrame.value(frameNo);
        }
        return T();
    }

    float angle(int frameNo) const {
        if ((mKeyFrames.front().mStartFrame >= frameNo) ||
            (mKeyFrames.back().mEndFrame <= frameNo) )
            return 0;

        for(const auto &keyFrame : mKeyFrames) {
            if (frameNo >= keyFrame.mStartFrame && frameNo < keyFrame.mEndFrame)
                return keyFrame.angle(frameNo);
        }
        return 0;
    }

    bool changed(int prevFrame, int curFrame) const {
        auto first = mKeyFrames.front().mStartFrame;
        auto last = mKeyFrames.back().mEndFrame;

        return !((first > prevFrame  && first > curFrame) ||
                 (last < prevFrame  && last < curFrame));
    }

public:
    std::vector<LOTKeyFrame<T>>    mKeyFrames;
};

template<typename T>
class LOTAnimatable
{
public:
    LOTAnimatable() { construct(impl.mValue, {}); }
    explicit LOTAnimatable(T value) { construct(impl.mValue, std::move(value)); }

    const LOTAnimInfo<T>& animation() const {return *(impl.mAnimInfo.get());}
    const T& value() const {return impl.mValue;}

    LOTAnimInfo<T>& animation()
    {
        if (mStatic) {
            destroy();
            construct(impl.mAnimInfo, std::make_unique<LOTAnimInfo<T>>());
            mStatic = false;
        }
        return *(impl.mAnimInfo.get());
    }

    T& value()
    {
        assert(mStatic);
        return impl.mValue;
    }

    LOTAnimatable(LOTAnimatable &&other) noexcept {
        if (!other.mStatic) {
            construct(impl.mAnimInfo, std::move(other.impl.mAnimInfo));
            mStatic = false;
        } else {
            construct(impl.mValue, std::move(other.impl.mValue));
            mStatic = true;
        }
    }
    // delete special member functions
    LOTAnimatable(const LOTAnimatable &) = delete;
    LOTAnimatable& operator=(const LOTAnimatable&) = delete;
    LOTAnimatable& operator=(LOTAnimatable&&) = delete;

    ~LOTAnimatable() {destroy();}

    bool isStatic() const {return mStatic;}

    T value(int frameNo) const {
        return isStatic() ? value() : animation().value(frameNo);
    }

    float angle(int frameNo) const {
        return isStatic() ? 0 : animation().angle(frameNo);
    }

    bool changed(int prevFrame, int curFrame) const {
        return isStatic() ? false : animation().changed(prevFrame, curFrame);
    }
private:
    template <typename Tp>
    void construct(Tp& member, Tp&& val)
    {
        new (&member) Tp(std::move(val));
    }

    void destroy() {
        if (mStatic) {
            impl.mValue.~T();
        } else {
            using std::unique_ptr;
            impl.mAnimInfo.~unique_ptr<LOTAnimInfo<T>>();
        }
    }
    union details {
        std::unique_ptr<LOTAnimInfo<T>>   mAnimInfo;
        T                                 mValue;
        details(){};
        details(const details&) = delete;
        details(details&&) = delete;
        details& operator=(details&&) = delete;
        details& operator=(const details&) = delete;
        ~details(){};
    }impl;
    bool                                 mStatic{true};
};


template <typename T>
class LOTProxyModel
{
public:
    LOTProxyModel(T *model): _modelData(model) {}
    LOTFilter& filter() {return mFilter;}
    const char* name() const {return _modelData->name();}
    LottieColor color(int frame) const
    {
        if (mFilter.hasFilter(imlottie::Property::StrokeColor)) {
            return mFilter.color(imlottie::Property::StrokeColor, frame);
        }
        return _modelData->color(frame);
    }
    float opacity(int frame) const
    {
        if (mFilter.hasFilter(imlottie::Property::StrokeOpacity)) {
            return mFilter.opacity(imlottie::Property::StrokeOpacity, frame);
        }
        return _modelData->opacity(frame);
    }
    float strokeWidth(int frame) const
    {
        if (mFilter.hasFilter(imlottie::Property::StrokeWidth)) {
            return mFilter.value(imlottie::Property::StrokeWidth, frame);
        }
        return _modelData->strokeWidth(frame);
    }
    float miterLimit() const {return _modelData->miterLimit();}
    CapStyle capStyle() const {return _modelData->capStyle();}
    JoinStyle joinStyle() const {return _modelData->joinStyle();}
    bool hasDashInfo() const { return _modelData->hasDashInfo();}
    void getDashInfo(int frameNo, std::vector<float>& result) const {
        return _modelData->getDashInfo(frameNo, result);
    }

private:
    T                         *_modelData;
    LOTFilter                  mFilter;
};

class LOTDataVisitor;
class LOTData
{
public:
    enum class Type :unsigned char {
        Composition = 1,
        Layer,
        ShapeGroup,
        Transform,
        Fill,
        Stroke,
        GFill,
        GStroke,
        Rect,
        Ellipse,
        Shape,
        Polystar,
        Trim,
        Repeater
    };

    explicit LOTData(LOTData::Type type):mPtr(nullptr)
    {
        mData._type = type;
        mData._static = true;
        mData._shortString = true;
        mData._hidden = false;
    }
    ~LOTData() { if (!shortString() && mPtr) free(mPtr); }
    LOTData(const LOTData&) = delete;
    LOTData& operator =(const LOTData&) = delete;

    void setStatic(bool value) { mData._static = value;}
    bool isStatic() const {return mData._static;}
    bool hidden() const {return mData._hidden;}
    void setHidden(bool value) {mData._hidden = value;}
    void setType(LOTData::Type type) {mData._type = type;}
    LOTData::Type type() const { return mData._type;}
    void setName(const char *name)
    {
        if (name) {
            auto len = strlen(name);
            if (len < maxShortStringLength) {
                setShortString(true);
                strncpy ( mData._buffer, name, len+1);
            } else {
                setShortString(false);
                mPtr = strdup(name);
            }

        }
    }
    const char* name() const {return shortString() ? mData._buffer : mPtr;}
private:
    static constexpr unsigned char maxShortStringLength = 14;
    void setShortString(bool value) {mData._shortString = value;}
    bool shortString() const {return mData._shortString;}
    struct Data{
        char           _buffer[maxShortStringLength];
        LOTData::Type  _type;
        bool           _static      : 1;
        bool           _hidden      : 1;
        bool           _shortString : 1;
    };
    union {
        Data  mData;
        char *mPtr{nullptr};
    };
};

class LOTFillData : public LOTData
{
public:
    LOTFillData():LOTData(LOTData::Type::Fill){}
    LottieColor color(int frameNo) const {return mColor.value(frameNo);}
    float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0f;}
    FillRule fillRule() const {return mFillRule;}
public:
    FillRule                       mFillRule{FillRule::Winding}; /* "r" */
    bool                           mEnabled{true}; /* "fillEnabled" */
    LOTAnimatable<LottieColor>     mColor;   /* "c" */
    LOTAnimatable<float>           mOpacity{100};  /* "o" */
};

template <>
class LOTProxyModel<LOTFillData>
{
public:
    LOTProxyModel(LOTFillData *model): _modelData(model) {}
    LOTFilter& filter() {return mFilter;}
    const char* name() const {return _modelData->name();}
    LottieColor color(int frame) const
    {
        if (mFilter.hasFilter(imlottie::Property::FillColor)) {
            return mFilter.color(imlottie::Property::FillColor, frame);
        }
        return _modelData->color(frame);
    }
    float opacity(int frame) const
    {
        if (mFilter.hasFilter(imlottie::Property::FillOpacity)) {
            return mFilter.opacity(imlottie::Property::FillOpacity, frame);
        }
        return _modelData->opacity(frame);
    }
    FillRule fillRule() const {return _modelData->fillRule();}
private:
    LOTFillData               *_modelData;
    LOTFilter                  mFilter;
};

class LOTGroupData: public LOTData
{
public:
    explicit LOTGroupData(LOTData::Type  type):LOTData(type){}
public:
    std::vector<LOTData *>  mChildren;
    LOTTransformData       *mTransform{nullptr};
};

struct TransformDataExtra
{
    LOTAnimatable<float>     m3DRx{0};
    LOTAnimatable<float>     m3DRy{0};
    LOTAnimatable<float>     m3DRz{0};
    LOTAnimatable<float>     mSeparateX{0};
    LOTAnimatable<float>     mSeparateY{0};
    bool                     mSeparate{false};
    bool                     m3DData{false};
};

struct TransformData
{
    VMatrix matrix(int frameNo, bool autoOrient = false) const;
    float opacity(int frameNo) const { return mOpacity.value(frameNo)/100.0f; }
    void createExtraData()
    {
        if (!mExtra) mExtra = std::make_unique<TransformDataExtra>();
    }
    LOTAnimatable<float>                   mRotation{0};  /* "r" */
    LOTAnimatable<VPointF>                 mScale{{100, 100}};     /* "s" */
    LOTAnimatable<VPointF>                 mPosition;  /* "p" */
    LOTAnimatable<VPointF>                 mAnchor;    /* "a" */
    LOTAnimatable<float>                   mOpacity{100};   /* "o" */
    std::unique_ptr<TransformDataExtra>    mExtra;
};

class LOTTransformData : public LOTData
{
public:
    LOTTransformData():LOTData(LOTData::Type::Transform){}
    void set(TransformData* data, bool staticFlag)
    {
        setStatic(staticFlag);
        if (isStatic()) {
            new (&impl.mStaticData) static_data(data->matrix(0), data->opacity(0));
        } else {
            impl.mData = data;
        }
    }
    VMatrix matrix(int frameNo, bool autoOrient = false) const
    {
        if (isStatic()) return impl.mStaticData.mMatrix;
        return impl.mData->matrix(frameNo, autoOrient);
    }
    float opacity(int frameNo) const
    {
        if (isStatic()) return impl.mStaticData.mOpacity;
        return impl.mData->opacity(frameNo);
    }
    LOTTransformData(const LOTTransformData&) = delete;
    LOTTransformData(LOTTransformData&&) = delete;
    LOTTransformData& operator=(LOTTransformData&) = delete;
    LOTTransformData& operator=(LOTTransformData&&) = delete;
    ~LOTTransformData() {destroy();}

private:
    void destroy() {
        if (isStatic()) {
            impl.mStaticData.~static_data();
        }
    }
    struct static_data {
        static_data(VMatrix &&m, float opacity):
            mOpacity(opacity), mMatrix(std::move(m)){}
        float    mOpacity;
        VMatrix  mMatrix;
    };
    union details {
        TransformData     *mData{nullptr};
        static_data        mStaticData;
        details(){};
        details(const details&) = delete;
        details(details&&) = delete;
        details& operator=(details&&) = delete;
        details& operator=(const details&) = delete;
        ~details(){};
    }impl;
};

template <>
class LOTProxyModel<LOTGroupData>
{
public:
    LOTProxyModel(LOTGroupData *model = nullptr): _modelData(model) {}
    bool hasModel() const { return _modelData ? true : false; }
    LOTFilter& filter() {return mFilter;}
    const char* name() const {return _modelData->name();}
    LOTTransformData* transform() const { return _modelData->mTransform; }
    VMatrix matrix(int frame) const {
        VMatrix mS, mR, mT;
        if (mFilter.hasFilter(imlottie::Property::TrScale)) {
            VSize s = mFilter.scale(imlottie::Property::TrScale, frame);
            mS.scale(s.width() / 100.0f, s.height() / 100.0f);
        }
        if (mFilter.hasFilter(imlottie::Property::TrRotation)) {
            mR.rotate(mFilter.value(imlottie::Property::TrRotation, frame));
        }
        if (mFilter.hasFilter(imlottie::Property::TrPosition)) {
            mT.translate(mFilter.point(imlottie::Property::TrPosition, frame));
        }

        return _modelData->mTransform->matrix(frame) * mS * mR * mT;
    }
private:
    LOTGroupData               *_modelData;
    LOTFilter                  mFilter;
};

using Marker = std::tuple<std::string, int , int>;
using LayerInfo = Marker;
enum class LottieBlendMode: uchar
{
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    OverLay = 3
};

class LOTLayerData;
struct LOTAsset
{
    enum class Type : unsigned char{
        Precomp,
        Image,
        Char
    };
    bool isStatic() const {return mStatic;}
    void setStatic(bool value) {mStatic = value;}
    VBitmap  bitmap() const {return mBitmap;}
    void loadImageData(std::string data);
    void loadImagePath(std::string Path);
    Type                                      mAssetType{Type::Precomp};
    bool                                      mStatic{true};
    std::string                               mRefId; // ref id
    std::vector<LOTData *>                    mLayers;
    // image asset data
    int                                       mWidth{0};
    int                                       mHeight{0};
    VBitmap                                   mBitmap;
};

class LottieShapeData
{
public:
    void reserve(size_t size) {
        mPoints.reserve(mPoints.size() + size);
    }
    static void lerp(const LottieShapeData& start, const LottieShapeData& end, float t, VPath& result)
    {
        result.reset();
        auto size = std::min(start.mPoints.size(), end.mPoints.size());
        /* reserve exact memory requirement at once
        * ptSize = size + 1(size + close)
        * elmSize = size/3 cubic + 1 move + 1 close
        */
        result.reserve(size + 1 , size/3 + 2);
        result.moveTo(start.mPoints[0] + t * (end.mPoints[0] - start.mPoints[0]));
        for (size_t i = 1 ; i < size; i+=3) {
            result.cubicTo(start.mPoints[i] + t * (end.mPoints[i] - start.mPoints[i]),
                           start.mPoints[i+1] + t * (end.mPoints[i+1] - start.mPoints[i+1]),
                           start.mPoints[i+2] + t * (end.mPoints[i+2] - start.mPoints[i+2]));
        }
        if (start.mClosed) result.close();
    }
    void toPath(VPath& path) const {
        path.reset();

        if (mPoints.empty()) return;

        auto size = mPoints.size();
        auto points = mPoints.data();
        /* reserve exact memory requirement at once
        * ptSize = size + 1(size + close)
        * elmSize = size/3 cubic + 1 move + 1 close
        */
        path.reserve(size + 1 , size/3 + 2);
        path.moveTo(points[0]);
        for (size_t i = 1 ; i < size; i+=3) {
            path.cubicTo(points[i], points[i+1], points[i+2]);
        }
        if (mClosed)
            path.close();
    }
public:
    std::vector<VPointF> mPoints;
    bool                 mClosed = false;   /* "c" */
};

class LOTAnimatableShape : public LOTAnimatable<LottieShapeData>
{
public:
    void updatePath(int frameNo, VPath &path) const {
        if (isStatic()) {
            value().toPath(path);
        } else {
            const auto &vec = animation().mKeyFrames;
            if (vec.front().mStartFrame >= frameNo)
                return vec.front().mValue.mStartValue.toPath(path);
            if(vec.back().mEndFrame <= frameNo)
                return vec.back().mValue.mEndValue.toPath(path);

            for(const auto &keyFrame : vec) {
                if (frameNo >= keyFrame.mStartFrame && frameNo < keyFrame.mEndFrame) {
                    LottieShapeData::lerp(keyFrame.mValue.mStartValue,
                                          keyFrame.mValue.mEndValue,
                                          keyFrame.progress(frameNo),
                                          path);
                }
            }
        }
    }
};

class LOTMaskData
{
public:
    enum class Mode {
        None,
        Add,
        Substarct,
        Intersect,
        Difference
    };
    float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0f;}
    bool isStatic() const {return mIsStatic;}
public:
    LOTAnimatableShape                mShape;
    LOTAnimatable<float>              mOpacity{100};
    bool                              mInv{false};
    bool                              mIsStatic{true};
    LOTMaskData::Mode                 mMode;
};

class LOTCompositionData;
struct ExtraLayerData
{
    LottieColor                mSolidColor;
    std::string                mPreCompRefId;
    LOTAnimatable<float>       mTimeRemap;  /* "tm" */
    LOTCompositionData        *mCompRef{nullptr};
    LOTAsset                  *mAsset{nullptr};
    std::vector<LOTMaskData *>  mMasks;
};

class LOTLayerData : public LOTGroupData
{
public:
    LOTLayerData():LOTGroupData(LOTData::Type::Layer){}
    bool hasPathOperator() const noexcept {return mHasPathOperator;}
    bool hasGradient() const noexcept {return mHasGradient;}
    bool hasMask() const noexcept {return mHasMask;}
    bool hasRepeater() const noexcept {return mHasRepeater;}
    int id() const noexcept{ return mId;}
    int parentId() const noexcept{ return mParentId;}
    bool hasParent() const noexcept {return mParentId != -1;}
    int inFrame() const noexcept{return mInFrame;}
    int outFrame() const noexcept{return mOutFrame;}
    int startFrame() const noexcept{return mStartFrame;}
    LottieColor solidColor() const noexcept{return mExtra->mSolidColor;}
    bool autoOrient() const noexcept{return mAutoOrient;}
    int timeRemap(int frameNo) const;
    VSize layerSize() const {return mLayerSize;}
    bool precompLayer() const {return mLayerType == LayerType::Precomp;}
    VMatrix matrix(int frameNo) const
    {
        return mTransform ? mTransform->matrix(frameNo, autoOrient()) : VMatrix{};
    }
    float opacity(int frameNo) const
    {
        return mTransform ? mTransform->opacity(frameNo) : 1.0f;
    }
    LOTAsset* asset() const
    {
        return (mExtra && mExtra->mAsset) ? mExtra->mAsset : nullptr;
    }
public:
    ExtraLayerData* extra()
    {
        if (!mExtra) mExtra = std::make_unique<ExtraLayerData>();
        return mExtra.get();
    }
    MatteType            mMatteType{MatteType::None};
    LayerType            mLayerType{LayerType::Null};
    LottieBlendMode      mBlendMode{LottieBlendMode::Normal};
    bool                 mHasPathOperator{false};
    bool                 mHasMask{false};
    bool                 mHasRepeater{false};
    bool                 mHasGradient{false};
    bool                 mAutoOrient{false};
    VSize                mLayerSize;
    int                  mParentId{-1}; // Lottie the id of the parent in the composition
    int                  mId{-1};  // Lottie the group id  used for parenting.
    float                mTimeStreatch{1.0f};
    int                  mInFrame{0};
    int                  mOutFrame{0};
    int                  mStartFrame{0};
    std::unique_ptr<ExtraLayerData> mExtra{nullptr};
};

class LOTCompositionData : public LOTData
{
public:
    LOTCompositionData():LOTData(LOTData::Type::Composition){}
    std::vector<LayerInfo> layerInfoList() const;
    const std::vector<Marker> &markers() const { return  mMarkers;}
    double duration() const {
        return frameDuration() / frameRate(); // in second
    }
    size_t frameAtPos(double pos) const {
        if (pos < 0) pos = 0;
        if (pos > 1) pos = 1;
        return size_t(pos * frameDuration());
    }
    long frameAtTime(double timeInSec) const {
        return long(frameAtPos(timeInSec / duration()));
    }
    size_t totalFrame() const {return mEndFrame - mStartFrame;}
    long frameDuration() const {return mEndFrame - mStartFrame -1;}
    float frameRate() const {return mFrameRate;}
    long startFrame() const {return mStartFrame;}
    long endFrame() const {return mEndFrame;}
    VSize size() const {return mSize;}
    void processRepeaterObjects();
    void updateStats();
public:
    std::string          mVersion;
    VSize                mSize;
    long                 mStartFrame{0};
    long                 mEndFrame{0};
    float                mFrameRate{60};
    LottieBlendMode      mBlendMode{LottieBlendMode::Normal};
    LOTLayerData        *mRootLayer{nullptr};
    std::unordered_map<std::string,
        LOTAsset*>    mAssets;

    std::vector<Marker>     mMarkers;
    VArenaAlloc             mArenaAlloc{2048};
    LOTModelStat            mStats;
};

class LOTModel
{
public:
    bool  isStatic() const {return mRoot->isStatic();}
    VSize size() const {return mRoot->size();}
    double duration() const {return mRoot->duration();}
    size_t totalFrame() const {return mRoot->totalFrame();}
    size_t frameDuration() const {return mRoot->frameDuration();}
    double frameRate() const {return mRoot->frameRate();}
    size_t startFrame() const {return mRoot->startFrame();}
    size_t endFrame() const {return mRoot->endFrame();}
    size_t frameAtPos(double pos) const {return mRoot->frameAtPos(pos);}
    std::vector<LayerInfo> layerInfoList() const { return mRoot->layerInfoList();}
    const std::vector<Marker> &markers() const { return mRoot->markers();}
public:
    std::shared_ptr<LOTCompositionData> mRoot;
};

class LottieParserImpl;
class LottieParser {
public:
    ~LottieParser();
    LottieParser(char* str, const char *dir_path);
    std::shared_ptr<LOTModel> model();
private:
   std::unique_ptr<LottieParserImpl>  d;
};

inline LottieColor operator-(const LottieColor &c1, const LottieColor &c2)
{
    return LottieColor(c1.r - c2.r, c1.g - c2.g, c1.b - c2.b);
}
inline LottieColor operator+(const LottieColor &c1, const LottieColor &c2)
{
    return LottieColor(c1.r + c2.r, c1.g + c2.g, c1.b + c2.b);
}

inline const LottieColor operator*(const LottieColor &c, float m)
{ return LottieColor(c.r*m, c.g*m, c.b*m); }

inline const LottieColor operator*(float m, const LottieColor &c)
{ return LottieColor(c.r*m, c.g*m, c.b*m); }

template<typename T>
inline T lerp(const T& start, const T& end, float t)
{
    return start + t * (end - start);
}

template <>
struct LOTKeyFrameValue<VPointF>
{
    VPointF mStartValue;
    VPointF mEndValue;
    VPointF mInTangent;
    VPointF mOutTangent;
    bool    mPathKeyFrame = false;

    VPointF value(float t) const {
        if (mPathKeyFrame) {
            /*
             * position along the path calcualated
             * using bezier at progress length (t * bezlen)
             */
            VBezier b = VBezier::fromPoints(mStartValue, mStartValue + mOutTangent,
                                       mEndValue + mInTangent, mEndValue);
            return b.pointAt(b.tAtLength(t * b.length()));

        }
        return lerp(mStartValue, mEndValue, t);
    }

    float angle(float t) const {
        if (mPathKeyFrame) {
            VBezier b = VBezier::fromPoints(mStartValue, mStartValue + mOutTangent,
                                       mEndValue + mInTangent, mEndValue);
            return b.angleAt(b.tAtLength(t * b.length()));
        }
        return 0;
    }
};

class LOTShapeGroupData : public LOTGroupData
{
public:
    LOTShapeGroupData():LOTGroupData(LOTData::Type::ShapeGroup){}
};

/**
 * TimeRemap has the value in time domain(in sec)
 * To get the proper mapping first we get the mapped time at the current frame Number
 * then we need to convert mapped time to frame number using the composition time line
 * Ex: at frame 10 the mappend time is 0.5(500 ms) which will be convert to frame number
 * 30 if the frame rate is 60. or will result to frame number 15 if the frame rate is 30.
 */
inline int LOTLayerData::timeRemap(int frameNo) const
{
    /*
     * only consider startFrame() when there is no timeRemap.
     * when a layer has timeremap bodymovin updates the startFrame()
     * of all child layer so we don't have to take care of it.
     */
    if (!mExtra || mExtra->mTimeRemap.isStatic())
        frameNo = frameNo - startFrame();
    else
        frameNo = mExtra->mCompRef->frameAtTime(mExtra->mTimeRemap.value(frameNo));
    /* Apply time streatch if it has any.
     * Time streatch is just a factor by which the animation will speedup or slow
     * down with respect to the overal animation.
     * Time streach factor is already applied to the layers inFrame and outFrame.
     * @TODO need to find out if timestreatch also affects the in and out frame of the
     * child layers or not. */
    return int(frameNo / mTimeStreatch);
}

struct LOTDashProperty
{
    std::vector<LOTAnimatable<float>> mData;
    bool empty() const {return mData.empty();}
    size_t size() const {return mData.size();}
    bool isStatic() const {
        for(const auto &elm : mData)
            if (!elm.isStatic()) return false;
        return true;
    }
    void getDashInfo(int frameNo, std::vector<float>& result) const;
};

class LOTStrokeData : public LOTData
{
public:
    LOTStrokeData():LOTData(LOTData::Type::Stroke){}
    LottieColor color(int frameNo) const {return mColor.value(frameNo);}
    float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0f;}
    float strokeWidth(int frameNo) const {return mWidth.value(frameNo);}
    CapStyle capStyle() const {return mCapStyle;}
    JoinStyle joinStyle() const {return mJoinStyle;}
    float miterLimit() const{return mMiterLimit;}
    bool  hasDashInfo() const {return !mDash.empty();}
    void getDashInfo(int frameNo, std::vector<float>& result) const
    {
        return mDash.getDashInfo(frameNo, result);
    }
public:
    LOTAnimatable<LottieColor>        mColor;      /* "c" */
    LOTAnimatable<float>              mOpacity{100};    /* "o" */
    LOTAnimatable<float>              mWidth{0};      /* "w" */
    CapStyle                          mCapStyle{CapStyle::Flat};   /* "lc" */
    JoinStyle                         mJoinStyle{JoinStyle::Miter};  /* "lj" */
    float                             mMiterLimit{0}; /* "ml" */
    LOTDashProperty                   mDash;
    bool                              mEnabled{true}; /* "fillEnabled" */
};

class LottieGradient
{
public:
    friend inline LottieGradient operator+(const LottieGradient &g1, const LottieGradient &g2);
    friend inline LottieGradient operator-(const LottieGradient &g1, const LottieGradient &g2);
    friend inline LottieGradient operator*(float m, const LottieGradient &g);
public:
    std::vector<float>    mGradient;
};

inline LottieGradient operator+(const LottieGradient &g1, const LottieGradient &g2)
{
    if (g1.mGradient.size() != g2.mGradient.size())
        return g1;

    LottieGradient newG;
    newG.mGradient = g1.mGradient;

    auto g2It = g2.mGradient.begin();
    for(auto &i : newG.mGradient) {
        i = i + *g2It;
        g2It++;
    }

    return newG;
}

inline LottieGradient operator-(const LottieGradient &g1, const LottieGradient &g2)
{
    if (g1.mGradient.size() != g2.mGradient.size())
        return g1;
    LottieGradient newG;
    newG.mGradient = g1.mGradient;

    auto g2It = g2.mGradient.begin();
    for(auto &i : newG.mGradient) {
        i = i - *g2It;
        g2It++;
    }

    return newG;
}

inline LottieGradient operator*(float m, const LottieGradient &g)
{
    LottieGradient newG;
    newG.mGradient = g.mGradient;

    for(auto &i : newG.mGradient) {
        i = i * m;
    }
    return newG;
}



class LOTGradient : public LOTData
{
public:
    explicit LOTGradient(LOTData::Type  type):LOTData(type){}
    inline float opacity(int frameNo) const {return mOpacity.value(frameNo)/100.0f;}
    void update(std::unique_ptr<VGradient> &grad, int frameNo);

private:
    void populate(VGradientStops &stops, int frameNo);
public:
    int                                 mGradientType{1};        /* "t" Linear=1 , Radial = 2*/
    LOTAnimatable<VPointF>              mStartPoint;          /* "s" */
    LOTAnimatable<VPointF>              mEndPoint;            /* "e" */
    LOTAnimatable<float>                mHighlightLength{0};     /* "h" */
    LOTAnimatable<float>                mHighlightAngle{0};      /* "a" */
    LOTAnimatable<float>                mOpacity{100};             /* "o" */
    LOTAnimatable<LottieGradient>       mGradient;            /* "g" */
    int                                 mColorPoints{-1};
    bool                                mEnabled{true};      /* "fillEnabled" */
};

class LOTGFillData : public LOTGradient
{
public:
    LOTGFillData():LOTGradient(LOTData::Type::GFill){}
    FillRule fillRule() const {return mFillRule;}
public:
    FillRule                       mFillRule{FillRule::Winding}; /* "r" */
};

class LOTGStrokeData : public LOTGradient
{
public:
    LOTGStrokeData():LOTGradient(LOTData::Type::GStroke){}
    float width(int frameNo) const {return mWidth.value(frameNo);}
    CapStyle capStyle() const {return mCapStyle;}
    JoinStyle joinStyle() const {return mJoinStyle;}
    float miterLimit() const{return mMiterLimit;}
    bool  hasDashInfo() const {return !mDash.empty();}
    void getDashInfo(int frameNo, std::vector<float>& result) const
    {
        return mDash.getDashInfo(frameNo, result);
    }
public:
    LOTAnimatable<float>           mWidth;       /* "w" */
    CapStyle                       mCapStyle{CapStyle::Flat};    /* "lc" */
    JoinStyle                      mJoinStyle{JoinStyle::Miter};   /* "lj" */
    float                          mMiterLimit{0};  /* "ml" */
    LOTDashProperty                mDash;
};

class LOTPath : public LOTData
{
public:
    explicit LOTPath(LOTData::Type  type):LOTData(type){}
    VPath::Direction direction() {
        return (mDirection == 3) ?
               VPath::Direction::CCW : VPath::Direction::CW;
    }
public:
    int                                    mDirection{1};
};

class LOTShapeData : public LOTPath
{
public:
    LOTShapeData():LOTPath(LOTData::Type::Shape){}
public:
    LOTAnimatableShape    mShape;
};

class LOTRectData : public LOTPath
{
public:
    LOTRectData():LOTPath(LOTData::Type::Rect){}
public:
    LOTAnimatable<VPointF>    mPos;
    LOTAnimatable<VPointF>    mSize;
    LOTAnimatable<float>      mRound{0};
};

class LOTEllipseData : public LOTPath
{
public:
    LOTEllipseData():LOTPath(LOTData::Type::Ellipse){}
public:
    LOTAnimatable<VPointF>   mPos;
    LOTAnimatable<VPointF>   mSize;
};

class LOTPolystarData : public LOTPath
{
public:
    enum class PolyType {
        Star = 1,
        Polygon = 2
    };
    LOTPolystarData():LOTPath(LOTData::Type::Polystar){}
public:
    LOTPolystarData::PolyType     mPolyType{PolyType::Polygon};
    LOTAnimatable<VPointF>        mPos;
    LOTAnimatable<float>          mPointCount{0};
    LOTAnimatable<float>          mInnerRadius{0};
    LOTAnimatable<float>          mOuterRadius{0};
    LOTAnimatable<float>          mInnerRoundness{0};
    LOTAnimatable<float>          mOuterRoundness{0};
    LOTAnimatable<float>          mRotation{0};
};

class LOTTrimData : public LOTData
{
public:
    struct Segment {
        float start{0};
        float end{0};
        Segment() = default;
        explicit Segment(float s, float e):start(s), end(e) {}
    };
    enum class TrimType {
        Simultaneously,
        Individually
    };
    LOTTrimData():LOTData(LOTData::Type::Trim){}
    /*
     * if start > end vector trims the path as a loop ( 2 segment)
     * if start < end vector trims the path without loop ( 1 segment).
     * if no offset then there is no loop.
     */
    Segment segment(int frameNo) const {
        float start = mStart.value(frameNo)/100.0f;
        float end = mEnd.value(frameNo)/100.0f;
        float offset = std::fmod(mOffset.value(frameNo), 360.0f)/ 360.0f;

        float diff = std::abs(start - end);
        if (vCompare(diff, 0.0f)) return Segment(0, 0);
        if (vCompare(diff, 1.0f)) return Segment(0, 1);

        if (offset > 0) {
            start += offset;
            end += offset;
            if (start <= 1 && end <=1) {
                return noloop(start, end);
            } else if (start > 1 && end > 1) {
                return noloop(start - 1, end - 1);
            } else {
                return (start > 1) ?
                            loop(start - 1 , end) : loop(start , end - 1);
            }
        } else {
            start += offset;
            end   += offset;
            if (start >= 0 && end >= 0) {
                return noloop(start, end);
            } else if (start < 0 && end < 0) {
                return noloop(1 + start, 1 + end);
            } else {
                return (start < 0) ?
                            loop(1 + start, end) : loop(start , 1 + end);
            }
        }
    }
    LOTTrimData::TrimType type() const {return mTrimType;}
private:
    Segment noloop(float start, float end) const{
        assert(start >= 0);
        assert(end >= 0);
        Segment s;
        s.start = std::min(start, end);
        s.end = std::max(start, end);
        return s;
    }
    Segment loop(float start, float end) const{
        assert(start >= 0);
        assert(end >= 0);
        Segment s;
        s.start = std::max(start, end);
        s.end = std::min(start, end);
        return s;
    }
public:
    LOTAnimatable<float>             mStart{0};
    LOTAnimatable<float>             mEnd{0};
    LOTAnimatable<float>             mOffset{0};
    LOTTrimData::TrimType            mTrimType{TrimType::Simultaneously};
};

class LOTRepeaterTransform
{
public:
    VMatrix matrix(int frameNo, float multiplier) const;
    float startOpacity(int frameNo) const { return mStartOpacity.value(frameNo)/100;}
    float endOpacity(int frameNo) const { return mEndOpacity.value(frameNo)/100;}
    bool isStatic() const
    {
        return mRotation.isStatic() &&
               mScale.isStatic() &&
               mPosition.isStatic() &&
               mAnchor.isStatic() &&
               mStartOpacity.isStatic() &&
               mEndOpacity.isStatic();
    }
public:
    LOTAnimatable<float>          mRotation{0};  /* "r" */
    LOTAnimatable<VPointF>        mScale{{100, 100}};     /* "s" */
    LOTAnimatable<VPointF>        mPosition;  /* "p" */
    LOTAnimatable<VPointF>        mAnchor;    /* "a" */
    LOTAnimatable<float>          mStartOpacity{100}; /* "so" */
    LOTAnimatable<float>          mEndOpacity{100};   /* "eo" */
};

class LOTRepeaterData : public LOTData
{
public:
    LOTRepeaterData():LOTData(LOTData::Type::Repeater){}
    LOTShapeGroupData *content() const { return mContent ? mContent : nullptr; }
    void setContent(LOTShapeGroupData *content) {mContent = content;}
    int maxCopies() const { return int(mMaxCopies);}
    float copies(int frameNo) const {return mCopies.value(frameNo);}
    float offset(int frameNo) const {return mOffset.value(frameNo);}
    bool processed() const {return mProcessed;}
    void markProcessed() {mProcessed = true;}
public:
    LOTShapeGroupData*                      mContent{nullptr};
    LOTRepeaterTransform                    mTransform;
    LOTAnimatable<float>                    mCopies{0};
    LOTAnimatable<float>                    mOffset{0};
    float                                   mMaxCopies{0.0};
    bool                                    mProcessed{false};
};

class LottieLoader
{
public:
   static void configureModelCacheSize(size_t cacheSize);
   bool load(const std::string &filePath, bool cachePolicy);
   bool loadFromData(std::string &&jsonData, const std::string &key,
                     const std::string &resourcePath, bool cachePolicy);
   std::shared_ptr<LOTModel> model();
private:
   std::shared_ptr<LOTModel>    mModel;
};

enum class DirtyFlagBit : uchar
{
    None   = 0x00,
    Matrix = 0x01,
    Alpha  = 0x02,
    All    = (Matrix | Alpha)
};

class LOTLayerItem;
class LOTMaskItem;
class VDrawable;

class LOTDrawable : public VDrawable
{
public:
    void sync();
public:
    std::unique_ptr<LOTNode>  mCNode{nullptr};

    ~LOTDrawable() {
        if (mCNode && mCNode->mGradient.stopPtr)
            free(mCNode->mGradient.stopPtr);
    }
};

class LOTCompItem
{
public:
    explicit LOTCompItem(LOTModel *model);
    bool update(int frameNo, const VSize &size, bool keepAspectRatio);
    VSize size() const { return mViewSize;}
    void buildRenderTree();
    const LOTLayerNode * renderTree()const;
    bool render(const imlottie::Surface &surface);
    void setValue(const std::string &keypath, LOTVariant &value);
private:
    VBitmap                                     mSurface;
    VMatrix                                     mScaleMatrix;
    VSize                                       mViewSize;
    LOTCompositionData                         *mCompData{nullptr};
    LOTLayerItem                               *mRootLayer{nullptr};
    VArenaAlloc                                 mAllocator{2048};
    int                                         mCurFrameNo;
    bool                                        mKeepAspectRatio{true};
};

class LOTLayerMaskItem;

class LOTClipperItem
{
public:
    explicit LOTClipperItem(VSize size): mSize(size){}
    void update(const VMatrix &matrix);
    void preprocess(const VRect &clip);
    VRle rle(const VRle& mask);
public:
    VSize                    mSize;
    VPath                    mPath;
    VRle                     mMaskedRle;
    VRasterizer              mRasterizer;
    bool                     mRasterRequest{false};
};

typedef vFlag<DirtyFlagBit> DirtyFlag;

struct LOTCApiData
{
    LOTCApiData();
    LOTLayerNode                  mLayer;
    std::vector<LOTMask>          mMasks;
    std::vector<LOTLayerNode *>   mLayers;
    std::vector<LOTNode *>        mCNodeList;
};

template< class T>
class VSpan
{
public:
    using reference         = T &;
    using pointer           = T *;
    using const_pointer     = T const *;
    using const_reference   = T const &;
    using index_type        = size_t;

    using iterator          = pointer;
    using const_iterator    = const_pointer;

    VSpan() = default;
    VSpan(pointer data, index_type size):_data(data), _size(size){}

    constexpr pointer data() const noexcept {return _data; }
    constexpr index_type size() const noexcept {return _size; }
    constexpr bool empty() const noexcept { return size() == 0 ;}
    constexpr iterator begin() const noexcept { return data(); }
    constexpr iterator end() const noexcept {return data() + size() ;}
    constexpr const_iterator cbegin() const noexcept {return  data();}
    constexpr const_iterator cend() const noexcept { return data() + size();}
    constexpr reference operator[]( index_type idx ) const { return *( data() + idx );}

private:
    pointer      _data{nullptr};
    index_type   _size{0};
};

using DrawableList = VSpan<VDrawable *>;

class LOTLayerItem
{
public:
    virtual ~LOTLayerItem() = default;
    LOTLayerItem& operator=(LOTLayerItem&&) noexcept = delete;
    LOTLayerItem(LOTLayerData *layerData);
    int id() const {return mLayerData->id();}
    int parentId() const {return mLayerData->parentId();}
    void setParentLayer(LOTLayerItem *parent){mParentLayer = parent;}
    void setComplexContent(bool value) { mComplexContent = value;}
    bool complexContent() const {return mComplexContent;}
    virtual void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha);
    VMatrix matrix(int frameNo) const;
    void preprocess(const VRect& clip);
    virtual DrawableList renderList(){ return {};}
    virtual void render(VPainter *painter, const VRle &mask, const VRle &matteRle);
    bool hasMatte() { if (mLayerData->mMatteType == MatteType::None) return false; return true; }
    MatteType matteType() const { return mLayerData->mMatteType;}
    bool visible() const;
    virtual void buildLayerNode();
    LOTLayerNode& clayer() {return mCApiData->mLayer;}
    std::vector<LOTLayerNode *>& clayers() {return mCApiData->mLayers;}
    std::vector<LOTMask>& cmasks() {return mCApiData->mMasks;}
    std::vector<LOTNode *>& cnodes() {return mCApiData->mCNodeList;}
    const char* name() const {return mLayerData->name();}
    virtual bool resolveKeyPath(LOTKeyPath &keyPath, uint depth, LOTVariant &value);
    VBitmap& bitmap() {return mRenderBuffer;}
protected:
    virtual void preprocessStage(const VRect& clip) = 0;
    virtual void updateContent() = 0;
    inline VMatrix combinedMatrix() const {return mCombinedMatrix;}
    inline int frameNo() const {return mFrameNo;}
    inline float combinedAlpha() const {return mCombinedAlpha;}
    inline bool isStatic() const {return mLayerData->isStatic();}
    float opacity(int frameNo) const {return mLayerData->opacity(frameNo);}
    inline DirtyFlag flag() const {return mDirtyFlag;}
    bool skipRendering() const {return (!visible() || vIsZero(combinedAlpha()));}
protected:
    std::unique_ptr<LOTLayerMaskItem>           mLayerMask;
    LOTLayerData                               *mLayerData{nullptr};
    LOTLayerItem                               *mParentLayer{nullptr};
    VMatrix                                     mCombinedMatrix;
    VBitmap                                     mRenderBuffer;
    float                                       mCombinedAlpha{0.0};
    int                                         mFrameNo{-1};
    DirtyFlag                                   mDirtyFlag{DirtyFlagBit::All};
    bool                                        mComplexContent{false};
    std::unique_ptr<LOTCApiData>                mCApiData;
};

class LOTCompLayerItem: public LOTLayerItem
{
public:
    explicit LOTCompLayerItem(LOTLayerData *layerData, VArenaAlloc* allocator);

    void render(VPainter *painter, const VRle &mask, const VRle &matteRle) final;
    void buildLayerNode() final;
    bool resolveKeyPath(LOTKeyPath &keyPath, uint depth, LOTVariant &value) override;
protected:
    void preprocessStage(const VRect& clip) final;
    void updateContent() final;
private:
    void renderHelper(VPainter *painter, const VRle &mask, const VRle &matteRle);
    void renderMatteLayer(VPainter *painter, const VRle &inheritMask, const VRle &matteRle,
                          LOTLayerItem *layer, LOTLayerItem *src);
private:
    std::vector<LOTLayerItem*>            mLayers;
    std::unique_ptr<LOTClipperItem>       mClipper;
};

class LOTSolidLayerItem: public LOTLayerItem
{
public:
    explicit LOTSolidLayerItem(LOTLayerData *layerData);
    void buildLayerNode() final;
    DrawableList renderList() final;
protected:
    void preprocessStage(const VRect& clip) final;
    void updateContent() final;
private:
    LOTDrawable                  mRenderNode;
    VDrawable                   *mDrawableList{nullptr}; //to work with the Span api
};

class LOTContentItem;
class LOTContentGroupItem;
class LOTShapeLayerItem: public LOTLayerItem
{
public:
    explicit LOTShapeLayerItem(LOTLayerData *layerData, VArenaAlloc* allocator);
    DrawableList renderList() final;
    void buildLayerNode() final;
    bool resolveKeyPath(LOTKeyPath &keyPath, uint depth, LOTVariant &value) override;
protected:
    void preprocessStage(const VRect& clip) final;
    void updateContent() final;
    std::vector<VDrawable *>             mDrawableList;
    LOTContentGroupItem                 *mRoot{nullptr};
};

class LOTNullLayerItem: public LOTLayerItem
{
public:
    explicit LOTNullLayerItem(LOTLayerData *layerData);
protected:
    void preprocessStage(const VRect&) final {}
    void updateContent() final;
};

class LOTImageLayerItem: public LOTLayerItem
{
public:
    explicit LOTImageLayerItem(LOTLayerData *layerData);
    void buildLayerNode() final;
    DrawableList renderList() final;
protected:
    void preprocessStage(const VRect& clip) final;
    void updateContent() final;
private:
    LOTDrawable                  mRenderNode;
    VTexture                     mTexture;
    VDrawable                   *mDrawableList{nullptr}; //to work with the Span api
};

class LOTMaskItem
{
public:
    explicit LOTMaskItem(LOTMaskData *data): mData(data){}
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag);
    LOTMaskData::Mode maskMode() const { return mData->mMode;}
    VRle rle();
    void preprocess(const VRect &clip);
public:
    LOTMaskData             *mData{nullptr};
    VPath                    mLocalPath;
    VPath                    mFinalPath;
    VRasterizer              mRasterizer;
    float                    mCombinedAlpha{0};
    bool                     mRasterRequest{false};
};

/*
* Handels mask property of a layer item
*/
class LOTLayerMaskItem
{
public:
    explicit LOTLayerMaskItem(LOTLayerData *layerData);
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag);
    bool isStatic() const {return mStatic;}
    VRle maskRle(const VRect &clipRect);
    void preprocess(const VRect &clip);
public:
    std::vector<LOTMaskItem>   mMasks;
    VRle                       mRle;
    bool                       mStatic{true};
    bool                       mDirty{true};
};

class LOTPathDataItem;
class LOTPaintDataItem;
class LOTTrimItem;

enum class ContentType : uchar
{
    Unknown,
    Group,
    Path,
    Paint,
    Trim
};

class LOTContentGroupItem;
class LOTContentItem
{
public:
    virtual ~LOTContentItem() = default;
    LOTContentItem& operator=(LOTContentItem&&) noexcept = delete;
    virtual void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag) = 0;   virtual void renderList(std::vector<VDrawable *> &){}
    virtual bool resolveKeyPath(LOTKeyPath &, uint, LOTVariant &) {return false;}
    virtual ContentType type() const {return ContentType::Unknown;}
};

class LOTContentGroupItem: public LOTContentItem
{
public:
    LOTContentGroupItem() = default;
    explicit LOTContentGroupItem(LOTGroupData *data, VArenaAlloc* allocator);
    void addChildren(LOTGroupData *data, VArenaAlloc* allocator);
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag) override;
    void applyTrim();
    void processTrimItems(std::vector<LOTPathDataItem *> &list);
    void processPaintItems(std::vector<LOTPathDataItem *> &list);
    void renderList(std::vector<VDrawable *> &list) override;
    ContentType type() const final {return ContentType::Group;}
    const VMatrix & matrix() const { return mMatrix;}
    const char* name() const
    {
        static const char* TAG = "__";
        return mModel.hasModel() ? mModel.name() : TAG;
    }
    bool resolveKeyPath(LOTKeyPath &keyPath, uint depth, LOTVariant &value) override;
protected:
    std::vector<LOTContentItem*>   mContents;
    VMatrix                                        mMatrix;
private:
    LOTProxyModel<LOTGroupData> mModel;
};

class LOTPathDataItem : public LOTContentItem
{
public:
    LOTPathDataItem(bool staticPath): mStaticPath(staticPath){}
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag) final;
    ContentType type() const final {return ContentType::Path;}
    bool dirty() const {return mDirtyPath;}
    const VPath &localPath() const {return mTemp;}
    void finalPath(VPath& result);
    void updatePath(const VPath &path) {mTemp = path; mDirtyPath = true;}
    bool staticPath() const { return mStaticPath; }
    void setParent(LOTContentGroupItem *parent) {mParent = parent;}
    LOTContentGroupItem *parent() const {return mParent;}
protected:
    virtual void updatePath(VPath& path, int frameNo) = 0;
    virtual bool hasChanged(int prevFrame, int curFrame) = 0;
private:
    bool hasChanged(int frameNo) {
        int prevFrame = mFrameNo;
        mFrameNo = frameNo;
        if (prevFrame == -1) return true;
        if (mStaticPath ||
            (prevFrame == frameNo)) return false;
        return hasChanged(prevFrame, frameNo);
    }
    LOTContentGroupItem                    *mParent{nullptr};
    VPath                                   mLocalPath;
    VPath                                   mTemp;
    int                                     mFrameNo{-1};
    bool                                    mDirtyPath{true};
    bool                                    mStaticPath;
};

class LOTRectItem: public LOTPathDataItem
{
public:
    explicit LOTRectItem(LOTRectData *data);
protected:
    void updatePath(VPath& path, int frameNo) final;
    LOTRectData           *mData{nullptr};

    bool hasChanged(int prevFrame, int curFrame) final {
        return (mData->mPos.changed(prevFrame, curFrame) ||
                mData->mSize.changed(prevFrame, curFrame) ||
                mData->mRound.changed(prevFrame, curFrame));
    }
};

class LOTEllipseItem: public LOTPathDataItem
{
public:
    explicit LOTEllipseItem(LOTEllipseData *data);
private:
    void updatePath(VPath& path, int frameNo) final;
    LOTEllipseData           *mData{nullptr};
    bool hasChanged(int prevFrame, int curFrame) final {
        return (mData->mPos.changed(prevFrame, curFrame) ||
                mData->mSize.changed(prevFrame, curFrame));
    }
};

class LOTShapeItem: public LOTPathDataItem
{
public:
    explicit LOTShapeItem(LOTShapeData *data);
private:
    void updatePath(VPath& path, int frameNo) final;
    LOTShapeData             *mData{nullptr};
    bool hasChanged(int prevFrame, int curFrame) final {
        return mData->mShape.changed(prevFrame, curFrame);
    }
};

class LOTPolystarItem: public LOTPathDataItem
{
public:
    explicit LOTPolystarItem(LOTPolystarData *data);
private:
    void updatePath(VPath& path, int frameNo) final;
    LOTPolystarData             *mData{nullptr};

    bool hasChanged(int prevFrame, int curFrame) final {
        return (mData->mPos.changed(prevFrame, curFrame) ||
                mData->mPointCount.changed(prevFrame, curFrame) ||
                mData->mInnerRadius.changed(prevFrame, curFrame) ||
                mData->mOuterRadius.changed(prevFrame, curFrame) ||
                mData->mInnerRoundness.changed(prevFrame, curFrame) ||
                mData->mOuterRoundness.changed(prevFrame, curFrame) ||
                mData->mRotation.changed(prevFrame, curFrame));
    }
};



class LOTPaintDataItem : public LOTContentItem
{
public:
    LOTPaintDataItem(bool staticContent);
    void addPathItems(std::vector<LOTPathDataItem *> &list, size_t startOffset);
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag) override;
    void renderList(std::vector<VDrawable *> &list) final;
    ContentType type() const final {return ContentType::Paint;}
protected:
    virtual bool updateContent(int frameNo, const VMatrix &matrix, float alpha) = 0;
private:
    void updateRenderNode();
protected:
    std::vector<LOTPathDataItem *>   mPathItems;
    LOTDrawable                      mDrawable;
    VPath                            mPath;
    DirtyFlag                        mFlag;
    bool                             mStaticContent;
    bool                             mRenderNodeUpdate{true};
    bool                             mContentToRender{true};
};

class LOTFillItem : public LOTPaintDataItem
{
public:
    explicit LOTFillItem(LOTFillData *data);
protected:
    bool updateContent(int frameNo, const VMatrix &matrix, float alpha) final;
    bool resolveKeyPath(LOTKeyPath &keyPath, uint depth, LOTVariant &value) final;
private:
    LOTProxyModel<LOTFillData> mModel;
};

class LOTGFillItem : public LOTPaintDataItem
{
public:
    explicit LOTGFillItem(LOTGFillData *data);
protected:
    bool updateContent(int frameNo, const VMatrix &matrix, float alpha) final;
private:
    LOTGFillData                 *mData{nullptr};
    std::unique_ptr<VGradient>    mGradient;
};

class LOTStrokeItem : public LOTPaintDataItem
{
public:
    explicit LOTStrokeItem(LOTStrokeData *data);
protected:
    bool updateContent(int frameNo, const VMatrix &matrix, float alpha) final;
    bool resolveKeyPath(LOTKeyPath &keyPath, uint depth, LOTVariant &value) final;
private:
    LOTProxyModel<LOTStrokeData> mModel;
};

class LOTGStrokeItem : public LOTPaintDataItem
{
public:
    explicit LOTGStrokeItem(LOTGStrokeData *data);
protected:
    bool updateContent(int frameNo, const VMatrix &matrix, float alpha) final;
private:
    LOTGStrokeData               *mData{nullptr};
    std::unique_ptr<VGradient>    mGradient;
};

class LOTTrimItem : public LOTContentItem
{
public:
    explicit LOTTrimItem(LOTTrimData *data);
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag) final;
    ContentType type() const final {return ContentType::Trim;}
    void update();
    void addPathItems(std::vector<LOTPathDataItem *> &list, size_t startOffset);
private:
    bool pathDirty() const {
        for (auto &i : mPathItems) {
            if (i->dirty())
                return true;
        }
        return false;
    }
    struct Cache {
        int                     mFrameNo{-1};
        LOTTrimData::Segment    mSegment{};
    };
    Cache                            mCache;
    std::vector<LOTPathDataItem *>   mPathItems;
    LOTTrimData                     *mData{nullptr};
    VPathMesure                      mPathMesure;
    bool                             mDirty{true};
};

class LOTRepeaterItem : public LOTContentGroupItem
{
public:
    explicit LOTRepeaterItem(LOTRepeaterData *data, VArenaAlloc* allocator);
    void update(int frameNo, const VMatrix &parentMatrix, float parentAlpha, const DirtyFlag &flag) final;
    void renderList(std::vector<VDrawable *> &list) final;
private:
    LOTRepeaterData             *mRepeaterData{nullptr};
    bool                         mHidden{false};
    int                          mCopies{0};
};

} // namespace imlottie
