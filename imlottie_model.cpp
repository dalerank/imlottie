/*
 * Thanks for Samsung Electronics for amazing rlottie library.
 *
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

#include "imlottie_model.h"

#include "rapidjson/document.h"
#include "rapidjson/stream.h"

#include <fstream>

using namespace rapidjson;

namespace imlottie {

RjValue::RjValue()
{
  v_ = new Value();
}

static Value& vcast(void* p) { return *(Value*)p; }
void RjValue::SetNull() { vcast(v_).SetNull(); }
void RjValue::SetBool(bool b) { vcast(v_).SetBool(b); }
void RjValue::SetInt(int i) { vcast(v_).SetInt(i); }
void RjValue::SetInt64(int64_t i) { vcast(v_).SetInt64(i); }
void RjValue::SetUint64(uint64_t i) { vcast(v_).SetUint64(i); }
void RjValue::SetUint(unsigned int i) { vcast(v_).SetUint(i); }
void RjValue::SetDouble(double i) { vcast(v_).SetDouble(i); }
void RjValue::SetString(const char* str,size_t length) { vcast(v_).SetString(str, static_cast<SizeType>(length)); }

RjValue::~RjValue()
{
	Value* d = (Value*)v_;
	delete d;
}

const char* RjValue::GetString() const { return vcast(v_).GetString(); }
int RjValue::GetInt() const { return vcast(v_).GetInt(); }
bool RjValue::GetBool() const { return vcast(v_).GetBool(); }
double RjValue::GetDouble() const { return vcast(v_).GetDouble(); }

int  RjValue::GetType()  const { return vcast(v_).GetType(); }
bool RjValue::IsNull()   const { return vcast(v_).IsNull(); }
bool RjValue::IsFalse()  const { return vcast(v_).IsFalse(); }
bool RjValue::IsTrue()   const { return vcast(v_).IsTrue(); }
bool RjValue::IsBool()   const { return vcast(v_).IsBool(); }
bool RjValue::IsObject() const { return vcast(v_).IsObject(); }
bool RjValue::IsArray()  const { return vcast(v_).IsArray(); }
bool RjValue::IsNumber() const { return vcast(v_).IsNumber(); }
bool RjValue::IsInt()    const { return vcast(v_).IsInt(); }
bool RjValue::IsUint()   const { return vcast(v_).IsUint(); }
bool RjValue::IsInt64()  const { return vcast(v_).IsInt64(); }
bool RjValue::IsUint64() const { return vcast(v_).IsUint64(); }
bool RjValue::IsDouble() const { return vcast(v_).IsDouble(); }
bool RjValue::IsString() const { return vcast(v_).IsString(); }

RjInsituStringStream::RjInsituStringStream(char* str)
{
  ss_ = new InsituStringStream(str);
}

RjReader::RjReader() { r_ = new Reader(); }

static Reader& rcast(void* p) { return *(Reader*)p; }
void RjReader::IterativeParseInit() { rcast(r_).IterativeParseInit(); }
bool RjReader::HasParseError() const { return rcast(r_).HasParseError(); }

bool RjReader::IterativeParseNext(int parseFlags, RjInsituStringStream& ss_, LookaheadParserHandlerBase& handler)
{
  if (parseFlags == (kParseDefaultFlags | kParseInsituFlag))
    return rcast(r_).IterativeParseNext<kParseDefaultFlags|kParseInsituFlag>(*(InsituStringStream*)(ss_.ss_), handler);
  else 
    return false;
}

class LookaheadParserHandler : public LookaheadParserHandlerBase {
public:
    bool Null()
    {
        st_ = kHasNull;
        v_.SetNull();
        return true;
    }
    bool Bool(bool b)
    {
        st_ = kHasBool;
        v_.SetBool(b);
        return true;
    }
    bool Int(int i)
    {
        st_ = kHasNumber;
        v_.SetInt(i);
        return true;
    }
    bool Uint(unsigned u)
    {
        st_ = kHasNumber;
        v_.SetUint(u);
        return true;
    }
    bool Int64(int64_t i)
    {
        st_ = kHasNumber;
        v_.SetInt64(i);
        return true;
    }
    bool Uint64(int64_t u)
    {
        st_ = kHasNumber;
        v_.SetUint64(u);
        return true;
    }
    bool Double(double d)
    {
        st_ = kHasNumber;
        v_.SetDouble(d);
        return true;
    }
    bool RawNumber(const char *, SizeType, bool) { return false; }
    bool String(const char *str, SizeType length, bool)
    {
        st_ = kHasString;
        v_.SetString(str, length);
        return true;
    }
    bool StartObject()
    {
        st_ = kEnteringObject;
        return true;
    }
    bool Key(const char *str, SizeType length, bool)
    {
        st_ = kHasKey;
        v_.SetString(str, length);
        return true;
    }
    bool EndObject(SizeType)
    {
        st_ = kExitingObject;
        return true;
    }
    bool StartArray()
    {
        st_ = kEnteringArray;
        return true;
    }
    bool EndArray(SizeType)
    {
        st_ = kExitingArray;
        return true;
    }

protected:
    explicit LookaheadParserHandler(char *str);

protected:
    enum LookaheadParsingState {
        kInit,
        kError,
        kHasNull,
        kHasBool,
        kHasNumber,
        kHasString,
        kHasKey,
        kEnteringObject,
        kExitingObject,
        kEnteringArray,
        kExitingArray
    };

    RjValue v_;
    LookaheadParsingState st_;
    RjReader r_;
    RjInsituStringStream  ss_;

    static const int parseFlags = 0 | 1;//kParseDefaultFlags | kParseInsituFlag;
};

class LottieParserImpl : public LookaheadParserHandler {
public:
    LottieParserImpl(char *str, const char *dir_path)
        : LookaheadParserHandler(str), mDirPath(dir_path) {}
    bool VerifyType();
    bool ParseNext();
public:
    VArenaAlloc& allocator() {return compRef->mArenaAlloc;}
    bool        EnterObject();
    bool        EnterArray();
    const char *NextObjectKey();
    bool        NextArrayValue();
    int         GetInt();
    double      GetDouble();
    const char *GetString();
    bool        GetBool();
    void        GetNull();

    void   SkipObject();
    void   SkipArray();
    void   SkipValue();
    RjValue *PeekValue();
    int PeekType() const;
    bool IsValid() { return st_ != kError; }

    void                  Skip(const char *key);
    LottieBlendMode       getBlendMode();
    CapStyle              getLineCap();
    JoinStyle             getLineJoin();
    FillRule              getFillRule();
    LOTTrimData::TrimType getTrimType();
    MatteType             getMatteType();
    LayerType             getLayerType();

    std::shared_ptr<LOTCompositionData> composition() const
    {
        return mComposition;
    }
    void                         parseComposition();
    void                         parseMarkers();
    void                         parseMarker();
    void                         parseAssets(LOTCompositionData *comp);
    LOTAsset*                    parseAsset();
    void                         parseLayers(LOTCompositionData *comp);
    LOTLayerData*                parseLayer();
    void                         parseMaskProperty(LOTLayerData *layer);
    void                         parseShapesAttr(LOTLayerData *layer);
    void                         parseObject(LOTGroupData *parent);
    LOTMaskData*                 parseMaskObject();
    LOTData*                     parseObjectTypeAttr();
    LOTData*                     parseGroupObject();
    LOTRectData*                 parseRectObject();
    LOTEllipseData*              parseEllipseObject();
    LOTShapeData*                parseShapeObject();
    LOTPolystarData*             parsePolystarObject();

    LOTTransformData*            parseTransformObject(bool ddd = false);
    LOTFillData*                 parseFillObject();
    LOTGFillData*                parseGFillObject();
    LOTStrokeData*               parseStrokeObject();
    LOTGStrokeData*              parseGStrokeObject();
    LOTTrimData*                 parseTrimObject();
    LOTRepeaterData*             parseReapeaterObject();

    void parseGradientProperty(LOTGradient *gradient, const char *key);

    VPointF parseInperpolatorPoint();

    void getValue(VPointF &pt);
    void getValue(float &fval);
    void getValue(LottieColor &color);
    void getValue(int &ival);
    void getValue(LottieShapeData &shape);
    void getValue(LottieGradient &gradient);
    void getValue(std::vector<VPointF> &v);
    void getValue(LOTRepeaterTransform &);

    template <typename T>
    bool parseKeyFrameValue(const char *key, LOTKeyFrameValue<T> &value);
    template <typename T>
    void parseKeyFrame(LOTAnimInfo<T> &obj);
    template <typename T>
    void parseProperty(LOTAnimatable<T> &obj);
    template <typename T>
    void parsePropertyHelper(LOTAnimatable<T> &obj);

    void parseShapeKeyFrame(LOTAnimInfo<LottieShapeData> &obj);
    void parseShapeProperty(LOTAnimatable<LottieShapeData> &obj);
    void parseDashProperty(LOTDashProperty &dash);

    VInterpolator* interpolator(VPointF, VPointF, const char*);

    LottieColor toColor(const char *str);

    void resolveLayerRefs();

protected:
    std::unordered_map<std::string, VInterpolator*>
                                               mInterpolatorCache;
    std::shared_ptr<LOTCompositionData>        mComposition;
    LOTCompositionData *                       compRef{nullptr};
    LOTLayerData *                             curLayerRef{nullptr};
    std::vector<LOTLayerData *>                mLayersToUpdate;
    std::string                                mDirPath;
    std::vector<VPointF>                       mInPoint;  /* "i" */
    std::vector<VPointF>                       mOutPoint; /* "o" */
    std::vector<VPointF>                       mVertices;
    void                                       SkipOut(int depth);
};

LookaheadParserHandler::LookaheadParserHandler(char *str)
    : v_(), st_(kInit), ss_(str)
{
    r_.IterativeParseInit();
}

bool LottieParserImpl::VerifyType()
{
    /* Verify the media type is lottie json.
       Could add more strict check. */
    return ParseNext();
}

bool LottieParserImpl::ParseNext()
{
    if (r_.HasParseError()) {
        st_ = kError;
        return false;
    }

    if (!r_.IterativeParseNext(parseFlags, ss_, *this)) {
        vCritical << "Lottie file parsing error";
        st_ = kError;
        return false;
    }
    return true;
}

bool LottieParserImpl::EnterObject()
{
    if (st_ != kEnteringObject) {
        st_ = kError;
        RAPIDJSON_ASSERT(false);
        return false;
    }

    ParseNext();
    return true;
}

bool LottieParserImpl::EnterArray()
{
    if (st_ != kEnteringArray) {
        st_ = kError;
        RAPIDJSON_ASSERT(false);
        return false;
    }

    ParseNext();
    return true;
}

const char *LottieParserImpl::NextObjectKey()
{
    if (st_ == kHasKey) {
        const char *result = v_.GetString();
        ParseNext();
        return result;
    }

    /* SPECIAL CASE
     * The parser works with a prdefined rule that it will be only
     * while (NextObjectKey()) for each object but in case of our nested group
     * object we can call multiple time NextObjectKey() while exiting the object
     * so ignore those and don't put parser in the error state.
     * */
    if (st_ == kExitingArray || st_ == kEnteringObject) {
        return nullptr;
    }

    if (st_ != kExitingObject) {
        RAPIDJSON_ASSERT(false);
        st_ = kError;
        return nullptr;
    }

    ParseNext();
    return nullptr;
}

bool LottieParserImpl::NextArrayValue()
{
    if (st_ == kExitingArray) {
        ParseNext();
        return false;
    }

    /* SPECIAL CASE
     * same as  NextObjectKey()
     */
    if (st_ == kExitingObject) {
        return false;
    }

    if (st_ == kError || st_ == kHasKey) {
        RAPIDJSON_ASSERT(false);
        st_ = kError;
        return false;
    }

    return true;
}

int LottieParserImpl::GetInt()
{
    if (st_ != kHasNumber || !v_.IsInt()) {
        st_ = kError;
        RAPIDJSON_ASSERT(false);
        return 0;
    }

    int result = v_.GetInt();
    ParseNext();
    return result;
}

double LottieParserImpl::GetDouble()
{
    if (st_ != kHasNumber) {
        st_ = kError;
        RAPIDJSON_ASSERT(false);
        return 0.;
    }

    double result = v_.GetDouble();
    ParseNext();
    return result;
}

bool LottieParserImpl::GetBool()
{
    if (st_ != kHasBool) {
        st_ = kError;
        RAPIDJSON_ASSERT(false);
        return false;
    }

    bool result = v_.GetBool();
    ParseNext();
    return result;
}

void LottieParserImpl::GetNull()
{
    if (st_ != kHasNull) {
        st_ = kError;
        return;
    }

    ParseNext();
}

const char *LottieParserImpl::GetString()
{
    if (st_ != kHasString) {
        st_ = kError;
        RAPIDJSON_ASSERT(false);
        return nullptr;
    }

    const char *result = v_.GetString();
    ParseNext();
    return result;
}

void LottieParserImpl::SkipOut(int depth)
{
    do {
        if (st_ == kEnteringArray || st_ == kEnteringObject) {
            ++depth;
        } else if (st_ == kExitingArray || st_ == kExitingObject) {
            --depth;
        } else if (st_ == kError) {
            RAPIDJSON_ASSERT(false);
            return;
        }

        ParseNext();
    } while (depth > 0);
}

void LottieParserImpl::SkipValue()
{
    SkipOut(0);
}

void LottieParserImpl::SkipArray()
{
    SkipOut(1);
}

void LottieParserImpl::SkipObject()
{
    SkipOut(1);
}

RjValue *LottieParserImpl::PeekValue()
{
    if (st_ >= kHasNull && st_ <= kHasKey) {
        return &v_;
    }

    return nullptr;
}

// returns a rapidjson::Type, or -1 for no value (at end of
// object/array)
int LottieParserImpl::PeekType() const
{
    if (st_ >= kHasNull && st_ <= kHasKey) {
        return v_.GetType();
    }

    if (st_ == kEnteringArray) {
        return kArrayType;
    }

    if (st_ == kEnteringObject) {
        return kObjectType;
    }

    return -1;
}

void LottieParserImpl::Skip(const char * /*key*/)
{
    if (PeekType() == kArrayType) {
        EnterArray();
        SkipArray();
    } else if (PeekType() == kObjectType) {
        EnterObject();
        SkipObject();
    } else {
        SkipValue();
    }
}

LottieBlendMode LottieParserImpl::getBlendMode()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    LottieBlendMode mode = LottieBlendMode::Normal;

    switch (GetInt()) {
    case 1:
        mode = LottieBlendMode::Multiply;
        break;
    case 2:
        mode = LottieBlendMode::Screen;
        break;
    case 3:
        mode = LottieBlendMode::OverLay;
        break;
    default:
        break;
    }
    return mode;
}

void LottieParserImpl::resolveLayerRefs()
{
    for (const auto &layer : mLayersToUpdate) {
        auto          search = compRef->mAssets.find(layer->extra()->mPreCompRefId.c_str());
        if (search != compRef->mAssets.end()) {
            if (layer->mLayerType == LayerType::Image) {
                layer->extra()->mAsset = search->second;
            } else if (layer->mLayerType == LayerType::Precomp) {
                layer->mChildren = search->second->mLayers;
                layer->setStatic(layer->isStatic() &&
                                 search->second->isStatic());
            }
        }
    }
}

void LottieParserImpl::parseComposition()
{
    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    EnterObject();
    std::shared_ptr<LOTCompositionData> sharedComposition =
        std::make_shared<LOTCompositionData>();
    LOTCompositionData *comp = sharedComposition.get();
    compRef = comp;
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "v")) {
            RAPIDJSON_ASSERT(PeekType() == kStringType);
            comp->mVersion = std::string(GetString());
        } else if (0 == strcmp(key, "w")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            comp->mSize.setWidth(GetInt());
        } else if (0 == strcmp(key, "h")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            comp->mSize.setHeight(GetInt());
        } else if (0 == strcmp(key, "ip")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            comp->mStartFrame = (long)GetDouble();
        } else if (0 == strcmp(key, "op")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            comp->mEndFrame = (long)GetDouble();
        } else if (0 == strcmp(key, "fr")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            comp->mFrameRate = (float)GetDouble();
        } else if (0 == strcmp(key, "assets")) {
            parseAssets(comp);
        } else if (0 == strcmp(key, "layers")) {
            parseLayers(comp);
        } else if (0 == strcmp(key, "markers")) {
            parseMarkers();
        } else {
#ifdef DEBUG_PARSER
            vWarning << "Composition Attribute Skipped : " << key;
#endif
            Skip(key);
        }
    }

    if (comp->mVersion.empty() || !comp->mRootLayer) {
        // don't have a valid bodymovin header
        return;
    }
    if (!IsValid()) {
        return;
    }

    resolveLayerRefs();
    comp->setStatic(comp->mRootLayer->isStatic());
    comp->mRootLayer->mInFrame = comp->mStartFrame;
    comp->mRootLayer->mOutFrame = comp->mEndFrame;

    mComposition = sharedComposition;
}

void LottieParserImpl::parseMarker()
{
    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    EnterObject();
    std::string comment;
    int         timeframe{0};
    int          duration{0};
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "cm")) {
            RAPIDJSON_ASSERT(PeekType() == kStringType);
            comment = std::string(GetString());
        } else if (0 == strcmp(key, "tm")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            timeframe = (int)GetDouble();
        } else if (0 == strcmp(key, "dr")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            duration = (int)GetDouble();

        } else {
#ifdef DEBUG_PARSER
            vWarning << "Marker Attribute Skipped : " << key;
#endif
            Skip(key);
        }
    }
    compRef->mMarkers.emplace_back(std::move(comment), timeframe, timeframe + duration);
}

void LottieParserImpl::parseMarkers()
{
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        parseMarker();
    }
    // update the precomp layers with the actual layer object
}

void LottieParserImpl::parseAssets(LOTCompositionData *composition)
{
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        auto asset = parseAsset();
        composition->mAssets[asset->mRefId.c_str()] = asset;
    }
    // update the precomp layers with the actual layer object
}

static constexpr const unsigned char B64index[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  62, 63, 62, 62, 63, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, 0,  0,  0,  0,  63, 0,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

std::string b64decode(const char *data, const size_t len)
{
    auto p = reinterpret_cast<const unsigned char *>(data);
    int            pad = len > 0 && (len % 4 || p[len - 1] == '=');
    const size_t   L = ((len + 3) / 4 - pad) * 4;
    std::string    str(L / 4 * 3 + pad, '\0');

    for (size_t i = 0, j = 0; i < L; i += 4) {
        int n = B64index[p[i]] << 18 | B64index[p[i + 1]] << 12 |
                B64index[p[i + 2]] << 6 | B64index[p[i + 3]];
        str[j++] = char(n >> 16);
        str[j++] = char(n >> 8 & 0xFF);
        str[j++] = char(n & 0xFF);
    }
    if (pad) {
        int n = B64index[p[L]] << 18 | B64index[p[L + 1]] << 12;
        str[str.size() - 1] = (char)(n >> 16);

        if (len > L + 2 && p[L + 2] != '=') {
            n |= B64index[p[L + 2]] << 6;
            str.push_back(n >> 8 & 0xFF);
        }
    }
    return str;
}

static std::string convertFromBase64(const std::string &str)
{
    // usual header look like "data:image/png;base64,"
    // so need to skip till ','.
    size_t startIndex = str.find(",", 0);
    startIndex += 1;  // skip ","
    size_t length = str.length() - startIndex;

    const char *b64Data = str.c_str() + startIndex;

    return b64decode(b64Data, length);
}

/*
 *  std::to_string() function is missing in VS2017
 *  so this is workaround for windows build
 */

template<class T>
static std::string toString(const T &value) {
   return std::to_string(value);
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/layers/shape.json
 *
 */
LOTAsset* LottieParserImpl::parseAsset()
{
    RAPIDJSON_ASSERT(PeekType() == kObjectType);

    auto                      asset = allocator().make<LOTAsset>();
    std::string               filename;
    std::string               relativePath;
    bool                      embededResource = false;
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "w")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            asset->mWidth = GetInt();
        } else if (0 == strcmp(key, "h")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            asset->mHeight = GetInt();
        } else if (0 == strcmp(key, "p")) { /* image name */
            asset->mAssetType = LOTAsset::Type::Image;
            RAPIDJSON_ASSERT(PeekType() == kStringType);
            filename = std::string(GetString());
        } else if (0 == strcmp(key, "u")) { /* relative image path */
            RAPIDJSON_ASSERT(PeekType() == kStringType);
            relativePath = std::string(GetString());
        } else if (0 == strcmp(key, "e")) { /* relative image path */
            embededResource = GetInt();
        } else if (0 == strcmp(key, "id")) { /* reference id*/
            if (PeekType() == kStringType) {
                asset->mRefId = std::string(GetString());
            } else {
                RAPIDJSON_ASSERT(PeekType() == kNumberType);
                asset->mRefId = toString(GetInt()).c_str();
            }
        } else if (0 == strcmp(key, "layers")) {
            asset->mAssetType = LOTAsset::Type::Precomp;
            RAPIDJSON_ASSERT(PeekType() == kArrayType);
            EnterArray();
            bool staticFlag = true;
            while (NextArrayValue()) {
                auto layer = parseLayer();
                if (layer) {
                    staticFlag = staticFlag && layer->isStatic();
                    asset->mLayers.push_back(layer);
                }
            }
            asset->setStatic(staticFlag);
        } else {
#ifdef DEBUG_PARSER
            vWarning << "Asset Attribute Skipped : " << key;
#endif
            Skip(key);
        }
    }

    if (asset->mAssetType == LOTAsset::Type::Image) {
        if (embededResource) {
            // embeder resource should start with "data:"
            if (filename.compare(0, 5, "data:") == 0) {
                asset->loadImageData(convertFromBase64(filename));
            }
        } else {
            asset->loadImagePath(mDirPath + relativePath + filename);
        }
    }

    return asset;
}

void LottieParserImpl::parseLayers(LOTCompositionData *comp)
{
    comp->mRootLayer = allocator().make<LOTLayerData>();
    comp->mRootLayer->mLayerType = LayerType::Precomp;
    comp->mRootLayer->setName("__");
    bool staticFlag = true;
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        auto layer = parseLayer();
        if (layer) {
            staticFlag = staticFlag && layer->isStatic();
            comp->mRootLayer->mChildren.push_back(layer);
        }
    }
    comp->mRootLayer->setStatic(staticFlag);
}

LottieColor LottieParserImpl::toColor(const char *str)
{
    LottieColor color;
    auto len = strlen(str);

    // some resource has empty color string
    // return a default color for those cases.
    if (len != 7 || str[0] != '#') return color;

    char tmp[3] = {'\0', '\0', '\0'};
    tmp[0] = str[1];
    tmp[1] = str[2];
    color.r = std::strtol(tmp, nullptr, 16) / 255.0f;

    tmp[0] = str[3];
    tmp[1] = str[4];
    color.g = std::strtol(tmp, nullptr, 16) / 255.0f;

    tmp[0] = str[5];
    tmp[1] = str[6];
    color.b = std::strtol(tmp, nullptr, 16) / 255.0f;

    return color;
}

MatteType LottieParserImpl::getMatteType()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    switch (GetInt()) {
    case 1:
        return MatteType::Alpha;
        break;
    case 2:
        return MatteType::AlphaInv;
        break;
    case 3:
        return MatteType::Luma;
        break;
    case 4:
        return MatteType::LumaInv;
        break;
    default:
        return MatteType::None;
        break;
    }
}

LayerType LottieParserImpl::getLayerType()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    switch (GetInt()) {
    case 0:
        return LayerType::Precomp;
        break;
    case 1:
        return LayerType::Solid;
        break;
    case 2:
        return LayerType::Image;
        break;
    case 3:
        return LayerType::Null;
        break;
    case 4:
        return LayerType::Shape;
        break;
    case 5:
        return LayerType::Text;
        break;
    default:
        return LayerType::Null;
        break;
    }
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/layers/shape.json
 *
 */
LOTLayerData* LottieParserImpl::parseLayer()
{
    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    LOTLayerData *layer = allocator().make<LOTLayerData>();
    curLayerRef = layer;
    bool ddd = true;
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "ty")) { /* Type of layer*/
            layer->mLayerType = getLayerType();
        } else if (0 == strcmp(key, "nm")) { /*Layer name*/
            RAPIDJSON_ASSERT(PeekType() == kStringType);
            layer->setName(GetString());
        } else if (0 == strcmp(key, "ind")) { /*Layer index in AE. Used for
                                                 parenting and expressions.*/
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            layer->mId = GetInt();
        } else if (0 == strcmp(key, "ddd")) { /*3d layer */
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            ddd = GetInt();
        } else if (0 ==
                   strcmp(key,
                          "parent")) { /*Layer Parent. Uses "ind" of parent.*/
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            layer->mParentId = GetInt();
        } else if (0 == strcmp(key, "refId")) { /*preComp Layer reference id*/
            RAPIDJSON_ASSERT(PeekType() == kStringType);
            layer->extra()->mPreCompRefId = std::string(GetString());
            layer->mHasGradient = true;
            mLayersToUpdate.push_back(layer);
        } else if (0 == strcmp(key, "sr")) {  // "Layer Time Stretching"
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            layer->mTimeStreatch = (float)GetDouble();
        } else if (0 == strcmp(key, "tm")) {  // time remapping
            parseProperty(layer->extra()->mTimeRemap);
        } else if (0 == strcmp(key, "ip")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            layer->mInFrame = std::lround((float)GetDouble());
        } else if (0 == strcmp(key, "op")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            layer->mOutFrame = std::lround((float)GetDouble());
        } else if (0 == strcmp(key, "st")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            layer->mStartFrame = (int)GetDouble();
        } else if (0 == strcmp(key, "bm")) {
            layer->mBlendMode = getBlendMode();
        } else if (0 == strcmp(key, "ks")) {
            RAPIDJSON_ASSERT(PeekType() == kObjectType);
            EnterObject();
            layer->mTransform = parseTransformObject(ddd);
        } else if (0 == strcmp(key, "shapes")) {
            parseShapesAttr(layer);
        } else if (0 == strcmp(key, "w")) {
            layer->mLayerSize.setWidth(GetInt());
        } else if (0 == strcmp(key, "h")) {
            layer->mLayerSize.setHeight(GetInt());
        } else if (0 == strcmp(key, "sw")) {
            layer->mLayerSize.setWidth(GetInt());
        } else if (0 == strcmp(key, "sh")) {
            layer->mLayerSize.setHeight(GetInt());
        } else if (0 == strcmp(key, "sc")) {
            layer->extra()->mSolidColor = toColor(GetString());
        } else if (0 == strcmp(key, "tt")) {
            layer->mMatteType = getMatteType();
        } else if (0 == strcmp(key, "hasMask")) {
            layer->mHasMask = GetBool();
        } else if (0 == strcmp(key, "masksProperties")) {
            parseMaskProperty(layer);
        } else if (0 == strcmp(key, "ao")) {
            layer->mAutoOrient = GetInt();
        } else if (0 == strcmp(key, "hd")) {
            layer->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vWarning << "Layer Attribute Skipped : " << key;
#endif
            Skip(key);
        }
    }

    if (!layer->mTransform) {
        // not a valid layer
        return nullptr;
    }

    // make sure layer data is not corrupted.
    if (layer->hasParent() && (layer->id() == layer->parentId())) return nullptr;

    if (layer->mExtra) layer->mExtra->mCompRef = compRef;

    if (layer->hidden()) {
        // if layer is hidden, only data that is usefull is its
        // transform matrix(when it is a parent of some other layer)
        // so force it to be a Null Layer and release all resource.
        layer->setStatic(layer->mTransform->isStatic());
        layer->mLayerType = LayerType::Null;
        layer->mChildren = {};
        return layer;
    }

    // update the static property of layer
    bool staticFlag = true;
    for (const auto &child : layer->mChildren) {
        staticFlag &= child->isStatic();
    }

    if (layer->hasMask()) {
        for (const auto &mask : layer->mExtra->mMasks) {
            staticFlag &= mask->isStatic();
        }
    }

    layer->setStatic(staticFlag && layer->mTransform->isStatic());

    return layer;
}

void LottieParserImpl::parseMaskProperty(LOTLayerData *layer)
{
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        layer->extra()->mMasks.push_back(parseMaskObject());
    }
}

LOTMaskData* LottieParserImpl::parseMaskObject()
{
    auto obj = allocator().make<LOTMaskData>();

    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "inv")) {
            obj->mInv = GetBool();
        } else if (0 == strcmp(key, "mode")) {
            const char *str = GetString();
            if (!str) {
                obj->mMode = LOTMaskData::Mode::None;
                continue;
            }
            switch (str[0]) {
            case 'n':
                obj->mMode = LOTMaskData::Mode::None;
                break;
            case 'a':
                obj->mMode = LOTMaskData::Mode::Add;
                break;
            case 's':
                obj->mMode = LOTMaskData::Mode::Substarct;
                break;
            case 'i':
                obj->mMode = LOTMaskData::Mode::Intersect;
                break;
            case 'f':
                obj->mMode = LOTMaskData::Mode::Difference;
                break;
            default:
                obj->mMode = LOTMaskData::Mode::None;
                break;
            }
        } else if (0 == strcmp(key, "pt")) {
            parseShapeProperty(obj->mShape);
        } else if (0 == strcmp(key, "o")) {
            parseProperty(obj->mOpacity);
        } else {
            Skip(key);
        }
    }
    obj->mIsStatic = obj->mShape.isStatic() && obj->mOpacity.isStatic();
    return obj;
}

void LottieParserImpl::parseShapesAttr(LOTLayerData *layer)
{
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        parseObject(layer);
    }
}

LOTData* LottieParserImpl::parseObjectTypeAttr()
{
    RAPIDJSON_ASSERT(PeekType() == kStringType);
    const char *type = GetString();
    if (0 == strcmp(type, "gr")) {
        return parseGroupObject();
    } else if (0 == strcmp(type, "rc")) {
        return parseRectObject();
    } else if (0 == strcmp(type, "el")) {
        return parseEllipseObject();
    } else if (0 == strcmp(type, "tr")) {
        return parseTransformObject();
    } else if (0 == strcmp(type, "fl")) {
        return parseFillObject();
    } else if (0 == strcmp(type, "st")) {
        return parseStrokeObject();
    } else if (0 == strcmp(type, "gf")) {
        curLayerRef->mHasGradient = true;
        return parseGFillObject();
    } else if (0 == strcmp(type, "gs")) {
        curLayerRef->mHasGradient = true;
        return parseGStrokeObject();
    } else if (0 == strcmp(type, "sh")) {
        return parseShapeObject();
    } else if (0 == strcmp(type, "sr")) {
        return parsePolystarObject();
    } else if (0 == strcmp(type, "tm")) {
        curLayerRef->mHasPathOperator = true;
        return parseTrimObject();
    } else if (0 == strcmp(type, "rp")) {
        curLayerRef->mHasRepeater = true;
        return parseReapeaterObject();
    } else if (0 == strcmp(type, "mm")) {
        vWarning << "Merge Path is not supported yet";
        return nullptr;
    } else {
#ifdef DEBUG_PARSER
        vDebug << "The Object Type not yet handled = " << type;
#endif
        return nullptr;
    }
}

void LottieParserImpl::parseObject(LOTGroupData *parent)
{
    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "ty")) {
            auto child = parseObjectTypeAttr();
            if (child && !child->hidden()) parent->mChildren.push_back(child);
        } else {
            Skip(key);
        }
    }
}

LOTData* LottieParserImpl::parseGroupObject()
{
    auto group = allocator().make<LOTShapeGroupData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            group->setName(GetString());
        } else if (0 == strcmp(key, "it")) {
            RAPIDJSON_ASSERT(PeekType() == kArrayType);
            EnterArray();
            while (NextArrayValue()) {
                RAPIDJSON_ASSERT(PeekType() == kObjectType);
                parseObject(group);
            }
            if (group->mChildren.back()->type() == LOTData::Type::Transform) {
                group->mTransform = static_cast<LOTTransformData *>(group->mChildren.back());
                group->mChildren.pop_back();
            }
        } else {
            Skip(key);
        }
    }
    bool staticFlag = true;
    for (const auto &child : group->mChildren) {
        staticFlag &= child->isStatic();
    }

    if (group->mTransform) {
        group->setStatic(staticFlag && group->mTransform->isStatic());
    }

    return group;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/rect.json
 */
LOTRectData* LottieParserImpl::parseRectObject()
{
    auto obj = allocator().make<LOTRectData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "p")) {
            parseProperty(obj->mPos);
        } else if (0 == strcmp(key, "s")) {
            parseProperty(obj->mSize);
        } else if (0 == strcmp(key, "r")) {
            parseProperty(obj->mRound);
        } else if (0 == strcmp(key, "d")) {
            obj->mDirection = GetInt();
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
            Skip(key);
        }
    }
    obj->setStatic(obj->mPos.isStatic() && obj->mSize.isStatic() &&
                   obj->mRound.isStatic());
    return obj;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/ellipse.json
 */
LOTEllipseData* LottieParserImpl::parseEllipseObject()
{
    auto obj = allocator().make<LOTEllipseData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "p")) {
            parseProperty(obj->mPos);
        } else if (0 == strcmp(key, "s")) {
            parseProperty(obj->mSize);
        } else if (0 == strcmp(key, "d")) {
            obj->mDirection = GetInt();
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
            Skip(key);
        }
    }
    obj->setStatic(obj->mPos.isStatic() && obj->mSize.isStatic());
    return obj;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/shape.json
 */
LOTShapeData* LottieParserImpl::parseShapeObject()
{
    auto obj = allocator().make<LOTShapeData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "ks")) {
            parseShapeProperty(obj->mShape);
        } else if (0 == strcmp(key, "d")) {
            obj->mDirection = GetInt();
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vDebug << "Shape property ignored :" << key;
#endif
            Skip(key);
        }
    }
    obj->setStatic(obj->mShape.isStatic());

    return obj;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/star.json
 */
LOTPolystarData* LottieParserImpl::parsePolystarObject()
{
    auto obj = allocator().make<LOTPolystarData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "p")) {
            parseProperty(obj->mPos);
        } else if (0 == strcmp(key, "pt")) {
            parseProperty(obj->mPointCount);
        } else if (0 == strcmp(key, "ir")) {
            parseProperty(obj->mInnerRadius);
        } else if (0 == strcmp(key, "is")) {
            parseProperty(obj->mInnerRoundness);
        } else if (0 == strcmp(key, "or")) {
            parseProperty(obj->mOuterRadius);
        } else if (0 == strcmp(key, "os")) {
            parseProperty(obj->mOuterRoundness);
        } else if (0 == strcmp(key, "r")) {
            parseProperty(obj->mRotation);
        } else if (0 == strcmp(key, "sy")) {
            int starType = GetInt();
            if (starType == 1) obj->mPolyType = LOTPolystarData::PolyType::Star;
            if (starType == 2) obj->mPolyType = LOTPolystarData::PolyType::Polygon;
        } else if (0 == strcmp(key, "d")) {
            obj->mDirection = GetInt();
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vDebug << "Polystar property ignored :" << key;
#endif
            Skip(key);
        }
    }
    obj->setStatic(
        obj->mPos.isStatic() && obj->mPointCount.isStatic() &&
        obj->mInnerRadius.isStatic() && obj->mInnerRoundness.isStatic() &&
        obj->mOuterRadius.isStatic() && obj->mOuterRoundness.isStatic() &&
        obj->mRotation.isStatic());

    return obj;
}

LOTTrimData::TrimType LottieParserImpl::getTrimType()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    switch (GetInt()) {
    case 1:
        return LOTTrimData::TrimType::Simultaneously;
        break;
    case 2:
        return LOTTrimData::TrimType::Individually;
        break;
    default:
        RAPIDJSON_ASSERT(0);
        return LOTTrimData::TrimType::Simultaneously;
        break;
    }
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/trim.json
 */
LOTTrimData* LottieParserImpl::parseTrimObject()
{
    auto obj = allocator().make<LOTTrimData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "s")) {
            parseProperty(obj->mStart);
        } else if (0 == strcmp(key, "e")) {
            parseProperty(obj->mEnd);
        } else if (0 == strcmp(key, "o")) {
            parseProperty(obj->mOffset);
        } else if (0 == strcmp(key, "m")) {
            obj->mTrimType = getTrimType();
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vDebug << "Trim property ignored :" << key;
#endif
            Skip(key);
        }
    }
    obj->setStatic(obj->mStart.isStatic() && obj->mEnd.isStatic() &&
                   obj->mOffset.isStatic());
    return obj;
}

void LottieParserImpl::getValue(LOTRepeaterTransform &obj)
{
    EnterObject();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "a")) {
            parseProperty(obj.mAnchor);
        } else if (0 == strcmp(key, "p")) {
            parseProperty(obj.mPosition);
        } else if (0 == strcmp(key, "r")) {
            parseProperty(obj.mRotation);
        } else if (0 == strcmp(key, "s")) {
            parseProperty(obj.mScale);
        } else if (0 == strcmp(key, "so")) {
            parseProperty(obj.mStartOpacity);
        } else if (0 == strcmp(key, "eo")) {
            parseProperty(obj.mEndOpacity);
        } else {
            Skip(key);
        }
    }
}

LOTRepeaterData* LottieParserImpl::parseReapeaterObject()
{
    auto obj = allocator().make<LOTRepeaterData>();

    obj->setContent(allocator().make<LOTShapeGroupData>());

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "c")) {
            parseProperty(obj->mCopies);
            float maxCopy = 0.0;
            if (!obj->mCopies.isStatic()) {
                for (auto &keyFrame : obj->mCopies.animation().mKeyFrames) {
                    if (maxCopy < keyFrame.mValue.mStartValue)
                        maxCopy = keyFrame.mValue.mStartValue;
                    if (maxCopy < keyFrame.mValue.mEndValue)
                        maxCopy = keyFrame.mValue.mEndValue;
                }
            } else {
                maxCopy = obj->mCopies.value();
            }
            obj->mMaxCopies = maxCopy;
        } else if (0 == strcmp(key, "o")) {
            parseProperty(obj->mOffset);
        } else if (0 == strcmp(key, "tr")) {
            getValue(obj->mTransform);
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vDebug << "Repeater property ignored :" << key;
#endif
            Skip(key);
        }
    }
    obj->setStatic(obj->mCopies.isStatic() && obj->mOffset.isStatic() &&
                   obj->mTransform.isStatic());

    return obj;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/transform.json
 */
LOTTransformData* LottieParserImpl::parseTransformObject(
    bool ddd)
{
    auto objT = allocator().make<LOTTransformData>();

    std::shared_ptr<LOTTransformData> sharedTransform =
        std::make_shared<LOTTransformData>();

    auto obj = allocator().make<TransformData>();
    if (ddd) {
        obj->createExtraData();
        obj->mExtra->m3DData = true;
    }

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            sharedTransform->setName(GetString());
        } else if (0 == strcmp(key, "a")) {
            parseProperty(obj->mAnchor);
        } else if (0 == strcmp(key, "p")) {
            EnterObject();
            bool separate = false;
            while (const char *rkey = NextObjectKey()) {
                if (0 == strcmp(rkey, "k")) {
                    parsePropertyHelper(obj->mPosition);
                } else if (0 == strcmp(rkey, "s")) {
                    obj->createExtraData();
                    obj->mExtra->mSeparate = GetBool();
                    separate = true;
                } else if (separate && (0 == strcmp(rkey, "x"))) {
                    parseProperty(obj->mExtra->mSeparateX);
                } else if (separate && (0 == strcmp(rkey, "y"))) {
                    parseProperty(obj->mExtra->mSeparateY);
                } else {
                    Skip(rkey);
                }
            }
        } else if (0 == strcmp(key, "r")) {
            parseProperty(obj->mRotation);
        } else if (0 == strcmp(key, "s")) {
            parseProperty(obj->mScale);
        } else if (0 == strcmp(key, "o")) {
            parseProperty(obj->mOpacity);
        } else if (0 == strcmp(key, "hd")) {
            sharedTransform->setHidden(GetBool());
        } else if (0 == strcmp(key, "rx")) {
            parseProperty(obj->mExtra->m3DRx);
        } else if (0 == strcmp(key, "ry")) {
            parseProperty(obj->mExtra->m3DRy);
        } else if (0 == strcmp(key, "rz")) {
            parseProperty(obj->mExtra->m3DRz);
        } else {
            Skip(key);
        }
    }
    bool isStatic = obj->mAnchor.isStatic() && obj->mPosition.isStatic() &&
                    obj->mRotation.isStatic() && obj->mScale.isStatic() &&
                    obj->mOpacity.isStatic();
    if (obj->mExtra) {
        isStatic = isStatic &&
                   obj->mExtra->m3DRx.isStatic() &&
                   obj->mExtra->m3DRy.isStatic() &&
                   obj->mExtra->m3DRz.isStatic() &&
                   obj->mExtra->mSeparateX.isStatic() &&
                   obj->mExtra->mSeparateY.isStatic();
    }

    objT->set(obj, isStatic);

    return objT;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/fill.json
 */
LOTFillData* LottieParserImpl::parseFillObject()
{
    auto obj = allocator().make<LOTFillData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "c")) {
            parseProperty(obj->mColor);
        } else if (0 == strcmp(key, "o")) {
            parseProperty(obj->mOpacity);
        } else if (0 == strcmp(key, "fillEnabled")) {
            obj->mEnabled = GetBool();
        } else if (0 == strcmp(key, "r")) {
            obj->mFillRule = getFillRule();
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vWarning << "Fill property skipped = " << key;
#endif
            Skip(key);
        }
    }
    obj->setStatic(obj->mColor.isStatic() && obj->mOpacity.isStatic());

    return obj;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/helpers/lineCap.json
 */
CapStyle LottieParserImpl::getLineCap()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    switch (GetInt()) {
    case 1:
        return CapStyle::Flat;
        break;
    case 2:
        return CapStyle::Round;
        break;
    default:
        return CapStyle::Square;
        break;
    }
}

FillRule LottieParserImpl::getFillRule()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    switch (GetInt()) {
    case 1:
        return FillRule::Winding;
        break;
    case 2:
        return FillRule::EvenOdd;
        break;
    default:
        return FillRule::Winding;
        break;
    }
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/helpers/lineJoin.json
 */
JoinStyle LottieParserImpl::getLineJoin()
{
    RAPIDJSON_ASSERT(PeekType() == kNumberType);
    switch (GetInt()) {
    case 1:
        return JoinStyle::Miter;
        break;
    case 2:
        return JoinStyle::Round;
        break;
    default:
        return JoinStyle::Bevel;
        break;
    }
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/stroke.json
 */
LOTStrokeData* LottieParserImpl::parseStrokeObject()
{
    auto obj = allocator().make<LOTStrokeData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "c")) {
            parseProperty(obj->mColor);
        } else if (0 == strcmp(key, "o")) {
            parseProperty(obj->mOpacity);
        } else if (0 == strcmp(key, "w")) {
            parseProperty(obj->mWidth);
        } else if (0 == strcmp(key, "fillEnabled")) {
            obj->mEnabled = GetBool();
        } else if (0 == strcmp(key, "lc")) {
            obj->mCapStyle = getLineCap();
        } else if (0 == strcmp(key, "lj")) {
            obj->mJoinStyle = getLineJoin();
        } else if (0 == strcmp(key, "ml")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            obj->mMiterLimit = (float)GetDouble();
        } else if (0 == strcmp(key, "d")) {
            parseDashProperty(obj->mDash);
        } else if (0 == strcmp(key, "hd")) {
            obj->setHidden(GetBool());
        } else {
#ifdef DEBUG_PARSER
            vWarning << "Stroke property skipped = " << key;
#endif
            Skip(key);
        }
    }
    obj->setStatic(obj->mColor.isStatic() && obj->mOpacity.isStatic() &&
                   obj->mWidth.isStatic() && obj->mDash.isStatic());
    return obj;
}

void LottieParserImpl::parseGradientProperty(LOTGradient *obj, const char *key)
{
    if (0 == strcmp(key, "t")) {
        RAPIDJSON_ASSERT(PeekType() == kNumberType);
        obj->mGradientType = GetInt();
    } else if (0 == strcmp(key, "o")) {
        parseProperty(obj->mOpacity);
    } else if (0 == strcmp(key, "s")) {
        parseProperty(obj->mStartPoint);
    } else if (0 == strcmp(key, "e")) {
        parseProperty(obj->mEndPoint);
    } else if (0 == strcmp(key, "h")) {
        parseProperty(obj->mHighlightLength);
    } else if (0 == strcmp(key, "a")) {
        parseProperty(obj->mHighlightAngle);
    } else if (0 == strcmp(key, "g")) {
        EnterObject();
        while (const char *rkey = NextObjectKey()) {
            if (0 == strcmp(rkey, "k")) {
                parseProperty(obj->mGradient);
            } else if (0 == strcmp(rkey, "p")) {
                obj->mColorPoints = GetInt();
            } else {
                Skip(nullptr);
            }
        }
    } else if (0 == strcmp(key, "hd")) {
        obj->setHidden(GetBool());
    } else {
#ifdef DEBUG_PARSER
        vWarning << "Gradient property skipped = " << key;
#endif
        Skip(key);
    }
    obj->setStatic(
        obj->mOpacity.isStatic() && obj->mStartPoint.isStatic() &&
        obj->mEndPoint.isStatic() && obj->mHighlightAngle.isStatic() &&
        obj->mHighlightLength.isStatic() && obj->mGradient.isStatic());
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/gfill.json
 */
LOTGFillData* LottieParserImpl::parseGFillObject()
{
    auto obj = allocator().make<LOTGFillData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "r")) {
            obj->mFillRule = getFillRule();
        } else {
            parseGradientProperty(obj, key);
        }
    }
    return obj;
}

void LottieParserImpl::parseDashProperty(LOTDashProperty &dash)
{
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        RAPIDJSON_ASSERT(PeekType() == kObjectType);
        EnterObject();
        while (const char *key = NextObjectKey()) {
            if (0 == strcmp(key, "v")) {
                dash.mData.emplace_back();
                parseProperty(dash.mData.back());
            } else {
                Skip(key);
            }
        }
    }
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/shapes/gstroke.json
 */
LOTGStrokeData* LottieParserImpl::parseGStrokeObject()
{
    auto obj = allocator().make<LOTGStrokeData>();

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "nm")) {
            obj->setName(GetString());
        } else if (0 == strcmp(key, "w")) {
            parseProperty(obj->mWidth);
        } else if (0 == strcmp(key, "lc")) {
            obj->mCapStyle = getLineCap();
        } else if (0 == strcmp(key, "lj")) {
            obj->mJoinStyle = getLineJoin();
        } else if (0 == strcmp(key, "ml")) {
            RAPIDJSON_ASSERT(PeekType() == kNumberType);
            obj->mMiterLimit = (float)GetDouble();
        } else if (0 == strcmp(key, "d")) {
            parseDashProperty(obj->mDash);
        } else {
            parseGradientProperty(obj, key);
        }
    }

    obj->setStatic(obj->isStatic() && obj->mWidth.isStatic() &&
                   obj->mDash.isStatic());
    return obj;
}

void LottieParserImpl::getValue(std::vector<VPointF> &v)
{
    RAPIDJSON_ASSERT(PeekType() == kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        RAPIDJSON_ASSERT(PeekType() == kArrayType);
        EnterArray();
        VPointF pt;
        getValue(pt);
        v.push_back(pt);
    }
}

void LottieParserImpl::getValue(VPointF &pt)
{
    float val[4] = {0.f};
    int   i = 0;

    if (PeekType() == kArrayType) EnterArray();

    while (NextArrayValue()) {
        const auto value = (float)GetDouble();
        if (i < 4) {
            val[i++] = value;
        }
    }
    pt.setX(val[0]);
    pt.setY(val[1]);
}

void LottieParserImpl::getValue(float &val)
{
    if (PeekType() == kArrayType) {
        EnterArray();
        if (NextArrayValue()) val = (float)GetDouble();
        // discard rest
        while (NextArrayValue()) {
            GetDouble();
        }
    } else if (PeekType() == kNumberType) {
        val = (float)GetDouble();
    } else {
        RAPIDJSON_ASSERT(0);
    }
}

void LottieParserImpl::getValue(LottieColor &color)
{
    float val[4] = {0.f};
    int   i = 0;
    if (PeekType() == kArrayType) EnterArray();

    while (NextArrayValue()) {
        const auto value = (float)GetDouble();
        if (i < 4) {
            val[i++] = value;
        }
    }
    color.r = val[0];
    color.g = val[1];
    color.b = val[2];
}

void LottieParserImpl::getValue(LottieGradient &grad)
{
    if (PeekType() == kArrayType) EnterArray();

    while (NextArrayValue()) {
        grad.mGradient.push_back((float)GetDouble());
    }
}

void LottieParserImpl::getValue(int &val)
{
    if (PeekType() == kArrayType) {
        EnterArray();
        while (NextArrayValue()) {
            val = GetInt();
        }
    } else if (PeekType() == kNumberType) {
        val = GetInt();
    } else {
        RAPIDJSON_ASSERT(0);
    }
}

void LottieParserImpl::getValue(LottieShapeData &obj)
{
    mInPoint.clear();
    mOutPoint.clear();
    mVertices.clear();
    std::vector<VPointF> points;
    bool                 closed = false;

    /*
     * The shape object could be wrapped by a array
     * if its part of the keyframe object
     */
    bool arrayWrapper = (PeekType() == kArrayType);
    if (arrayWrapper) EnterArray();

    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "i")) {
            getValue(mInPoint);
        } else if (0 == strcmp(key, "o")) {
            getValue(mOutPoint);
        } else if (0 == strcmp(key, "v")) {
            getValue(mVertices);
        } else if (0 == strcmp(key, "c")) {
            closed = GetBool();
        } else {
            RAPIDJSON_ASSERT(0);
            Skip(nullptr);
        }
    }
    // exit properly from the array
    if (arrayWrapper) NextArrayValue();

    // shape data could be empty.
    if (mInPoint.empty() || mOutPoint.empty() || mVertices.empty()) return;

    /*
     * Convert the AE shape format to
     * list of bazier curves
     * The final structure will be Move +size*Cubic + Cubic (if the path is
     * closed one)
     */
    if (mInPoint.size() != mOutPoint.size() ||
        mInPoint.size() != mVertices.size()) {
        vCritical << "The Shape data are corrupted";
        points = std::vector<VPointF>();
    } else {
        auto size = mVertices.size();
        points.reserve(3 * size + 4);
        points.push_back(mVertices[0]);
        for (size_t i = 1; i < size; i++) {
            points.push_back(mVertices[i - 1] +
                             mOutPoint[i - 1]);  // CP1 = start + outTangent
            points.push_back(mVertices[i] +
                             mInPoint[i]);   // CP2 = end + inTangent
            points.push_back(mVertices[i]);  // end point
        }

        if (closed) {
            points.push_back(mVertices[size - 1] +
                             mOutPoint[size - 1]);  // CP1 = start + outTangent
            points.push_back(mVertices[0] +
                             mInPoint[0]);   // CP2 = end + inTangent
            points.push_back(mVertices[0]);  // end point
        }
    }
    obj.mPoints = std::move(points);
    obj.mClosed = closed;
}

VPointF LottieParserImpl::parseInperpolatorPoint()
{
    VPointF cp;
    RAPIDJSON_ASSERT(PeekType() == kObjectType);
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "x")) {
            getValue(cp.rx());
        }
        if (0 == strcmp(key, "y")) {
            getValue(cp.ry());
        }
    }
    return cp;
}

template <typename T>
bool LottieParserImpl::parseKeyFrameValue(const char *, LOTKeyFrameValue<T> &)
{
    return false;
}

template <>
bool LottieParserImpl::parseKeyFrameValue(const char *               key,
                                          LOTKeyFrameValue<VPointF> &value)
{
    if (0 == strcmp(key, "ti")) {
        value.mPathKeyFrame = true;
        getValue(value.mInTangent);
    } else if (0 == strcmp(key, "to")) {
        value.mPathKeyFrame = true;
        getValue(value.mOutTangent);
    } else {
        return false;
    }
    return true;
}

VInterpolator* LottieParserImpl::interpolator(
    VPointF inTangent, VPointF outTangent, const char* key)
{
    if (strlen(key) == 0) {
        std::vector<char> temp(20);
        snprintf(temp.data(), temp.size(), "%.2f_%.2f_%.2f_%.2f", inTangent.x(),
                 inTangent.y(), outTangent.x(), outTangent.y());
        key = temp.data();
    }

    auto search = mInterpolatorCache.find(key);

    if (search != mInterpolatorCache.end()) {
        return search->second;
    }

    auto obj = allocator().make<VInterpolator>(outTangent, inTangent);
    mInterpolatorCache[key] = obj;
    return obj;
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/properties/multiDimensionalKeyframed.json
 */
template <typename T>
void LottieParserImpl::parseKeyFrame(LOTAnimInfo<T> &obj)
{
    struct ParsedField {
        std::string interpolatorKey;
        bool        interpolator{false};
        bool        value{false};
        bool        hold{false};
        bool        noEndValue{true};
    };

    EnterObject();
    ParsedField    parsed;
    LOTKeyFrame<T> keyframe;
    VPointF        inTangent;
    VPointF        outTangent;

    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "i")) {
            parsed.interpolator = true;
            inTangent = parseInperpolatorPoint();
        } else if (0 == strcmp(key, "o")) {
            outTangent = parseInperpolatorPoint();
        } else if (0 == strcmp(key, "t")) {
            keyframe.mStartFrame = (float)GetDouble();
        } else if (0 == strcmp(key, "s")) {
            parsed.value = true;
            getValue(keyframe.mValue.mStartValue);
            continue;
        } else if (0 == strcmp(key, "e")) {
            parsed.noEndValue = false;
            getValue(keyframe.mValue.mEndValue);
            continue;
        } else if (0 == strcmp(key, "n")) {
            if (PeekType() == kStringType) {
                parsed.interpolatorKey = GetString();
            } else {
                RAPIDJSON_ASSERT(PeekType() == kArrayType);
                EnterArray();
                while (NextArrayValue()) {
                    RAPIDJSON_ASSERT(PeekType() == kStringType);
                    if (parsed.interpolatorKey.empty()) {
                        parsed.interpolatorKey = GetString();
                    } else {
                        // skip rest of the string
                        GetString();
                    }
                }
            }
            continue;
        } else if (parseKeyFrameValue(key, keyframe.mValue)) {
            continue;
        } else if (0 == strcmp(key, "h")) {
            parsed.hold = GetInt();
            continue;
        } else {
#ifdef DEBUG_PARSER
            vDebug << "key frame property skipped = " << key;
#endif
            Skip(key);
        }
    }

    if (!obj.mKeyFrames.empty()) {
        // update the endFrame value of current keyframe
        obj.mKeyFrames.back().mEndFrame = keyframe.mStartFrame;
        // if no end value provided, copy start value to previous frame
        if (parsed.value && parsed.noEndValue) {
            obj.mKeyFrames.back().mValue.mEndValue =
                keyframe.mValue.mStartValue;
        }
    }

    if (parsed.hold) {
        keyframe.mValue.mEndValue = keyframe.mValue.mStartValue;
        keyframe.mEndFrame = keyframe.mStartFrame;
        obj.mKeyFrames.push_back(std::move(keyframe));
    } else if (parsed.interpolator) {
        keyframe.mInterpolator = interpolator(
            inTangent, outTangent, parsed.interpolatorKey.c_str());
        obj.mKeyFrames.push_back(std::move(keyframe));
    } else {
        // its the last frame discard.
    }
}

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/properties/shapeKeyframed.json
 */

/*
 * https://github.com/airbnb/lottie-web/blob/master/docs/json/properties/shape.json
 */
void LottieParserImpl::parseShapeProperty(LOTAnimatable<LottieShapeData> &obj)
{
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "k")) {
            if (PeekType() == kArrayType) {
                EnterArray();
                while (NextArrayValue()) {
                    RAPIDJSON_ASSERT(PeekType() == kObjectType);
                    parseKeyFrame(obj.animation());
                }
            } else {
                if (!obj.isStatic()) {
                    RAPIDJSON_ASSERT(false);
                    st_ = kError;
                    return;
                }
                getValue(obj.value());
            }
        } else {
#ifdef DEBUG_PARSER
            vDebug << "shape property ignored = " << key;
#endif
            Skip(nullptr);
        }
    }
}

template <typename T>
void LottieParserImpl::parsePropertyHelper(LOTAnimatable<T> &obj)
{
    if (PeekType() == kNumberType) {
        if (!obj.isStatic()) {
            RAPIDJSON_ASSERT(false);
            st_ = kError;
            return;
        }
        /*single value property with no animation*/
        getValue(obj.value());
    } else {
        RAPIDJSON_ASSERT(PeekType() == kArrayType);
        EnterArray();
        while (NextArrayValue()) {
            /* property with keyframe info*/
            if (PeekType() == kObjectType) {
                parseKeyFrame(obj.animation());
            } else {
                /* Read before modifying.
                 * as there is no way of knowing if the
                 * value of the array is either array of numbers
                 * or array of object without entering the array
                 * thats why this hack is there
                 */
                RAPIDJSON_ASSERT(PeekType() == kNumberType);
                if (!obj.isStatic()) {
                    RAPIDJSON_ASSERT(false);
                    st_ = kError;
                    return;
                }
                /*multi value property with no animation*/
                getValue(obj.value());
                /*break here as we already reached end of array*/
                break;
            }
        }
    }
}

/*
 * https://github.com/airbnb/lottie-web/tree/master/docs/json/properties
 */
template <typename T>
void LottieParserImpl::parseProperty(LOTAnimatable<T> &obj)
{
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "k")) {
            parsePropertyHelper(obj);
        } else {
            Skip(key);
        }
    }
}

#ifdef LOTTIE_DUMP_TREE_SUPPORT

class LOTDataInspector {
public:
    void visit(LOTCompositionData *obj, std::string level)
    {
        vDebug << " { " << level << "Composition:: a: " << !obj->isStatic()
               << ", v: " << obj->mVersion << ", stFm: " << obj->startFrame()
               << ", endFm: " << obj->endFrame()
               << ", W: " << obj->size().width()
               << ", H: " << obj->size().height() << "\n";
        level.append("\t");
        visit(obj->mRootLayer, level);
        level.erase(level.end() - 1, level.end());
        vDebug << " } " << level << "Composition End\n";
    }
    void visit(LOTLayerData *obj, std::string level)
    {
        vDebug << level << "{ " << layerType(obj->mLayerType)
               << ", name: " << obj->name() << ", id:" << obj->mId
               << " Pid:" << obj->mParentId << ", a:" << !obj->isStatic()
               << ", " << matteType(obj->mMatteType)
               << ", mask:" << obj->hasMask() << ", inFm:" << obj->mInFrame
               << ", outFm:" << obj->mOutFrame << ", stFm:" << obj->mStartFrame
               << ", ts:" << obj->mTimeStreatch << ", ao:" << obj->autoOrient()
               << ", W:" << obj->layerSize().width()
               << ", H:" << obj->layerSize().height();

        if (obj->mLayerType == LayerType::Image)
            vDebug << level << "\t{ "
                   << "ImageInfo:"
                   << " W :" << obj->extra()->mAsset->mWidth
                   << ", H :" << obj->extra()->mAsset->mHeight << " }"
                   << "\n";
        else {
            vDebug << level;
        }
        visitChildren(static_cast<LOTGroupData *>(obj), level);
        vDebug << level << "} " << layerType(obj->mLayerType).c_str()
               << ", id: " << obj->mId << "\n";
    }
    void visitChildren(LOTGroupData *obj, std::string level)
    {
        level.append("\t");
        for (const auto &child : obj->mChildren) visit(child, level);
        if (obj->mTransform) visit(obj->mTransform, level);
    }

    void visit(LOTData *obj, std::string level)
    {
        switch (obj->type()) {
        case LOTData::Type::Repeater: {
            auto r = static_cast<LOTRepeaterData *>(obj);
            vDebug << level << "{ Repeater: name: " << obj->name()
                   << " , a:" << !obj->isStatic()
                   << ", copies:" << r->maxCopies()
                   << ", offset:" << r->offset(0);
            visitChildren(r->mContent, level);
            vDebug << level << "} Repeater";
            break;
        }
        case LOTData::Type::ShapeGroup: {
            vDebug << level << "{ ShapeGroup: name: " << obj->name()
                   << " , a:" << !obj->isStatic();
            visitChildren(static_cast<LOTGroupData *>(obj), level);
            vDebug << level << "} ShapeGroup";
            break;
        }
        case LOTData::Type::Layer: {
            visit(static_cast<LOTLayerData *>(obj), level);
            break;
        }
        case LOTData::Type::Trim: {
            vDebug << level << "{ Trim: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Rect: {
            vDebug << level << "{ Rect: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Ellipse: {
            vDebug << level << "{ Ellipse: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Shape: {
            vDebug << level << "{ Shape: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Polystar: {
            vDebug << level << "{ Polystar: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Transform: {
            vDebug << level << "{ Transform: name: " << obj->name()
                   << " , a: " << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Stroke: {
            vDebug << level << "{ Stroke: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::GStroke: {
            vDebug << level << "{ GStroke: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::Fill: {
            vDebug << level << "{ Fill: name: " << obj->name()
                   << " , a:" << !obj->isStatic() << " }";
            break;
        }
        case LOTData::Type::GFill: {
            auto f = static_cast<LOTGFillData *>(obj);
            vDebug << level << "{ GFill: name: " << obj->name()
                   << " , a:" << !f->isStatic() << ", ty:" << f->mGradientType
                   << ", s:" << f->mStartPoint.value(0)
                   << ", e:" << f->mEndPoint.value(0) << " }";
            break;
        }
        default:
            break;
        }
    }

    std::string matteType(MatteType type)
    {
        switch (type) {
        case MatteType::None:
            return "Matte::None";
            break;
        case MatteType::Alpha:
            return "Matte::Alpha";
            break;
        case MatteType::AlphaInv:
            return "Matte::AlphaInv";
            break;
        case MatteType::Luma:
            return "Matte::Luma";
            break;
        case MatteType::LumaInv:
            return "Matte::LumaInv";
            break;
        default:
            return "Matte::Unknown";
            break;
        }
    }
    std::string layerType(LayerType type)
    {
        switch (type) {
        case LayerType::Precomp:
            return "Layer::Precomp";
            break;
        case LayerType::Null:
            return "Layer::Null";
            break;
        case LayerType::Shape:
            return "Layer::Shape";
            break;
        case LayerType::Solid:
            return "Layer::Solid";
            break;
        case LayerType::Image:
            return "Layer::Image";
            break;
        case LayerType::Text:
            return "Layer::Text";
            break;
        default:
            return "Layer::Unknown";
            break;
        }
    }
};

#endif

LottieParser::~LottieParser() = default;
LottieParser::LottieParser(char *str, const char *dir_path)
    : d(std::make_unique<LottieParserImpl>(str, dir_path))
{
    if (d->VerifyType())
        d->parseComposition();
    else
        vWarning << "Input data is not Lottie format!";
}

std::shared_ptr<LOTModel> LottieParser::model()
{
    if (!d->composition()) return nullptr;

    std::shared_ptr<LOTModel> model = std::make_shared<LOTModel>();
    model->mRoot = d->composition();
    model->mRoot->processRepeaterObjects();
    model->mRoot->updateStats();


#ifdef LOTTIE_DUMP_TREE_SUPPORT
    LOTDataInspector inspector;
    inspector.visit(model->mRoot.get(), "");
#endif

    return model;
}

class LottieRepeaterProcesser {
public:
    void visitChildren(LOTGroupData *obj)
    {
        for (auto i = obj->mChildren.rbegin(); i != obj->mChildren.rend();
             ++i) {
            auto child = (*i);
            if (child->type() == LOTData::Type::Repeater) {
                LOTRepeaterData *repeater =
                    static_cast<LOTRepeaterData *>(child);
                // check if this repeater is already processed
                // can happen if the layer is an asset and referenced by
                // multiple layer.
                if (repeater->processed()) continue;

                repeater->markProcessed();

                LOTShapeGroupData *content = repeater->content();
                // 1. increment the reverse iterator to point to the
                //   object before the repeater
                ++i;
                // 2. move all the children till repater to the group
                std::move(obj->mChildren.begin(), i.base(),
                          std::back_inserter(content->mChildren));
                // 3. erase the objects from the original children list
                obj->mChildren.erase(obj->mChildren.begin(), i.base());

                // 5. visit newly created group to process remaining repeater
                // object.
                visitChildren(content);
                // 6. exit the loop as the current iterators are invalid
                break;
            }
            visit(child);
        }
    }

    void visit(LOTData *obj)
    {
        switch (obj->type()) {
        case LOTData::Type::ShapeGroup:
        case LOTData::Type::Layer: {
            visitChildren(static_cast<LOTGroupData *>(obj));
            break;
        }
        default:
            break;
        }
    }
};

class LottieUpdateStatVisitor {
    LOTModelStat *stat;
public:
    explicit LottieUpdateStatVisitor(LOTModelStat *s):stat(s){}
    void visitChildren(LOTGroupData *obj)
    {
        for (const auto &child : obj->mChildren) {
            if (child) visit(child);
        }
    }
    void visitLayer(LOTLayerData *layer)
    {
        switch (layer->mLayerType) {
        case LayerType::Precomp:
            stat->precompLayerCount++;
            break;
        case LayerType::Null:
            stat->nullLayerCount++;
            break;
        case LayerType::Shape:
            stat->shapeLayerCount++;
            break;
        case LayerType::Solid:
            stat->solidLayerCount++;
            break;
        case LayerType::Image:
            stat->imageLayerCount++;
            break;
        default:
            break;
        }
        visitChildren(layer);
    }
    void visit(LOTData *obj)
    {
        switch (obj->type()) {
        case LOTData::Type::Layer: {
            visitLayer(static_cast<LOTLayerData *>(obj));
            break;
        }
        case LOTData::Type::Repeater: {
            visitChildren(static_cast<LOTRepeaterData *>(obj)->content());
            break;
        }
        case LOTData::Type::ShapeGroup: {
            visitChildren(static_cast<LOTGroupData *>(obj));
            break;
        }
        default:
            break;
        }
    }

};

void LOTCompositionData::processRepeaterObjects()
{
    LottieRepeaterProcesser visitor;
    visitor.visit(mRootLayer);
}

void LOTCompositionData::updateStats()
{
    LottieUpdateStatVisitor visitor(&mStats);
    visitor.visit(mRootLayer);
}

VMatrix LOTRepeaterTransform::matrix(int frameNo, float multiplier) const
{
    VPointF scale = mScale.value(frameNo) / 100.f;
    scale.setX(std::pow(scale.x(), multiplier));
    scale.setY(std::pow(scale.y(), multiplier));
    VMatrix m;
    m.translate(mPosition.value(frameNo) * multiplier)
        .translate(mAnchor.value(frameNo))
        .scale(scale)
        .rotate(mRotation.value(frameNo) * multiplier)
        .translate(-mAnchor.value(frameNo));

    return m;
}

VMatrix TransformData::matrix(int frameNo, bool autoOrient) const
{
    VMatrix m;
    VPointF position;
    if (mExtra && mExtra->mSeparate) {
        position.setX(mExtra->mSeparateX.value(frameNo));
        position.setY(mExtra->mSeparateY.value(frameNo));
    } else {
        position = mPosition.value(frameNo);
    }

    float angle = autoOrient ? mPosition.angle(frameNo) : 0;
    if (mExtra && mExtra->m3DData) {
        m.translate(position)
            .rotate(mExtra->m3DRz.value(frameNo) + angle)
            .rotate(mExtra->m3DRy.value(frameNo), VMatrix::Axis::Y)
            .rotate(mExtra->m3DRx.value(frameNo), VMatrix::Axis::X)
            .scale(mScale.value(frameNo) / 100.f)
            .translate(-mAnchor.value(frameNo));
    } else {
        m.translate(position)
            .rotate(mRotation.value(frameNo) + angle)
            .scale(mScale.value(frameNo) / 100.f)
            .translate(-mAnchor.value(frameNo));
    }
    return m;
}

void LOTDashProperty::getDashInfo(int frameNo, std::vector<float>& result) const
{
    result.clear();

    if (mData.empty()) return;

    if (result.capacity() < mData.size()) result.reserve(mData.size() + 1);

    for (const auto &elm : mData)
        result.push_back(elm.value(frameNo));

    // if the size is even then we are missing last
    // gap information which is same as the last dash value
    // copy it from the last dash value.
    // NOTE: last value is the offset and last-1 is the last dash value.
    auto size = result.size();
    if ((size % 2) == 0) {
        //copy offset value to end.
        result.push_back(result.back());
        // copy dash value to gap.
        result[size-1] = result[size-2];
    }
}

/**
 * Both the color stops and opacity stops are in the same array.
 * There are {@link #colorPoints} colors sequentially as:
 * [
 *     ...,
 *     position,
 *     red,
 *     green,
 *     blue,
 *     ...
 * ]
 *
 * The remainder of the array is the opacity stops sequentially as:
 * [
 *     ...,
 *     position,
 *     opacity,
 *     ...
 * ]
 */
void LOTGradient::populate(VGradientStops &stops, int frameNo)
{
    LottieGradient gradData = mGradient.value(frameNo);
    auto            size = gradData.mGradient.size();
    float *        ptr = gradData.mGradient.data();
    int            colorPoints = mColorPoints;
    if (colorPoints == -1) {  // for legacy bodymovin (ref: lottie-android)
        colorPoints = int(size / 4);
    }
    auto    opacityArraySize = size - colorPoints * 4;
    float *opacityPtr = ptr + (colorPoints * 4);
    stops.clear();
    size_t j = 0;
    for (int i = 0; i < colorPoints; i++) {
        float       colorStop = ptr[0];
        LottieColor color = LottieColor(ptr[1], ptr[2], ptr[3]);
        if (opacityArraySize) {
            if (j == opacityArraySize) {
                // already reached the end
                float stop1 = opacityPtr[j - 4];
                float op1 = opacityPtr[j - 3];
                float stop2 = opacityPtr[j - 2];
                float op2 = opacityPtr[j - 1];
                if (colorStop > stop2) {
                    stops.push_back(
                        std::make_pair(colorStop, color.toColor(op2)));
                } else {
                    float progress = (colorStop - stop1) / (stop2 - stop1);
                    float opacity = op1 + progress * (op2 - op1);
                    stops.push_back(
                        std::make_pair(colorStop, color.toColor(opacity)));
                }
                continue;
            }
            for (; j < opacityArraySize; j += 2) {
                float opacityStop = opacityPtr[j];
                if (opacityStop < colorStop) {
                    // add a color using opacity stop
                    stops.push_back(std::make_pair(
                        opacityStop, color.toColor(opacityPtr[j + 1])));
                    continue;
                }
                // add a color using color stop
                if (j == 0) {
                    stops.push_back(std::make_pair(
                        colorStop, color.toColor(opacityPtr[j + 1])));
                } else {
                    float progress = (colorStop - opacityPtr[j - 2]) /
                                     (opacityPtr[j] - opacityPtr[j - 2]);
                    float opacity =
                        opacityPtr[j - 1] +
                        progress * (opacityPtr[j + 1] - opacityPtr[j - 1]);
                    stops.push_back(
                        std::make_pair(colorStop, color.toColor(opacity)));
                }
                j += 2;
                break;
            }
        } else {
            stops.push_back(std::make_pair(colorStop, color.toColor()));
        }
        ptr += 4;
    }
}

void LOTGradient::update(std::unique_ptr<VGradient> &grad, int frameNo)
{
    bool init = false;
    if (!grad) {
        if (mGradientType == 1)
            grad = std::make_unique<VGradient>(VGradient::Type::Linear);
        else
            grad = std::make_unique<VGradient>(VGradient::Type::Radial);
        grad->mSpread = VGradient::Spread::Pad;
        init = true;
    }

    if (!mGradient.isStatic() || init) {
        populate(grad->mStops, frameNo);
    }

    if (mGradientType == 1) {  // linear gradient
        VPointF start = mStartPoint.value(frameNo);
        VPointF end = mEndPoint.value(frameNo);
        grad->linear.x1 = start.x();
        grad->linear.y1 = start.y();
        grad->linear.x2 = end.x();
        grad->linear.y2 = end.y();
    } else {  // radial gradient
        VPointF start = mStartPoint.value(frameNo);
        VPointF end = mEndPoint.value(frameNo);
        grad->radial.cx = start.x();
        grad->radial.cy = start.y();
        grad->radial.cradius =
            VLine::length(start.x(), start.y(), end.x(), end.y());
        /*
         * Focal point is the point lives in highlight length distance from
         * center along the line (start, end)  and rotated by highlight angle.
         * below calculation first finds the quadrant(angle) on which the point
         * lives by applying inverse slope formula then adds the rotation angle
         * to find the final angle. then point is retrived using circle equation
         * of center, angle and distance.
         */
        float progress = mHighlightLength.value(frameNo) / 100.0f;
        if (vCompare(progress, 1.0f)) progress = 0.99f;
        float startAngle = VLine(start, end).angle();
        float highlightAngle = mHighlightAngle.value(frameNo);
        static constexpr float K_PI = 3.1415926f;
        float angle = (startAngle + highlightAngle) * (K_PI / 180.0f);
        grad->radial.fx =
            grad->radial.cx + std::cos(angle) * progress * grad->radial.cradius;
        grad->radial.fy =
            grad->radial.cy + std::sin(angle) * progress * grad->radial.cradius;
        // Lottie dosen't have any focal radius concept.
        grad->radial.fradius = 0;
    }
}

void LOTAsset::loadImageData(std::string data)
{
    if (!data.empty())
        mBitmap = VImageLoader::instance().load(data.c_str(), data.length());
}

void LOTAsset::loadImagePath(std::string path)
{
    if (!path.empty()) mBitmap = VImageLoader::instance().load(path.c_str());
}

std::vector<LayerInfo> LOTCompositionData::layerInfoList() const
{
    if (!mRootLayer || mRootLayer->mChildren.empty()) return {};

    std::vector<LayerInfo> result;

    result.reserve(mRootLayer->mChildren.size());

    for (auto it : mRootLayer->mChildren) {
        auto layer = static_cast<LOTLayerData *>(it);
        result.emplace_back(layer->name(), layer->mInFrame, layer->mOutFrame);
    }

    return result;
}

class LottieModelCache {
public:
    static LottieModelCache &instance()
    {
        static LottieModelCache CACHE;
        return CACHE;
    }
    std::shared_ptr<LOTModel> find(const std::string &) { return nullptr; }
    void add(const std::string &, std::shared_ptr<LOTModel>) {}
    void configureCacheSize(size_t) {}
};

void LottieLoader::configureModelCacheSize(size_t cacheSize)
{
    LottieModelCache::instance().configureCacheSize(cacheSize);
}

static std::string dirname(const std::string &path)
{
    const char *ptr = strrchr(path.c_str(), '/');
#ifdef _WIN32
    if (ptr) ptr = strrchr(ptr + 1, '\\');
#endif
    int         len = int(ptr + 1 - path.c_str());  // +1 to include '/'
    return std::string(path, 0, len);
}


bool LottieLoader::load(const std::string &path, bool cachePolicy)
{
    if (cachePolicy) {
        mModel = LottieModelCache::instance().find(path);
        if (mModel) return true;
    }

    FILE* f = std::fopen(path.c_str(), "r");

    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return { };

    // Read contents
    std::string content{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

    // Close the file
    file.close();

    if (content.empty()) {
        return false;
    }

    const char *str = content.c_str();
    LottieParser parser(const_cast<char *>(str),
                        dirname(path).c_str());
    mModel = parser.model();

    if (!mModel) return false;

    if (cachePolicy) {
        LottieModelCache::instance().add(path, mModel);
    }

    return true;
}

bool LottieLoader::loadFromData(std::string &&jsonData, const std::string &key,
                                const std::string &resourcePath, bool cachePolicy)
{
    if (cachePolicy) {
        mModel = LottieModelCache::instance().find(key);
        if (mModel) return true;
    }

    LottieParser parser(const_cast<char *>(jsonData.c_str()),
                        resourcePath.c_str());
    mModel = parser.model();

    if (!mModel) return false;

    if (cachePolicy)
        LottieModelCache::instance().add(key, mModel);

    return true;
}

std::shared_ptr<LOTModel> LottieLoader::model()
{
    return mModel;
}

LOTKeyPath::LOTKeyPath(const std::string &keyPath)
{
    int start = 0, i = 0;
    for (; i < keyPath.size(); i++)
      if (keyPath[i] == '.')
      {
        mKeys.push_back(keyPath.substr(start, i-start));
        i++;
        start = i;
      }
    mKeys.push_back(keyPath.substr(start, i-start));
}

bool LOTKeyPath::matches(const std::string &key, uint depth)
{
    if (skip(key)) {
        // This is an object we programatically create.
        return true;
    }
    if (depth > size()) {
        return false;
    }
    if ((mKeys[depth] == key) || (mKeys[depth] == "*") ||
        (mKeys[depth] == "**")) {
        return true;
    }
    return false;
}

uint LOTKeyPath::nextDepth(const std::string key, uint depth)
{
    if (skip(key)) {
        // If it's a container then we added programatically and it isn't a part
        // of the keypath.
        return depth;
    }
    if (mKeys[depth] != "**") {
        // If it's not a globstar then it is part of the keypath.
        return depth + 1;
    }
    if (depth == size()) {
        // The last key is a globstar.
        return depth;
    }
    if (mKeys[depth + 1] == key) {
        // We are a globstar and the next key is our current key so consume
        // both.
        return depth + 2;
    }
    return depth;
}

bool LOTKeyPath::fullyResolvesTo(const std::string key, uint depth)
{
    if (depth > mKeys.size()) {
        return false;
    }

    bool isLastDepth = (depth == size());

    if (!isGlobstar(depth)) {
        bool matches = (mKeys[depth] == key) || isGlob(depth);
        return (isLastDepth || (depth == size() - 1 && endsWithGlobstar())) &&
               matches;
    }

    bool isGlobstarButNextKeyMatches = !isLastDepth && mKeys[depth + 1] == key;
    if (isGlobstarButNextKeyMatches) {
        return depth == size() - 1 ||
               (depth == size() - 2 && endsWithGlobstar());
    }

    if (isLastDepth) {
        return true;
    }

    if (depth + 1 < size()) {
        // We are a globstar but there is more than 1 key after the globstar we
        // we can't fully match.
        return false;
    }
    // Return whether the next key (which we now know is the last one) is the
    // same as the current key.
    return mKeys[depth + 1] == key;
}

static bool transformProp(imlottie::Property prop)
{
    switch (prop) {
    case  Property::TrAnchor:
    case  Property::TrScale:
    case  Property::TrOpacity:
    case  Property::TrPosition:
    case  Property::TrRotation:
        return true;
    default:
        return false;
    }
}
static bool fillProp(imlottie::Property prop)
{
    switch (prop) {
    case Property::FillColor:
    case Property::FillOpacity:
        return true;
    default:
        return false;
    }
}

static bool strokeProp(imlottie::Property prop)
{
    switch (prop) {
    case Property::StrokeColor:
    case Property::StrokeOpacity:
    case Property::StrokeWidth:
        return true;
    default:
        return false;
    }
}

static LOTLayerItem*
createLayerItem(LOTLayerData *layerData, VArenaAlloc *allocator)
{
    switch (layerData->mLayerType) {
    case LayerType::Precomp: {
        return allocator->make<LOTCompLayerItem>(layerData, allocator);
    }
    case LayerType::Solid: {
        return allocator->make<LOTSolidLayerItem>(layerData);
    }
    case LayerType::Shape: {
        return allocator->make<LOTShapeLayerItem>(layerData, allocator);
    }
    case LayerType::Null: {
        return allocator->make<LOTNullLayerItem>(layerData);
    }
    case LayerType::Image: {
        return allocator->make<LOTImageLayerItem>(layerData);
    }
    default:
        return nullptr;
        break;
    }
}

LOTCompItem::LOTCompItem(LOTModel *model)
    : mCurFrameNo(-1)
{
    mCompData = model->mRoot.get();
    mRootLayer = createLayerItem(mCompData->mRootLayer, &mAllocator);
    mRootLayer->setComplexContent(false);
    mViewSize = mCompData->size();
}

void LOTCompItem::setValue(const std::string &keypath, LOTVariant &value)
{
    LOTKeyPath key(keypath);
    mRootLayer->resolveKeyPath(key, 0, value);
}

bool LOTCompItem::update(int frameNo, const VSize &size, bool keepAspectRatio)
{
    // check if cached frame is same as requested frame.
    if ((mViewSize == size) &&
        (mCurFrameNo == frameNo) &&
        (mKeepAspectRatio == keepAspectRatio)) return false;

    mViewSize = size;
    mCurFrameNo = frameNo;
    mKeepAspectRatio = keepAspectRatio;

    /*
     * if viewbox dosen't scale exactly to the viewport
     * we scale the viewbox keeping AspectRatioPreserved and then align the
     * viewbox to the viewport using AlignCenter rule.
     */
    VMatrix m;
    VSize viewPort = mViewSize;
    VSize viewBox = mCompData->size();
    float sx = float(viewPort.width()) / viewBox.width();
    float sy = float(viewPort.height()) / viewBox.height();
    if (mKeepAspectRatio) {
        float scale = std::min<float>(sx, sy);
        float tx = (viewPort.width() - viewBox.width() * scale) * 0.5f;
        float ty = (viewPort.height() - viewBox.height() * scale) * 0.5f;
        m.translate(tx, ty).scale(scale, scale);
    } else {
       m.scale(sx, sy);
    }
    mRootLayer->update(frameNo, m, 1.0);
    return true;
}

bool LOTCompItem::render(const imlottie::Surface &surface)
{
    mSurface.reset(reinterpret_cast<uchar *>(surface.buffer()),
                   uint(surface.width()), uint(surface.height()), uint(surface.bytesPerLine()),
                   VBitmap::Format::ARGB32_Premultiplied);
    mSurface.setNeedClear(surface.isNeedClear());

    /* schedule all preprocess task for this frame at once.
     */
    VRect clip(0, 0, int(surface.drawRegionWidth()), int(surface.drawRegionHeight()));
    mRootLayer->preprocess(clip);

    VPainter painter(&mSurface);
    // set sub surface area for drawing.
    painter.setDrawRegion(
        VRect(int(surface.drawRegionPosX()), int(surface.drawRegionPosY()),
              int(surface.drawRegionWidth()), int(surface.drawRegionHeight())));
    mRootLayer->render(&painter, {}, {});
    painter.end();
    return true;
}

void LOTMaskItem::update(int frameNo, const VMatrix &            parentMatrix,
                         float /*parentAlpha*/, const DirtyFlag &flag)
{
    if (flag.testFlag(DirtyFlagBit::None) && mData->isStatic()) return;

    if (mData->mShape.isStatic()) {
        if (mLocalPath.empty()) {
            mData->mShape.updatePath(frameNo, mLocalPath);
        }
    } else {
        mData->mShape.updatePath(frameNo, mLocalPath);
    }
    /* mask item dosen't inherit opacity */
    mCombinedAlpha = mData->opacity(frameNo);

    mFinalPath.clone(mLocalPath);
    mFinalPath.transform(parentMatrix);

    mRasterRequest = true;
}

VRle LOTMaskItem::rle()
{
    if (mRasterRequest) {
        mRasterRequest = false;
        if (!vCompare(mCombinedAlpha, 1.0f))
            mRasterizer.rle() *= uchar(mCombinedAlpha * 255);
        if (mData->mInv) mRasterizer.rle().invert();
    }
    return mRasterizer.rle();
}

void LOTMaskItem::preprocess(const VRect &clip)
{
    if (mRasterRequest) mRasterizer.rasterize(mFinalPath, FillRule::Winding, clip);
}

void LOTLayerItem::render(VPainter *painter, const VRle &inheritMask,
                          const VRle &matteRle)
{
    auto renderlist = renderList();

    if (renderlist.empty()) return;

    VRle mask;
    if (mLayerMask) {
        mask = mLayerMask->maskRle(painter->clipBoundingRect());
        if (!inheritMask.empty()) mask = mask & inheritMask;
        // if resulting mask is empty then return.
        if (mask.empty()) return;
    } else {
        mask = inheritMask;
    }

    for (auto &i : renderlist) {
        painter->setBrush(i->mBrush);
        VRle rle = i->rle();
        if (matteRle.empty()) {
            if (mask.empty()) {
                // no mask no matte
                painter->drawRle(VPoint(), rle);
            } else {
                // only mask
                painter->drawRle(rle, mask);
            }

        } else {
            if (!mask.empty()) rle = rle & mask;

            if (rle.empty()) continue;
            if (matteType() == MatteType::AlphaInv) {
                rle = rle - matteRle;
                painter->drawRle(VPoint(), rle);
            } else {
                // render with matteRle as clip.
                painter->drawRle(rle, matteRle);
            }
        }
    }
}

void LOTLayerMaskItem::preprocess(const VRect &clip)
{
    for (auto &i : mMasks) {
        i.preprocess(clip);
    }
}


LOTLayerMaskItem::LOTLayerMaskItem(LOTLayerData *layerData)
{
    if (!layerData->mExtra) return;

    mMasks.reserve(layerData->mExtra->mMasks.size());

    for (auto &i : layerData->mExtra->mMasks) {
        mMasks.emplace_back(i);
        mStatic &= i->isStatic();
    }
}

void LOTLayerMaskItem::update(int frameNo, const VMatrix &parentMatrix,
                              float parentAlpha, const DirtyFlag &flag)
{
    if (flag.testFlag(DirtyFlagBit::None) && isStatic()) return;

    for (auto &i : mMasks) {
        i.update(frameNo, parentMatrix, parentAlpha, flag);
    }
    mDirty = true;
}

VRle LOTLayerMaskItem::maskRle(const VRect &clipRect)
{
    if (!mDirty) return mRle;

    VRle rle;
    for (auto &i : mMasks) {
        switch (i.maskMode()) {
        case LOTMaskData::Mode::Add: {
            rle = rle + i.rle();
            break;
        }
        case LOTMaskData::Mode::Substarct: {
            if (rle.empty() && !clipRect.empty()) rle = VRle::toRle(clipRect);
            rle = rle - i.rle();
            break;
        }
        case LOTMaskData::Mode::Intersect: {
            if (rle.empty() && !clipRect.empty()) rle = VRle::toRle(clipRect);
            rle = rle & i.rle();
            break;
        }
        case LOTMaskData::Mode::Difference: {
            rle = rle ^ i.rle();
            break;
        }
        default:
            break;
        }
    }

    if (!rle.empty() && !rle.unique()) {
        mRle.clone(rle);
    } else {
        mRle = rle;
    }
    mDirty = false;
    return mRle;
}

LOTLayerItem::LOTLayerItem(LOTLayerData *layerData) : mLayerData(layerData)
{
    if (mLayerData->mHasMask)
        mLayerMask = std::make_unique<LOTLayerMaskItem>(mLayerData);
}

bool LOTLayerItem::resolveKeyPath(LOTKeyPath &keyPath, uint depth,
                                  LOTVariant &value)
{
    if (!keyPath.matches(name(), depth)) {
        return false;
    }

    if (!keyPath.skip(name())) {
        if (keyPath.fullyResolvesTo(name(), depth) &&
            transformProp(value.property())) {
            //@TODO handle propery update.
        }
    }
    return true;
}

bool LOTShapeLayerItem::resolveKeyPath(LOTKeyPath &keyPath, uint depth,
                                       LOTVariant &value)
{
    if (LOTLayerItem::resolveKeyPath(keyPath, depth, value)) {
        if (keyPath.propagate(name(), depth)) {
            uint newDepth = keyPath.nextDepth(name(), depth);
            mRoot->resolveKeyPath(keyPath, newDepth, value);
        }
        return true;
    }
    return false;
}

bool LOTCompLayerItem::resolveKeyPath(LOTKeyPath &keyPath, uint depth,
                                      LOTVariant &value)
{
    if (LOTLayerItem::resolveKeyPath(keyPath, depth, value)) {
        if (keyPath.propagate(name(), depth)) {
            uint newDepth = keyPath.nextDepth(name(), depth);
            for (const auto &layer : mLayers) {
                layer->resolveKeyPath(keyPath, newDepth, value);
            }
        }
        return true;
    }
    return false;
}

void LOTLayerItem::update(int frameNumber, const VMatrix &parentMatrix,
                          float parentAlpha)
{
    mFrameNo = frameNumber;
    // 1. check if the layer is part of the current frame
    if (!visible()) return;

    float alpha = parentAlpha * opacity(frameNo());
    if (vIsZero(alpha)) {
        mCombinedAlpha = 0;
        return;
    }

    // 2. calculate the parent matrix and alpha
    VMatrix m = matrix(frameNo());
    m *= parentMatrix;

    // 3. update the dirty flag based on the change
    if (mCombinedMatrix != m) {
        mDirtyFlag |= DirtyFlagBit::Matrix;
        mCombinedMatrix = m;
    }

    if (!vCompare(mCombinedAlpha, alpha)) {
        mDirtyFlag |= DirtyFlagBit::Alpha;
        mCombinedAlpha = alpha;
    }

    // 4. update the mask
    if (mLayerMask) {
        mLayerMask->update(frameNo(), mCombinedMatrix, mCombinedAlpha,
                           mDirtyFlag);
    }

    // 5. if no parent property change and layer is static then nothing to do.
    if (!mLayerData->precompLayer() && flag().testFlag(DirtyFlagBit::None) &&
        isStatic())
        return;

    // 6. update the content of the layer
    updateContent();

    // 7. reset the dirty flag
    mDirtyFlag = DirtyFlagBit::None;
}

VMatrix LOTLayerItem::matrix(int frameNo) const
{
    return mParentLayer
               ? (mLayerData->matrix(frameNo) * mParentLayer->matrix(frameNo))
               : mLayerData->matrix(frameNo);
}

bool LOTLayerItem::visible() const
{
    return (frameNo() >= mLayerData->inFrame() &&
            frameNo() < mLayerData->outFrame());
}

void LOTLayerItem::preprocess(const VRect& clip)
{
    // layer dosen't contribute to the frame
    if (skipRendering()) return;

    // preprocess layer masks
    if (mLayerMask) mLayerMask->preprocess(clip);

    preprocessStage(clip);
}

LOTCompLayerItem::LOTCompLayerItem(LOTLayerData *layerModel, VArenaAlloc* allocator)
    : LOTLayerItem(layerModel)
{
    if (!mLayerData->mChildren.empty())
        mLayers.reserve(mLayerData->mChildren.size());

    // 1. keep the layer in back-to-front order.
    // as lottie model keeps the data in front-toback-order.
    for (auto it = mLayerData->mChildren.crbegin();
         it != mLayerData->mChildren.rend(); ++it ) {
        auto model = static_cast<LOTLayerData *>(*it);
        auto item = createLayerItem(model, allocator);
        if (item) mLayers.push_back(item);
    }

    // 2. update parent layer
    for (const auto &layer : mLayers) {
        int id = layer->parentId();
        if (id >= 0) {
            auto search =
                std::find_if(mLayers.begin(), mLayers.end(),
                             [id](const auto &val) { return val->id() == id; });
            if (search != mLayers.end()) layer->setParentLayer(*search);
        }
    }

    // 4. check if its a nested composition
    if (!layerModel->layerSize().empty()) {
        mClipper = std::make_unique<LOTClipperItem>(layerModel->layerSize());
    }

    if (mLayers.size() > 1) setComplexContent(true);
}

void LOTCompLayerItem::render(VPainter *painter, const VRle &inheritMask,
                              const VRle &matteRle)
{
    if (vIsZero(combinedAlpha())) return;

    if (vCompare(combinedAlpha(), 1.0)) {
        renderHelper(painter, inheritMask, matteRle);
    } else {
        if (complexContent()) {
            VSize    size = painter->clipBoundingRect().size();
            VPainter srcPainter;
            VBitmap  srcBitmap(size.width(), size.height(),
                              VBitmap::Format::ARGB32_Premultiplied);
            srcPainter.begin(&srcBitmap);
            renderHelper(&srcPainter, inheritMask, matteRle);
            srcPainter.end();
            painter->drawBitmap(VPoint(), srcBitmap, uchar(combinedAlpha() * 255.0f));
        } else {
            renderHelper(painter, inheritMask, matteRle);
        }
    }
}

void LOTCompLayerItem::renderHelper(VPainter *painter, const VRle &inheritMask,
                                    const VRle &matteRle)
{
    VRle mask;
    if (mLayerMask) {
        mask = mLayerMask->maskRle(painter->clipBoundingRect());
        if (!inheritMask.empty()) mask = mask & inheritMask;
        // if resulting mask is empty then return.
        if (mask.empty()) return;
    } else {
        mask = inheritMask;
    }

    if (mClipper) {
        mask = mClipper->rle(mask);
        if (mask.empty()) return;
    }

    LOTLayerItem *matte = nullptr;
    for (const auto &layer : mLayers) {
        if (layer->hasMatte()) {
            matte = layer;
        } else {
            if (layer->visible()) {
                if (matte) {
                    if (matte->visible())
                        renderMatteLayer(painter, mask, matteRle, matte,
                                         layer);
                } else {
                    layer->render(painter, mask, matteRle);
                }
            }
            matte = nullptr;
        }
    }
}

void LOTCompLayerItem::renderMatteLayer(VPainter *painter, const VRle &mask,
                                        const VRle &  matteRle,
                                        LOTLayerItem *layer, LOTLayerItem *src)
{
    VSize size = painter->clipBoundingRect().size();
    // Decide if we can use fast matte.
    // 1. draw src layer to matte buffer
    VPainter srcPainter;
    src->bitmap().reset(size.width(), size.height(),
                        VBitmap::Format::ARGB32_Premultiplied);
    srcPainter.begin(&src->bitmap());
    src->render(&srcPainter, mask, matteRle);
    srcPainter.end();

    // 2. draw layer to layer buffer
    VPainter layerPainter;
    layer->bitmap().reset(size.width(), size.height(),
                          VBitmap::Format::ARGB32_Premultiplied);
    layerPainter.begin(&layer->bitmap());
    layer->render(&layerPainter, mask, matteRle);

    // 2.1update composition mode
    switch (layer->matteType()) {
    case MatteType::Alpha:
    case MatteType::Luma: {
        layerPainter.setBlendMode(BlendMode::DestIn);
        break;
    }
    case MatteType::AlphaInv:
    case MatteType::LumaInv: {
        layerPainter.setBlendMode(BlendMode::DestOut);
        break;
    }
    default:
        break;
    }

    // 2.2 update srcBuffer if the matte is luma type
    if (layer->matteType() == MatteType::Luma ||
        layer->matteType() == MatteType::LumaInv) {
        src->bitmap().updateLuma();
    }

    // 2.3 draw src buffer as mask
    layerPainter.drawBitmap(VPoint(), src->bitmap());
    layerPainter.end();
    // 3. draw the result buffer into painter
    painter->drawBitmap(VPoint(), layer->bitmap());
}

void LOTClipperItem::update(const VMatrix &matrix)
{
    mPath.reset();
    mPath.addRect(VRectF(0, 0, mSize.width(), mSize.height()));
    mPath.transform(matrix);
    mRasterRequest = true;
}

void LOTClipperItem::preprocess(const VRect &clip)
{
    if (mRasterRequest)
        mRasterizer.rasterize(mPath, FillRule::Winding, clip);

    mRasterRequest = false;
}

VRle LOTClipperItem::rle(const VRle& mask)
{
    if (mask.empty())
        return mRasterizer.rle();

    mMaskedRle.clone(mask);
    mMaskedRle &= mRasterizer.rle();
    return mMaskedRle;
}

void LOTCompLayerItem::updateContent()
{
    if (mClipper && flag().testFlag(DirtyFlagBit::Matrix)) {
        mClipper->update(combinedMatrix());
    }
    int   mappedFrame = mLayerData->timeRemap(frameNo());
    float alpha = combinedAlpha();
    if (complexContent()) alpha = 1;
    for (const auto &layer : mLayers) {
        layer->update(mappedFrame, combinedMatrix(), alpha);
    }
}

void LOTCompLayerItem::preprocessStage(const VRect &clip)
{
    // if layer has clipper
    if (mClipper) mClipper->preprocess(clip);

    LOTLayerItem *matte = nullptr;
    for (const auto &layer : mLayers) {
        if (layer->hasMatte()) {
            matte = layer;
        } else {
            if (layer->visible()) {
                if (matte) {
                    if (matte->visible()) {
                        layer->preprocess(clip);
                        matte->preprocess(clip);
                    }
                } else {
                    layer->preprocess(clip);
                }
            }
            matte = nullptr;
        }
    }
}

LOTSolidLayerItem::LOTSolidLayerItem(LOTLayerData *layerData)
    : LOTLayerItem(layerData)
{
    mDrawableList = &mRenderNode;
}

void LOTSolidLayerItem::updateContent()
{
    if (flag() & DirtyFlagBit::Matrix) {
        VPath path;
        path.addRect(
            VRectF(0, 0,
                   mLayerData->layerSize().width(),
                   mLayerData->layerSize().height()));
        path.transform(combinedMatrix());
        mRenderNode.mFlag |= VDrawable::DirtyState::Path;
        mRenderNode.mPath = path;
    }
    if (flag() & DirtyFlagBit::Alpha) {
        LottieColor color = mLayerData->solidColor();
        VBrush      brush(color.toColor(combinedAlpha()));
        mRenderNode.setBrush(brush);
        mRenderNode.mFlag |= VDrawable::DirtyState::Brush;
    }
}

void LOTSolidLayerItem::preprocessStage(const VRect& clip)
{
    mRenderNode.preprocess(clip);
}

DrawableList LOTSolidLayerItem::renderList()
{
    if (skipRendering()) return {};

    return {&mDrawableList , 1};
}

LOTImageLayerItem::LOTImageLayerItem(LOTLayerData *layerData)
    : LOTLayerItem(layerData)
{
    mDrawableList = &mRenderNode;

    if (!mLayerData->asset()) return;

    mTexture.mBitmap = mLayerData->asset()->bitmap();
    VBrush brush(&mTexture);
    mRenderNode.setBrush(brush);
}

void LOTImageLayerItem::updateContent()
{
    if (!mLayerData->asset()) return;

    if (flag() & DirtyFlagBit::Matrix) {
        VPath path;
        path.addRect(VRectF(0, 0, mLayerData->asset()->mWidth,
                            mLayerData->asset()->mHeight));
        path.transform(combinedMatrix());
        mRenderNode.mFlag |= VDrawable::DirtyState::Path;
        mRenderNode.mPath = path;
        mTexture.mMatrix = combinedMatrix();
    }

    if (flag() & DirtyFlagBit::Alpha) {
        mTexture.mAlpha = int(combinedAlpha() * 255);
    }
}

void LOTImageLayerItem::preprocessStage(const VRect& clip)
{
    mRenderNode.preprocess(clip);
}

DrawableList LOTImageLayerItem::renderList()
{
    if (skipRendering()) return {};

    return {&mDrawableList , 1};
}

LOTNullLayerItem::LOTNullLayerItem(LOTLayerData *layerData)
    : LOTLayerItem(layerData)
{
}
void LOTNullLayerItem::updateContent() {}

static LOTContentItem*
createContentItem(LOTData *contentData, VArenaAlloc* allocator)
{
    switch (contentData->type()) {
    case LOTData::Type::ShapeGroup: {
        return allocator->make<LOTContentGroupItem>(
            static_cast<LOTGroupData *>(contentData), allocator);
    }
    case LOTData::Type::Rect: {
        return allocator->make<LOTRectItem>(
            static_cast<LOTRectData *>(contentData));
    }
    case LOTData::Type::Ellipse: {
        return allocator->make<LOTEllipseItem>(
            static_cast<LOTEllipseData *>(contentData));
    }
    case LOTData::Type::Shape: {
        return allocator->make<LOTShapeItem>(
            static_cast<LOTShapeData *>(contentData));
    }
    case LOTData::Type::Polystar: {
        return allocator->make<LOTPolystarItem>(
            static_cast<LOTPolystarData *>(contentData));
    }
    case LOTData::Type::Fill: {
        return allocator->make<LOTFillItem>(
            static_cast<LOTFillData *>(contentData));
    }
    case LOTData::Type::GFill: {
        return allocator->make<LOTGFillItem>(
            static_cast<LOTGFillData *>(contentData));
    }
    case LOTData::Type::Stroke: {
        return allocator->make<LOTStrokeItem>(
            static_cast<LOTStrokeData *>(contentData));
    }
    case LOTData::Type::GStroke: {
        return allocator->make<LOTGStrokeItem>(
            static_cast<LOTGStrokeData *>(contentData));
    }
    case LOTData::Type::Repeater: {
        return allocator->make<LOTRepeaterItem>(
            static_cast<LOTRepeaterData *>(contentData), allocator);
    }
    case LOTData::Type::Trim: {
        return allocator->make<LOTTrimItem>(
            static_cast<LOTTrimData *>(contentData));
    }
    default:
        return nullptr;
        break;
    }
}

LOTShapeLayerItem::LOTShapeLayerItem(LOTLayerData *layerData, VArenaAlloc* allocator)
    : LOTLayerItem(layerData),
      mRoot(allocator->make<LOTContentGroupItem>(nullptr, allocator))
{
    mRoot->addChildren(layerData, allocator);

    std::vector<LOTPathDataItem *> list;
    mRoot->processPaintItems(list);

    if (layerData->hasPathOperator()) {
        list.clear();
        mRoot->processTrimItems(list);
    }
}

void LOTShapeLayerItem::updateContent()
{
    mRoot->update(frameNo(), combinedMatrix(), combinedAlpha(), flag());

    if (mLayerData->hasPathOperator()) {
        mRoot->applyTrim();
    }
}

void LOTShapeLayerItem::preprocessStage(const VRect& clip)
{
    mDrawableList.clear();
    mRoot->renderList(mDrawableList);

    for (auto &drawable : mDrawableList) drawable->preprocess(clip);

}

DrawableList LOTShapeLayerItem::renderList()
{
    if (skipRendering()) return {};

    mDrawableList.clear();
    mRoot->renderList(mDrawableList);

    if (mDrawableList.empty()) return {};

    return {mDrawableList.data() , mDrawableList.size()};
}

bool LOTContentGroupItem::resolveKeyPath(LOTKeyPath &keyPath, uint depth,
                                         LOTVariant &value)
{
   if (!keyPath.skip(name())) {
        if (!keyPath.matches(mModel.name(), depth)) {
             return false;
        }

        if (!keyPath.skip(mModel.name())) {
             if (keyPath.fullyResolvesTo(mModel.name(), depth) &&
                 transformProp(value.property())) {
                  mModel.filter().addValue(value);
             }
        }
   }

    if (keyPath.propagate(name(), depth)) {
        uint newDepth = keyPath.nextDepth(name(), depth);
        for (auto &child : mContents) {
            child->resolveKeyPath(keyPath, newDepth, value);
        }
    }
    return true;
}

bool LOTFillItem::resolveKeyPath(LOTKeyPath &keyPath, uint depth,
                                 LOTVariant &value)
{
    if (!keyPath.matches(mModel.name(), depth)) {
        return false;
    }

    if (keyPath.fullyResolvesTo(mModel.name(), depth) &&
        fillProp(value.property())) {
        mModel.filter().addValue(value);
        return true;
    }
    return false;
}

bool LOTStrokeItem::resolveKeyPath(LOTKeyPath &keyPath, uint depth,
                                   LOTVariant &value)
{
    if (!keyPath.matches(mModel.name(), depth)) {
        return false;
    }

    if (keyPath.fullyResolvesTo(mModel.name(), depth) &&
        strokeProp(value.property())) {
        mModel.filter().addValue(value);
        return true;
    }
    return false;
}

LOTContentGroupItem::LOTContentGroupItem(LOTGroupData *data, VArenaAlloc* allocator)
    : mModel(data)
{
    addChildren(data, allocator);
}

void LOTContentGroupItem::addChildren(LOTGroupData *data, VArenaAlloc* allocator)
{
    if (!data) return;

    if (!data->mChildren.empty()) mContents.reserve(data->mChildren.size());

    // keep the content in back-to-front order.
    // as lottie model keeps it in front-to-back order.
    for (auto it = data->mChildren.crbegin(); it != data->mChildren.rend();
         ++it ) {
        auto content = createContentItem(*it, allocator);
        if (content) {
            mContents.push_back(content);
        }
    }
}

void LOTContentGroupItem::update(int frameNo, const VMatrix &parentMatrix,
                                 float parentAlpha, const DirtyFlag &flag)
{
    DirtyFlag newFlag = flag;
    float alpha;

    if (mModel.hasModel() && mModel.transform()) {
        VMatrix m = mModel.matrix(frameNo);

        m *= parentMatrix;
        if (!(flag & DirtyFlagBit::Matrix) && !mModel.transform()->isStatic() &&
            (m != mMatrix)) {
            newFlag |= DirtyFlagBit::Matrix;
        }

        mMatrix = m;

        alpha = parentAlpha * mModel.transform()->opacity(frameNo);
        if (!vCompare(alpha, parentAlpha)) {
            newFlag |= DirtyFlagBit::Alpha;
        }
    } else {
        mMatrix = parentMatrix;
        alpha = parentAlpha;
    }

    for (const auto &content : mContents) {
        content->update(frameNo, matrix(), alpha, newFlag);
    }
}

void LOTContentGroupItem::applyTrim()
{
    for (auto i = mContents.rbegin(); i != mContents.rend(); ++i) {
        auto content = (*i);
        switch (content->type()) {
        case ContentType::Trim: {
            static_cast<LOTTrimItem *>(content)->update();
            break;
        }
        case ContentType::Group: {
            static_cast<LOTContentGroupItem *>(content)->applyTrim();
            break;
        }
        default:
            break;
        }
    }
}

void LOTContentGroupItem::renderList(std::vector<VDrawable *> &list)
{
    for (const auto &content : mContents) {
        content->renderList(list);
    }
}

void LOTContentGroupItem::processPaintItems(
    std::vector<LOTPathDataItem *> &list)
{
    size_t curOpCount = list.size();
    for (auto i = mContents.rbegin(); i != mContents.rend(); ++i) {
        auto content = (*i);
        switch (content->type()) {
        case ContentType::Path: {
            auto pathItem = static_cast<LOTPathDataItem *>(content);
            pathItem->setParent(this);
            list.push_back(pathItem);
            break;
        }
        case ContentType::Paint: {
            static_cast<LOTPaintDataItem *>(content)->addPathItems(list,
                                                                   curOpCount);
            break;
        }
        case ContentType::Group: {
            static_cast<LOTContentGroupItem *>(content)->processPaintItems(
                list);
            break;
        }
        default:
            break;
        }
    }
}

void LOTContentGroupItem::processTrimItems(std::vector<LOTPathDataItem *> &list)
{
    size_t curOpCount = list.size();
    for (auto i = mContents.rbegin(); i != mContents.rend(); ++i) {
        auto content = (*i);

        switch (content->type()) {
        case ContentType::Path: {
            list.push_back(static_cast<LOTPathDataItem *>(content));
            break;
        }
        case ContentType::Trim: {
            static_cast<LOTTrimItem *>(content)->addPathItems(list, curOpCount);
            break;
        }
        case ContentType::Group: {
            static_cast<LOTContentGroupItem *>(content)->processTrimItems(list);
            break;
        }
        default:
            break;
        }
    }
}

/*
 * LOTPathDataItem uses 2 path objects for path object reuse.
 * mLocalPath -  keeps track of the local path of the item before
 * applying path operation and transformation.
 * mTemp - keeps a referece to the mLocalPath and can be updated by the
 *          path operation objects(trim, merge path),
 * We update the DirtyPath flag if the path needs to be updated again
 * beacuse of local path or matrix or some path operation has changed which
 * affects the final path.
 * The PaintObject queries the dirty flag to check if it needs to compute the
 * final path again and calls finalPath() api to do the same.
 * finalPath() api passes a result Object so that we keep only one copy of
 * the path object in the paintItem (for memory efficiency).
 *   NOTE: As path objects are COW objects we have to be
 * carefull about the refcount so that we don't generate deep copy while
 * modifying the path objects.
 */
void LOTPathDataItem::update(int              frameNo, const VMatrix &, float,
                             const DirtyFlag &flag)
{
    mDirtyPath = false;

    // 1. update the local path if needed
    if (hasChanged(frameNo)) {
        // loose the reference to mLocalPath if any
        // from the last frame update.
        mTemp = VPath();

        updatePath(mLocalPath, frameNo);
        mDirtyPath = true;
    }
    // 2. keep a reference path in temp in case there is some
    // path operation like trim which will update the path.
    // we don't want to update the local path.
    mTemp = mLocalPath;

    // 3. mark the path dirty if matrix has changed.
    if (flag & DirtyFlagBit::Matrix) {
        mDirtyPath = true;
    }
}

void LOTPathDataItem::finalPath(VPath& result)
{
    result.addPath(mTemp, static_cast<LOTContentGroupItem *>(parent())->matrix());
}

LOTRectItem::LOTRectItem(LOTRectData *data)
    : LOTPathDataItem(data->isStatic()), mData(data)
{
}

void LOTRectItem::updatePath(VPath &path, int frameNo)
{
    VPointF pos = mData->mPos.value(frameNo);
    VPointF size = mData->mSize.value(frameNo);
    float   roundness = mData->mRound.value(frameNo);
    VRectF  r(pos.x() - size.x() / 2, pos.y() - size.y() / 2, size.x(),
             size.y());

    path.reset();
    path.addRoundRect(r, roundness, mData->direction());
}

LOTEllipseItem::LOTEllipseItem(LOTEllipseData *data)
    : LOTPathDataItem(data->isStatic()), mData(data)
{
}

void LOTEllipseItem::updatePath(VPath &path, int frameNo)
{
    VPointF pos = mData->mPos.value(frameNo);
    VPointF size = mData->mSize.value(frameNo);
    VRectF  r(pos.x() - size.x() / 2, pos.y() - size.y() / 2, size.x(),
             size.y());

    path.reset();
    path.addOval(r, mData->direction());
}

LOTShapeItem::LOTShapeItem(LOTShapeData *data)
    : LOTPathDataItem(data->isStatic()), mData(data)
{
}

void LOTShapeItem::updatePath(VPath &path, int frameNo)
{
    mData->mShape.updatePath(frameNo, path);
}

LOTPolystarItem::LOTPolystarItem(LOTPolystarData *data)
    : LOTPathDataItem(data->isStatic()), mData(data)
{
}

void LOTPolystarItem::updatePath(VPath &path, int frameNo)
{
    VPointF pos = mData->mPos.value(frameNo);
    float   points = mData->mPointCount.value(frameNo);
    float   innerRadius = mData->mInnerRadius.value(frameNo);
    float   outerRadius = mData->mOuterRadius.value(frameNo);
    float   innerRoundness = mData->mInnerRoundness.value(frameNo);
    float   outerRoundness = mData->mOuterRoundness.value(frameNo);
    float   rotation = mData->mRotation.value(frameNo);

    path.reset();
    VMatrix m;

    if (mData->mPolyType == LOTPolystarData::PolyType::Star) {
        path.addPolystar(points, innerRadius, outerRadius, innerRoundness,
                         outerRoundness, 0.0, 0.0, 0.0, mData->direction());
    } else {
        path.addPolygon(points, outerRadius, outerRoundness, 0.0, 0.0, 0.0,
                        mData->direction());
    }

    m.translate(pos.x(), pos.y()).rotate(rotation);
    m.rotate(rotation);
    path.transform(m);
}

/*
 * PaintData Node handling
 *
 */
LOTPaintDataItem::LOTPaintDataItem(bool staticContent)
    : mStaticContent(staticContent)
{
}

void LOTPaintDataItem::update(int   frameNo, const VMatrix & parentMatrix,
                              float parentAlpha, const DirtyFlag &/*flag*/)
{
    mRenderNodeUpdate = true;
    mContentToRender = updateContent(frameNo, parentMatrix, parentAlpha);
}

void LOTPaintDataItem::updateRenderNode()
{
    bool dirty = false;
    for (auto &i : mPathItems) {
        if (i->dirty()) {
            dirty = true;
            break;
        }
    }

    if (dirty) {
        mPath.reset();
        for (const auto &i : mPathItems) {
            i->finalPath(mPath);
        }
        mDrawable.setPath(mPath);
    } else {
        if (mDrawable.mFlag & VDrawable::DirtyState::Path)
            mDrawable.mPath = mPath;
    }
}

void LOTPaintDataItem::renderList(std::vector<VDrawable *> &list)
{
    if (mRenderNodeUpdate) {
        updateRenderNode();
        mRenderNodeUpdate = false;
    }

    // Q: Why we even update the final path if we don't have content
    // to render ?
    // Ans: We update the render nodes because we will loose the
    // dirty path information at end of this frame.
    // so if we return early without updating the final path.
    // in the subsequent frame when we have content to render but
    // we may not able to update our final path properly as we
    // don't know what paths got changed in between.
    if (mContentToRender) list.push_back(&mDrawable);
}

void LOTPaintDataItem::addPathItems(std::vector<LOTPathDataItem *> &list,
                                    size_t                          startOffset)
{
    std::copy(list.begin() + startOffset, list.end(),
              std::back_inserter(mPathItems));
}

LOTFillItem::LOTFillItem(LOTFillData *data)
    : LOTPaintDataItem(data->isStatic()), mModel(data)
{
    mDrawable.setName(mModel.name());
}

bool LOTFillItem::updateContent(int frameNo, const VMatrix &, float alpha)
{
    auto combinedAlpha = alpha * mModel.opacity(frameNo);
    auto color = mModel.color(frameNo).toColor(combinedAlpha);

    VBrush brush(color);
    mDrawable.setBrush(brush);
    mDrawable.setFillRule(mModel.fillRule());

    return !color.isTransparent();
}

LOTGFillItem::LOTGFillItem(LOTGFillData *data)
    : LOTPaintDataItem(data->isStatic()), mData(data)
{
    mDrawable.setName(mData->name());
}

bool LOTGFillItem::updateContent(int frameNo, const VMatrix &matrix, float alpha)
{
    float combinedAlpha = alpha * mData->opacity(frameNo);

    mData->update(mGradient, frameNo);
    mGradient->setAlpha(combinedAlpha);
    mGradient->mMatrix = matrix;
    mDrawable.setBrush(VBrush(mGradient.get()));
    mDrawable.setFillRule(mData->fillRule());

    return !vIsZero(combinedAlpha);
}

LOTStrokeItem::LOTStrokeItem(LOTStrokeData *data)
    : LOTPaintDataItem(data->isStatic()), mModel(data)
{
    mDrawable.setName(mModel.name());
    if (mModel.hasDashInfo()) {
        mDrawable.setType(VDrawable::Type::StrokeWithDash);
    } else {
        mDrawable.setType(VDrawable::Type::Stroke);
    }
}

static thread_local std::vector<float> Dash_Vector;

bool LOTStrokeItem::updateContent(int frameNo, const VMatrix &matrix, float alpha)
{
    auto combinedAlpha = alpha * mModel.opacity(frameNo);
    auto color = mModel.color(frameNo).toColor(combinedAlpha);

    VBrush brush(color);
    mDrawable.setBrush(brush);
    float scale = matrix.scale();
    mDrawable.setStrokeInfo(mModel.capStyle(), mModel.joinStyle(),
                            mModel.miterLimit(), mModel.strokeWidth(frameNo) * scale);

    if (mModel.hasDashInfo()) {
        Dash_Vector.clear();
        mModel.getDashInfo(frameNo, Dash_Vector);
        if (!Dash_Vector.empty()) {
            for (auto &elm : Dash_Vector) elm *= scale;
            mDrawable.setDashInfo(Dash_Vector);
        }
    }

    return !color.isTransparent();
}

LOTGStrokeItem::LOTGStrokeItem(LOTGStrokeData *data)
    : LOTPaintDataItem(data->isStatic()), mData(data)
{
    mDrawable.setName(mData->name());
    if (mData->hasDashInfo()) {
        mDrawable.setType(VDrawable::Type::StrokeWithDash);
    } else {
        mDrawable.setType(VDrawable::Type::Stroke);
    }
}

bool LOTGStrokeItem::updateContent(int frameNo, const VMatrix &matrix, float alpha)
{
    float combinedAlpha = alpha * mData->opacity(frameNo);

    mData->update(mGradient, frameNo);
    mGradient->setAlpha(combinedAlpha);
    mGradient->mMatrix = matrix;
    auto scale = mGradient->mMatrix.scale();
    mDrawable.setBrush(VBrush(mGradient.get()));
    mDrawable.setStrokeInfo(mData->capStyle(),  mData->joinStyle(),
                            mData->miterLimit(), mData->width(frameNo) * scale);

    if (mData->hasDashInfo()) {
        Dash_Vector.clear();
        mData->getDashInfo(frameNo, Dash_Vector);
        if (!Dash_Vector.empty()) {
            for (auto &elm : Dash_Vector) elm *= scale;
            mDrawable.setDashInfo(Dash_Vector);
        }
    }

    return !vIsZero(combinedAlpha);
}

LOTTrimItem::LOTTrimItem(LOTTrimData *data)
    : mData(data)
{
}

void LOTTrimItem::update(int frameNo, const VMatrix & /*parentMatrix*/,
                         float /*parentAlpha*/, const DirtyFlag & /*flag*/)
{
    mDirty = false;

    if (mCache.mFrameNo == frameNo) return;

    LOTTrimData::Segment segment = mData->segment(frameNo);

    if (!(vCompare(mCache.mSegment.start, segment.start) &&
          vCompare(mCache.mSegment.end, segment.end))) {
        mDirty = true;
        mCache.mSegment = segment;
    }
    mCache.mFrameNo = frameNo;
}

void LOTTrimItem::update()
{
    // when both path and trim are not dirty
    if (!(mDirty || pathDirty())) return;

    if (vCompare(mCache.mSegment.start, mCache.mSegment.end)) {
        for (auto &i : mPathItems) {
            i->updatePath(VPath());
        }
        return;
    }

    if (vCompare(std::fabs(mCache.mSegment.start - mCache.mSegment.end), 1)) {
        for (auto &i : mPathItems) {
            i->updatePath(i->localPath());
        }
        return;
    }

    if (mData->type() == LOTTrimData::TrimType::Simultaneously) {
        for (auto &i : mPathItems) {
            mPathMesure.setRange(mCache.mSegment.start, mCache.mSegment.end);
            i->updatePath(mPathMesure.trim(i->localPath()));
        }
    } else {  // LOTTrimData::TrimType::Individually
        float totalLength = 0.0;
        for (auto &i : mPathItems) {
            totalLength += i->localPath().length();
        }
        float start = totalLength * mCache.mSegment.start;
        float end = totalLength * mCache.mSegment.end;

        if (start < end) {
            float curLen = 0.0;
            for (auto &i : mPathItems) {
                if (curLen > end) {
                    // update with empty path.
                    i->updatePath(VPath());
                    continue;
                }
                float len = i->localPath().length();

                if (curLen < start && curLen + len < start) {
                    curLen += len;
                    // update with empty path.
                    i->updatePath(VPath());
                    continue;
                } else if (start <= curLen && end >= curLen + len) {
                    // inside segment
                    curLen += len;
                    continue;
                } else {
                    float local_start = start > curLen ? start - curLen : 0;
                    local_start /= len;
                    float local_end = curLen + len < end ? len : end - curLen;
                    local_end /= len;
                    mPathMesure.setRange(local_start, local_end);
                    i->updatePath(mPathMesure.trim(i->localPath()));
                    curLen += len;
                }
            }
        }
    }
}

void LOTTrimItem::addPathItems(std::vector<LOTPathDataItem *> &list,
                               size_t                          startOffset)
{
    std::copy(list.begin() + startOffset, list.end(),
              std::back_inserter(mPathItems));
}

LOTRepeaterItem::LOTRepeaterItem(LOTRepeaterData *data, VArenaAlloc* allocator) : mRepeaterData(data)
{
    assert(mRepeaterData->content());

    mCopies = mRepeaterData->maxCopies();

    for (int i = 0; i < mCopies; i++) {
        auto content =
            allocator->make<LOTContentGroupItem>(mRepeaterData->content(), allocator);
        //content->setParent(this);
        mContents.push_back(content);
    }
}

void LOTRepeaterItem::update(int frameNo, const VMatrix &parentMatrix,
                             float parentAlpha, const DirtyFlag &flag)
{
    DirtyFlag newFlag = flag;

    float copies = mRepeaterData->copies(frameNo);
    int   visibleCopies = int(copies);

    if (visibleCopies == 0) {
        mHidden = true;
        return;
    }

    mHidden = false;

    if (!mRepeaterData->isStatic()) newFlag |= DirtyFlagBit::Matrix;

    float offset = mRepeaterData->offset(frameNo);
    float startOpacity = mRepeaterData->mTransform.startOpacity(frameNo);
    float endOpacity = mRepeaterData->mTransform.endOpacity(frameNo);

    newFlag |= DirtyFlagBit::Alpha;

    for (int i = 0; i < mCopies; ++i) {
        float newAlpha =
            parentAlpha * lerp(startOpacity, endOpacity, i / copies);

        // hide rest of the copies , @TODO find a better solution.
        if (i >= visibleCopies) newAlpha = 0;

        VMatrix result = mRepeaterData->mTransform.matrix(frameNo, i + offset) *
                         parentMatrix;
        mContents[i]->update(frameNo, result, newAlpha, newFlag);
    }
}

void LOTRepeaterItem::renderList(std::vector<VDrawable *> &list)
{
    if (mHidden) return;
    return LOTContentGroupItem::renderList(list);
}

LOTCApiData::LOTCApiData()
{
    mLayer.mMaskList.ptr = nullptr;
    mLayer.mMaskList.size = 0;
    mLayer.mLayerList.ptr = nullptr;
    mLayer.mLayerList.size = 0;
    mLayer.mNodeList.ptr = nullptr;
    mLayer.mNodeList.size = 0;
    mLayer.mMatte = MatteNone;
    mLayer.mVisible = 0;
    mLayer.mAlpha = 255;
    mLayer.mClipPath.ptPtr = nullptr;
    mLayer.mClipPath.elmPtr = nullptr;
    mLayer.mClipPath.ptCount = 0;
    mLayer.mClipPath.elmCount = 0;
    mLayer.keypath = nullptr;
}

void LOTCompItem::buildRenderTree()
{
    mRootLayer->buildLayerNode();
}

const LOTLayerNode *LOTCompItem::renderTree() const
{
    return &mRootLayer->clayer();
}

void LOTCompLayerItem::buildLayerNode()
{
    LOTLayerItem::buildLayerNode();
    if (mClipper) {
        const auto &elm = mClipper->mPath.elements();
        const auto &pts = mClipper->mPath.points();
        auto ptPtr = reinterpret_cast<const float *>(pts.data());
        auto elmPtr = reinterpret_cast<const char *>(elm.data());
        clayer().mClipPath.ptPtr = ptPtr;
        clayer().mClipPath.elmPtr = elmPtr;
        clayer().mClipPath.ptCount = 2 * pts.size();
        clayer().mClipPath.elmCount = elm.size();
    }
    if (mLayers.size() != clayers().size()) {
        for (const auto &layer : mLayers) {
            layer->buildLayerNode();
            clayers().push_back(&layer->clayer());
        }
        clayer().mLayerList.ptr = clayers().data();
        clayer().mLayerList.size = clayers().size();
    } else {
        for (const auto &layer : mLayers) {
            layer->buildLayerNode();
        }
    }
}


void LOTShapeLayerItem::buildLayerNode()
{
    LOTLayerItem::buildLayerNode();

    auto renderlist = renderList();

    cnodes().clear();
    for (auto &i : renderlist) {
        auto lotDrawable = static_cast<LOTDrawable *>(i);
        lotDrawable->sync();
        cnodes().push_back(lotDrawable->mCNode.get());
    }
    clayer().mNodeList.ptr = cnodes().data();
    clayer().mNodeList.size = cnodes().size();
}

void LOTLayerItem::buildLayerNode()
{
    if (!mCApiData) {
        mCApiData = std::make_unique<LOTCApiData>();
        clayer().keypath = name();
    }
    if (complexContent()) clayer().mAlpha = uchar(combinedAlpha() * 255.f);
    clayer().mVisible = visible();
    // update matte
    if (hasMatte()) {
        switch (mLayerData->mMatteType) {
        case MatteType::Alpha:
            clayer().mMatte = MatteAlpha;
            break;
        case MatteType::AlphaInv:
            clayer().mMatte = MatteAlphaInv;
            break;
        case MatteType::Luma:
            clayer().mMatte = MatteLuma;
            break;
        case MatteType::LumaInv:
            clayer().mMatte = MatteLumaInv;
            break;
        default:
            clayer().mMatte = MatteNone;
            break;
        }
    }
    if (mLayerMask) {
        cmasks().clear();
        cmasks().resize(mLayerMask->mMasks.size());
        size_t i = 0;
        for (const auto &mask : mLayerMask->mMasks) {
            auto       &cNode = cmasks()[i++];
            const auto &elm = mask.mFinalPath.elements();
            const auto &pts = mask.mFinalPath.points();
            auto ptPtr = reinterpret_cast<const float *>(pts.data());
            auto elmPtr = reinterpret_cast<const char *>(elm.data());
            cNode.mPath.ptPtr = ptPtr;
            cNode.mPath.ptCount = pts.size();
            cNode.mPath.elmPtr = elmPtr;
            cNode.mPath.elmCount = elm.size();
            cNode.mAlpha = uchar(mask.mCombinedAlpha * 255.0f);
            switch (mask.maskMode()) {
            case LOTMaskData::Mode::Add:
                cNode.mMode = MaskAdd;
                break;
            case LOTMaskData::Mode::Substarct:
                cNode.mMode = MaskSubstract;
                break;
            case LOTMaskData::Mode::Intersect:
                cNode.mMode = MaskIntersect;
                break;
            case LOTMaskData::Mode::Difference:
                cNode.mMode = MaskDifference;
                break;
            default:
                cNode.mMode = MaskAdd;
                break;
            }
        }
        clayer().mMaskList.ptr = cmasks().data();
        clayer().mMaskList.size = cmasks().size();
    }
}

void LOTSolidLayerItem::buildLayerNode()
{
    LOTLayerItem::buildLayerNode();

    auto renderlist = renderList();

    cnodes().clear();
    for (auto &i : renderlist) {
        auto lotDrawable = static_cast<LOTDrawable *>(i);
        lotDrawable->sync();
        cnodes().push_back(lotDrawable->mCNode.get());
    }
    clayer().mNodeList.ptr = cnodes().data();
    clayer().mNodeList.size = cnodes().size();
}

void LOTImageLayerItem::buildLayerNode()
{
    LOTLayerItem::buildLayerNode();

    auto renderlist = renderList();

    cnodes().clear();
    for (auto &i : renderlist) {
        auto lotDrawable = static_cast<LOTDrawable *>(i);
        lotDrawable->sync();

        lotDrawable->mCNode->mImageInfo.data =
            lotDrawable->mBrush.mTexture->mBitmap.data();
        lotDrawable->mCNode->mImageInfo.width =
            int(lotDrawable->mBrush.mTexture->mBitmap.width());
        lotDrawable->mCNode->mImageInfo.height =
            int(lotDrawable->mBrush.mTexture->mBitmap.height());

        lotDrawable->mCNode->mImageInfo.mMatrix.m11 = combinedMatrix().m_11();
        lotDrawable->mCNode->mImageInfo.mMatrix.m12 = combinedMatrix().m_12();
        lotDrawable->mCNode->mImageInfo.mMatrix.m13 = combinedMatrix().m_13();

        lotDrawable->mCNode->mImageInfo.mMatrix.m21 = combinedMatrix().m_21();
        lotDrawable->mCNode->mImageInfo.mMatrix.m22 = combinedMatrix().m_22();
        lotDrawable->mCNode->mImageInfo.mMatrix.m23 = combinedMatrix().m_23();

        lotDrawable->mCNode->mImageInfo.mMatrix.m31 = combinedMatrix().m_tx();
        lotDrawable->mCNode->mImageInfo.mMatrix.m32 = combinedMatrix().m_ty();
        lotDrawable->mCNode->mImageInfo.mMatrix.m33 = combinedMatrix().m_33();

        // Alpha calculation already combined.
        lotDrawable->mCNode->mImageInfo.mAlpha = uchar(lotDrawable->mBrush.mTexture->mAlpha);

        cnodes().push_back(lotDrawable->mCNode.get());
    }
    clayer().mNodeList.ptr = cnodes().data();
    clayer().mNodeList.size = cnodes().size();
}

static void updateGStops(LOTNode *n, const VGradient *grad)
{
    if (grad->mStops.size() != n->mGradient.stopCount) {
        if (n->mGradient.stopCount) free(n->mGradient.stopPtr);
        n->mGradient.stopCount = grad->mStops.size();
        n->mGradient.stopPtr = (LOTGradientStop *)malloc(
            n->mGradient.stopCount * sizeof(LOTGradientStop));
    }

    LOTGradientStop *ptr = n->mGradient.stopPtr;
    for (const auto &i : grad->mStops) {
        ptr->pos = i.first;
        ptr->a = uchar(i.second.alpha() * grad->alpha());
        ptr->r = i.second.red();
        ptr->g = i.second.green();
        ptr->b = i.second.blue();
        ptr++;
    }
}

void LOTDrawable::sync()
{
    if (!mCNode) {
        mCNode = std::make_unique<LOTNode>();
        mCNode->mGradient.stopPtr = nullptr;
        mCNode->mGradient.stopCount = 0;
    }

    mCNode->mFlag = ChangeFlagNone;
    if (mFlag & DirtyState::None) return;

    if (mFlag & DirtyState::Path) {
        applyDashOp();
        const std::vector<VPath::Element> &elm = mPath.elements();
        const std::vector<VPointF> &       pts = mPath.points();
        const float *ptPtr = reinterpret_cast<const float *>(pts.data());
        const char * elmPtr = reinterpret_cast<const char *>(elm.data());
        mCNode->mPath.elmPtr = elmPtr;
        mCNode->mPath.elmCount = elm.size();
        mCNode->mPath.ptPtr = ptPtr;
        mCNode->mPath.ptCount = 2 * pts.size();
        mCNode->mFlag |= ChangeFlagPath;
        mCNode->keypath = name();
    }

    if (mStrokeInfo) {
        mCNode->mStroke.width = mStrokeInfo->width;
        mCNode->mStroke.miterLimit = mStrokeInfo->miterLimit;
        mCNode->mStroke.enable = 1;

        switch (mStrokeInfo->cap) {
        case CapStyle::Flat:
            mCNode->mStroke.cap = LOTCapStyle::CapFlat;
            break;
        case CapStyle::Square:
            mCNode->mStroke.cap = LOTCapStyle::CapSquare;
            break;
        case CapStyle::Round:
            mCNode->mStroke.cap = LOTCapStyle::CapRound;
            break;
        }

        switch (mStrokeInfo->join) {
        case JoinStyle::Miter:
            mCNode->mStroke.join = LOTJoinStyle::JoinMiter;
            break;
        case JoinStyle::Bevel:
            mCNode->mStroke.join = LOTJoinStyle::JoinBevel;
            break;
        case JoinStyle::Round:
            mCNode->mStroke.join = LOTJoinStyle::JoinRound;
            break;
        default:
            mCNode->mStroke.join = LOTJoinStyle::JoinMiter;
            break;
        }
    } else {
        mCNode->mStroke.enable = 0;
    }

    switch (mFillRule) {
    case FillRule::EvenOdd:
        mCNode->mFillRule = LOTFillRule::FillEvenOdd;
        break;
    default:
        mCNode->mFillRule = LOTFillRule::FillWinding;
        break;
    }

    switch (mBrush.type()) {
    case VBrush::Type::Solid:
        mCNode->mBrushType = LOTBrushType::BrushSolid;
        mCNode->mColor.r = mBrush.mColor.r;
        mCNode->mColor.g = mBrush.mColor.g;
        mCNode->mColor.b = mBrush.mColor.b;
        mCNode->mColor.a = mBrush.mColor.a;
        break;
    case VBrush::Type::LinearGradient: {
        mCNode->mBrushType = LOTBrushType::BrushGradient;
        mCNode->mGradient.type = LOTGradientType::GradientLinear;
        VPointF s = mBrush.mGradient->mMatrix.map(
            {mBrush.mGradient->linear.x1, mBrush.mGradient->linear.y1});
        VPointF e = mBrush.mGradient->mMatrix.map(
            {mBrush.mGradient->linear.x2, mBrush.mGradient->linear.y2});
        mCNode->mGradient.start.x = s.x();
        mCNode->mGradient.start.y = s.y();
        mCNode->mGradient.end.x = e.x();
        mCNode->mGradient.end.y = e.y();
        updateGStops(mCNode.get(), mBrush.mGradient);
        break;
    }
    case VBrush::Type::RadialGradient: {
        mCNode->mBrushType = LOTBrushType::BrushGradient;
        mCNode->mGradient.type = LOTGradientType::GradientRadial;
        VPointF c = mBrush.mGradient->mMatrix.map(
            {mBrush.mGradient->radial.cx, mBrush.mGradient->radial.cy});
        VPointF f = mBrush.mGradient->mMatrix.map(
            {mBrush.mGradient->radial.fx, mBrush.mGradient->radial.fy});
        mCNode->mGradient.center.x = c.x();
        mCNode->mGradient.center.y = c.y();
        mCNode->mGradient.focal.x = f.x();
        mCNode->mGradient.focal.y = f.y();

        float scale = mBrush.mGradient->mMatrix.scale();
        mCNode->mGradient.cradius = mBrush.mGradient->radial.cradius * scale;
        mCNode->mGradient.fradius = mBrush.mGradient->radial.fradius * scale;
        updateGStops(mCNode.get(), mBrush.mGradient);
        break;
    }
    default:
        break;
    }
}

void configureModelCacheSize(size_t cacheSize)
{
    LottieLoader::configureModelCacheSize(cacheSize);
}

struct RenderTask {
    RenderTask() { receiver = sender.get_future(); }
    std::promise<Surface> sender;
    std::future<Surface>  receiver;
    AnimationImpl *       playerImpl{nullptr};
    size_t                frameNo{0};
    Surface               surface;
    bool                  keepAspectRatio{true};
};
using SharedRenderTask = std::shared_ptr<RenderTask>;

class AnimationImpl {
public:
    void    init(const std::shared_ptr<LOTModel> &model);
    bool    update(size_t frameNo, const VSize &size, bool keepAspectRatio);
    VSize   size() const { return mModel->size(); }
    double  duration() const { return mModel->duration(); }
    double  frameRate() const { return mModel->frameRate(); }
    size_t  totalFrame() const { return mModel->totalFrame(); }
    size_t  frameAtPos(double pos) const { return mModel->frameAtPos(pos); }
    Surface render(size_t frameNo, const Surface &surface, bool keepAspectRatio);

    const LOTLayerNode * renderTree(size_t frameNo, const VSize &size);

    const LayerInfoList &layerInfoList() const
    {
        if (mLayerList.empty()) {
            mLayerList = mModel->layerInfoList();
        }
        return mLayerList;
    }
    const MarkerList &markers() const
    {
        return mModel->markers();
    }
    void setValue(const std::string &keypath, LOTVariant &&value);
    void removeFilter(const std::string &keypath, Property prop);

private:
    mutable LayerInfoList        mLayerList;
    std::string                  mFilePath;
    std::shared_ptr<LOTModel>    mModel;
    std::unique_ptr<LOTCompItem> mCompItem;
    SharedRenderTask             mTask;
    std::atomic<bool>            mRenderInProgress;
};

void AnimationImpl::setValue(const std::string &keypath, LOTVariant &&value)
{
    if (keypath.empty()) return;
    mCompItem->setValue(keypath, value);
}

const LOTLayerNode *AnimationImpl::renderTree(size_t frameNo, const VSize &size)
{
    if (update(frameNo, size, true)) {
        mCompItem->buildRenderTree();
    }
    return mCompItem->renderTree();
}

bool AnimationImpl::update(size_t frameNo, const VSize &size, bool keepAspectRatio)
{
    frameNo += mModel->startFrame();

    if (frameNo > mModel->endFrame()) frameNo = mModel->endFrame();

    if (frameNo < mModel->startFrame()) frameNo = mModel->startFrame();

    return mCompItem->update(int(frameNo), size, keepAspectRatio);
}

Surface AnimationImpl::render(size_t frameNo, const Surface &surface, bool keepAspectRatio)
{
    bool renderInProgress = mRenderInProgress.load();
    if (renderInProgress) {
        vCritical << "Already Rendering Scheduled for this Animation";
        return surface;
    }

    mRenderInProgress.store(true);
    update(frameNo,
           VSize(int(surface.drawRegionWidth()), int(surface.drawRegionHeight())), keepAspectRatio);
    mCompItem->render(surface);
    mRenderInProgress.store(false);

    return surface;
}

void AnimationImpl::init(const std::shared_ptr<LOTModel> &model)
{
    mModel = model;
    mCompItem = std::make_unique<LOTCompItem>(mModel.get());
    mRenderInProgress = false;
}

class RenderTaskScheduler {
public:
    static RenderTaskScheduler &instance()
    {
        static RenderTaskScheduler singleton;
        return singleton;
    }

    std::future<Surface> process(SharedRenderTask task)
    {
        auto result = task->playerImpl->render(task->frameNo, task->surface, task->keepAspectRatio);
        task->sender.set_value(result);
        return std::move(task->receiver);
    }
};

/**
 * \breif Brief abput the Api.
 * Description about the setFilePath Api
 * @param path  add the details
 */
std::shared_ptr<Animation> Animation::loadFromData(
    std::string jsonData, const std::string &key,
    const std::string &resourcePath, bool cachePolicy)
{
    if (jsonData.empty()) {
        vWarning << "jason data is empty";
        return nullptr;
    }

    LottieLoader loader;
    if (loader.loadFromData(std::move(jsonData), key,
                            (resourcePath.empty() ? " " : resourcePath), cachePolicy)) {
        auto animation = std::unique_ptr<Animation>(new Animation);
        animation->d->init(loader.model());
        return animation;
    }
    return nullptr;
}

std::shared_ptr<Animation> Animation::loadFromFile(const std::string &path, bool cachePolicy)
{
    if (path.empty()) {
        vWarning << "File path is empty";
        return nullptr;
    }

    LottieLoader loader;
    if (loader.load(path, cachePolicy)) {
        auto animation = std::make_shared<Animation>();
        animation->d->init(loader.model());
        return animation;
    }
    return nullptr;
}

void Animation::size(size_t &width, size_t &height) const
{
    VSize sz = d->size();

    width = sz.width();
    height = sz.height();
}

double Animation::duration() const
{
    return d->duration();
}

double Animation::frameRate() const
{
    return d->frameRate();
}

size_t Animation::totalFrame() const
{
    return d->totalFrame();
}

size_t Animation::frameAtPos(double pos)
{
    return d->frameAtPos(pos);
}

const LOTLayerNode *Animation::renderTree(size_t frameNo, size_t width,
                                          size_t height) const
{
    return d->renderTree(frameNo, VSize(int(width), int(height)));
}

void Animation::renderSync(size_t frameNo, Surface surface, bool keepAspectRatio)
{
    d->render(frameNo, surface, keepAspectRatio);
}

const LayerInfoList &Animation::layers() const
{
    return d->layerInfoList();
}

const MarkerList &Animation::markers() const
{
    return d->markers();
}

void Animation::setValue(Color_Type, Property prop, const std::string &keypath,
                         Color value)
{
    d->setValue(keypath,
                LOTVariant(prop, [value](const FrameInfo &) { return value; }));
}

void Animation::setValue(Float_Type, Property prop, const std::string &keypath,
                         float value)
{
    d->setValue(keypath,
                LOTVariant(prop, [value](const FrameInfo &) { return value; }));
}

void Animation::setValue(Size_Type, Property prop, const std::string &keypath,
                         Size value)
{
    d->setValue(keypath,
                LOTVariant(prop, [value](const FrameInfo &) { return value; }));
}

void Animation::setValue(Point_Type, Property prop, const std::string &keypath,
                         Point value)
{
    d->setValue(keypath,
                LOTVariant(prop, [value](const FrameInfo &) { return value; }));
}

void Animation::setValue(Color_Type, Property prop, const std::string &keypath,
                         std::function<Color(const FrameInfo &)> &&value)
{
    d->setValue(keypath, LOTVariant(prop, value));
}

void Animation::setValue(Float_Type, Property prop, const std::string &keypath,
                         std::function<float(const FrameInfo &)> &&value)
{
    d->setValue(keypath, LOTVariant(prop, value));
}

void Animation::setValue(Size_Type, Property prop, const std::string &keypath,
                         std::function<Size(const FrameInfo &)> &&value)
{
    d->setValue(keypath, LOTVariant(prop, value));
}

void Animation::setValue(Point_Type, Property prop, const std::string &keypath,
                         std::function<Point(const FrameInfo &)> &&value)
{
    d->setValue(keypath, LOTVariant(prop, value));
}

Animation::~Animation() = default;
Animation::Animation() : d(std::make_unique<AnimationImpl>()) {}

Surface::Surface(uint32_t *buffer, size_t width, size_t height,
                 size_t bytesPerLine)
    : mBuffer(buffer),
      mWidth(width),
      mHeight(height),
      mBytesPerLine(bytesPerLine)
{
    mDrawArea.w = mWidth;
    mDrawArea.h = mHeight;
}

void Surface::setDrawRegion(size_t x, size_t y, size_t width, size_t height)
{
    if ((x + width > mWidth) || (y + height > mHeight)) return;

    mDrawArea.x = x;
    mDrawArea.y = y;
    mDrawArea.w = width;
    mDrawArea.h = height;
}

} // namespace imlottie

