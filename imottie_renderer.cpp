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

#include "freetype/v_ft_raster.h"
#include "freetype/v_ft_stroker.h"

#include "rapidjson/document.h"
#include "rapidjson/stream.h"

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_GIF
#define STBI_NO_PIC

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "imlottie_impl.h"

#include <fstream>
#include <mutex>
#include <condition_variable>

namespace imlottie {
    std::shared_ptr<Animation> animationLoad(const char *path) {
        return Animation::loadFromFile(path, false
                                                /*not use cache*/
        );
    }
    uint16_t animationTotalFrame(const std::shared_ptr<Animation> &anim) {
        return anim->totalFrame();
    }
    double animationDuration(const std::shared_ptr<Animation> &anim) {
        return anim->duration();
    }
    void animationRenderSync (const std::shared_ptr<Animation> &anim, int nextFrameIndex, uint32_t *data, int width, int height, int row_pitch) {
        Surface surface(data, width, height, row_pitch);
        // rasterize frame to nextFrame.data, imlottie::Surface is temporary
        // structure which not save any data
        anim->renderSync(nextFrameIndex, surface);
    }
} // ImGui

namespace imlottie {

enum class Operation {Add, Xor};
struct VRleHelper {
    size_t      alloc {0};
    size_t      size {0};
    VRle::Span *spans {nullptr};
};
static void rleIntersectWithRle(VRleHelper *, int, int, VRleHelper *, VRleHelper *);
static void rleIntersectWithRect(const VRect &, VRleHelper *, VRleHelper *);
static void rleOpGeneric(VRleHelper *, VRleHelper *, VRleHelper *, Operation op);
static void rleSubstractWithRle(VRleHelper *, VRleHelper *, VRleHelper *);
static inline uchar divBy255(int x) { return (x + (x >> 8) + 0x80) >> 8; }
inline static void copyArrayToVector(const VRle::Span *span, size_t count, std::vector<VRle::Span> &v) {
    // make sure enough memory available
    if (v.capacity() < v.size() + count) v.reserve(v.size() + count);
    std::copy(span, span + count, std::back_inserter(v));
}
void VRle::VRleData::addSpan(const VRle::Span *span, size_t count) {
    copyArrayToVector                   (span, count, mSpans);
    mBboxDirty                          = true;
}
VRect VRle::VRleData::bbox() const {
    updateBbox();
    return mBbox;
}
void VRle::VRleData::setBbox(const VRect &bbox) const {
    mBboxDirty = false;
    mBbox = bbox;
}
void VRle::VRleData::reset() {
    mSpans.clear();
    mBbox = VRect();
    mOffset = VPoint();
    mBboxDirty = false;
}
void VRle::VRleData::clone(const VRle::VRleData &o) {
    *this = o;
}
void VRle::VRleData::translate(const VPoint &p) {
    // take care of last offset if applied
    mOffset = p - mOffset;
    int x = mOffset.x();
    int y = mOffset.y();
    for (auto &i : mSpans) {
        i.x = i.x + x;
        i.y = i.y + y;
    }
    updateBbox();
    mBbox.translate(mOffset.x(), mOffset.y());
}
void VRle::VRleData::addRect(const VRect &rect) {
    int x = rect.left();
    int y = rect.top();
    int width = rect.width();
    int height = rect.height();
    mSpans.reserve(size_t(height));
    VRle::Span span;
    for (int i = 0; i < height; i++) {
        span.x = x;
        span.y = y + i;
        span.len = width;
        span.coverage = 255;
        mSpans.push_back(span);
    }
    updateBbox();
}
void VRle::VRleData::updateBbox() const {
    if (!mBboxDirty) return;
    mBboxDirty = false;
    int               l = std::numeric_limits<int>::max();
    const VRle::Span *span = mSpans.data();
    mBbox = VRect();
    size_t sz = mSpans.size();
    if (sz) {
        int t = span[0].y;
        int b = span[sz - 1].y;
        int r = 0;
        for (size_t i = 0; i < sz; i++) {
            if (span[i].x < l) l = span[i].x;
            if (span[i].x + span[i].len > r) r = span[i].x + span[i].len;
        }
        mBbox = VRect(l, t, r - l, b - t + 1);
    }
}
void VRle::VRleData::invert() {
    for (auto &i : mSpans) {
        i.coverage = 255 - i.coverage;
    }
}
void VRle::VRleData::operator*=(uchar alpha) {
    for (auto &i : mSpans) {
        i.coverage = divBy255(i.coverage * alpha);
    }
}
void VRle::VRleData::opIntersect(const VRect &r, VRle::VRleSpanCb cb,
                                 void *userData) const {
    if (empty()) return;
    if (r.contains(bbox())) {
        cb(mSpans.size(), mSpans.data(), userData);
        return;
    }
    VRect                       clip = r;
    VRleHelper                  tresult, tmp_obj;
    std::array<VRle::Span, 256> array;
    // setup the tresult object
    tresult.size = array.size();
    tresult.alloc = array.size();
    tresult.spans = array.data();
    // setup tmp object
    tmp_obj.size = mSpans.size();
    tmp_obj.spans = const_cast<VRle::Span *>(mSpans.data());
    // run till all the spans are processed
    while (tmp_obj.size) {
        rleIntersectWithRect(clip, &tmp_obj, &tresult);
        if (tresult.size) {
            cb(tresult.size, tresult.spans, userData);
        }
        tresult.size = 0;
    }
}
// res = a - b;
void VRle::VRleData::opSubstract(const VRle::VRleData &a,
                                 const VRle::VRleData &b) {
    // if two rle are disjoint
    if (!a.bbox().intersects(b.bbox())) {
        mSpans = a.mSpans;
    } else {
        VRle::Span *      aPtr = const_cast<VRle::Span *>(a.mSpans.data());
        const VRle::Span *aEnd = a.mSpans.data() + a.mSpans.size();
        VRle::Span *      bPtr = const_cast<VRle::Span *>(b.mSpans.data());
        const VRle::Span *bEnd = b.mSpans.data() + b.mSpans.size();
        // 1. forward till both y intersect
        while ((aPtr != aEnd) && (aPtr->y < bPtr->y)) aPtr++;
        size_t sizeA = size_t(aPtr - a.mSpans.data());
        if (sizeA) copyArrayToVector(a.mSpans.data(), sizeA, mSpans);
        // 2. forward b till it intersect with a.
        while ((bPtr != bEnd) && (bPtr->y < aPtr->y)) bPtr++;
        size_t sizeB = size_t(bPtr - b.mSpans.data());
        // 2. calculate the intersect region
        VRleHelper                  tresult, aObj, bObj;
        std::array<VRle::Span, 256> array;
        // setup the tresult object
        tresult.size = array.size();
        tresult.alloc = array.size();
        tresult.spans = array.data();
        // setup a object
        aObj.size = a.mSpans.size() - sizeA;
        aObj.spans = aPtr;
        // setup b object
        bObj.size = b.mSpans.size() - sizeB;
        bObj.spans = bPtr;
        // run till all the spans are processed
        while (aObj.size && bObj.size) {
            rleSubstractWithRle(&aObj, &bObj, &tresult);
            if (tresult.size) {
                copyArrayToVector(tresult.spans, tresult.size, mSpans);
            }
            tresult.size = 0;
        }
        // 3. copy the rest of a
        if (aObj.size) copyArrayToVector(aObj.spans, aObj.size, mSpans);
    }
    mBboxDirty = true;
}
void VRle::VRleData::opGeneric(const VRle::VRleData &a, const VRle::VRleData &b,
                               OpCode code) {
    // This routine assumes, obj1(span_y) < obj2(span_y).
    // reserve some space for the result vector.
    mSpans.reserve(a.mSpans.size() + b.mSpans.size());
    // if two rle are disjoint
    if (!a.bbox().intersects(b.bbox())) {
        if (a.mSpans[0].y < b.mSpans[0].y) {
            copyArrayToVector(a.mSpans.data(), a.mSpans.size(), mSpans);
            copyArrayToVector(b.mSpans.data(), b.mSpans.size(), mSpans);
        } else {
            copyArrayToVector(b.mSpans.data(), b.mSpans.size(), mSpans);
            copyArrayToVector(a.mSpans.data(), a.mSpans.size(), mSpans);
        }
    } else {
        VRle::Span *      aPtr = const_cast<VRle::Span *>(a.mSpans.data());
        const VRle::Span *aEnd = a.mSpans.data() + a.mSpans.size();
        VRle::Span *      bPtr = const_cast<VRle::Span *>(b.mSpans.data());
        const VRle::Span *bEnd = b.mSpans.data() + b.mSpans.size();
        // 1. forward a till it intersects with b
        while ((aPtr != aEnd) && (aPtr->y < bPtr->y)) aPtr++;
        size_t sizeA = size_t(aPtr - a.mSpans.data());
        if (sizeA) copyArrayToVector(a.mSpans.data(), sizeA, mSpans);
        // 2. forward b till it intersects with a
        while ((bPtr != bEnd) && (bPtr->y < aPtr->y)) bPtr++;
        size_t sizeB = size_t(bPtr - b.mSpans.data());
        if (sizeB) copyArrayToVector(b.mSpans.data(), sizeB, mSpans);
        // 3. calculate the intersect region
        VRleHelper                  tresult, aObj, bObj;
        std::array<VRle::Span, 256> array;
        // setup the tresult object
        tresult.size = array.size();
        tresult.alloc = array.size();
        tresult.spans = array.data();
        // setup a object
        aObj.size = a.mSpans.size() - sizeA;
        aObj.spans = aPtr;
        // setup b object
        bObj.size = b.mSpans.size() - sizeB;
        bObj.spans = bPtr;
        Operation op = Operation::Add;
        switch (code) {
        case OpCode::Add:
        op = Operation::Add;
        break;
        case OpCode::Xor:
        op = Operation::Xor;
        break;
        }
        // run till all the spans are processed
        while (aObj.size && bObj.size) {
            rleOpGeneric(&aObj, &bObj, &tresult, op);
            if (tresult.size) {
                copyArrayToVector(tresult.spans, tresult.size, mSpans);
            }
            tresult.size = 0;
        }
        // 3. copy the rest
        if (bObj.size) copyArrayToVector(bObj.spans, bObj.size, mSpans);
        if (aObj.size) copyArrayToVector(aObj.spans, aObj.size, mSpans);
    }
    mBboxDirty = true;
}
static void rle_cb(size_t count, const VRle::Span *spans, void *userData) {
    auto vector = static_cast<std::vector<VRle::Span> *>(userData);
    copyArrayToVector(spans, count, *vector);
}
void opIntersectHelper(const VRle::VRleData &obj1, const VRle::VRleData &obj2,
                       VRle::VRleSpanCb cb, void *userData) {
    VRleHelper                  result, source, clip;
    std::array<VRle::Span, 256> array;
    // setup the tresult object
    result.size = array.size();
    result.alloc = array.size();
    result.spans = array.data();
    // setup tmp object
    source.size = obj1.mSpans.size();
    source.spans = const_cast<VRle::Span *>(obj1.mSpans.data());
    // setup tmp clip object
    clip.size = obj2.mSpans.size();
    clip.spans = const_cast<VRle::Span *>(obj2.mSpans.data());
    // run till all the spans are processed
    while (source.size) {
        rleIntersectWithRle(&clip, 0, 0, &source, &result);
        if (result.size) {
            cb(result.size, result.spans, userData);
        }
        result.size = 0;
    }
}
void VRle::VRleData::opIntersect(const VRle::VRleData &obj1,
                                 const VRle::VRleData &obj2) {
    opIntersectHelper(obj1, obj2, rle_cb, &mSpans);
    updateBbox();
}
#define VMIN(a, b) ((a) < (b) ? (a) : (b))
#define VMAX(a, b) ((a) > (b) ? (a) : (b))
/*
* This function will clip a rle list with another rle object
* tmp_clip  : The rle list that will be use to clip the rle
* tmp_obj   : holds the list of spans that has to be clipped
* result    : will hold the result after the processing
* NOTE: if the algorithm runs out of the result buffer list
*       it will stop and update the tmp_obj with the span list
*       that are yet to be processed as well as the tpm_clip object
*       with the unprocessed clip spans.
*/
static void rleIntersectWithRle(VRleHelper *tmp_clip, int clip_offset_x,
                                int clip_offset_y, VRleHelper *tmp_obj,
                                VRleHelper *result) {
    VRle::Span *out = result->spans;
    size_t      available = result->alloc;
    VRle::Span *spans = tmp_obj->spans;
    VRle::Span *end = tmp_obj->spans + tmp_obj->size;
    VRle::Span *clipSpans = tmp_clip->spans;
    VRle::Span *clipEnd = tmp_clip->spans + tmp_clip->size;
    int         sx1, sx2, cx1, cx2, x, len;
    while (available && spans < end) {
        if (clipSpans >= clipEnd) {
            spans = end;
            break;
        }
        if ((clipSpans->y + clip_offset_y) > spans->y) {
            ++spans;
            continue;
        }
        if (spans->y != (clipSpans->y + clip_offset_y)) {
            ++clipSpans;
            continue;
        }
        // assert(spans->y == (clipSpans->y + clip_offset_y));
        sx1 = spans->x;
        sx2 = sx1 + spans->len;
        cx1 = (clipSpans->x + clip_offset_x);
        cx2 = cx1 + clipSpans->len;
        if (cx1 < sx1 && cx2 < sx1) {
            ++clipSpans;
            continue;
        } else if (sx1 < cx1 && sx2 < cx1) {
            ++spans;
            continue;
        }
        x = std::max(sx1, cx1);
        len = std::min(sx2, cx2) - x;
        if (len) {
            out->x = std::max(sx1, cx1);
            out->len = (std::min(sx2, cx2) - out->x);
            out->y = spans->y;
            out->coverage = divBy255(spans->coverage * clipSpans->coverage);
            ++out;
            --available;
        }
        if (sx2 < cx2) {
            ++spans;
        } else {
            ++clipSpans;
        }
    }
    // update the span list that yet to be processed
    tmp_obj->spans = spans;
    tmp_obj->size = end - spans;
    // update the clip list that yet to be processed
    tmp_clip->spans = clipSpans;
    tmp_clip->size = clipEnd - clipSpans;
    // update the result
    result->size = result->alloc - available;
}
/*
* This function will clip a rle list with a given rect
* clip      : The clip rect that will be use to clip the rle
* tmp_obj   : holds the list of spans that has to be clipped
* result    : will hold the result after the processing
* NOTE: if the algorithm runs out of the result buffer list
*       it will stop and update the tmp_obj with the span list
*       that are yet to be processed
*/
static void rleIntersectWithRect(const VRect &clip, VRleHelper *tmp_obj,
                                 VRleHelper *result) {
    VRle::Span *out = result->spans;
    size_t      available = result->alloc;
    VRle::Span *spans = tmp_obj->spans;
    VRle::Span *end = tmp_obj->spans + tmp_obj->size;
    short       minx, miny, maxx, maxy;
    minx = clip.left();
    miny = clip.top();
    maxx = clip.right() - 1;
    maxy = clip.bottom() - 1;
    while (available && spans < end) {
        if (spans->y > maxy) {
            spans = end;
            // update spans so that we can breakout
            break;
        }
        if (spans->y < miny || spans->x > maxx ||
            spans->x + spans->len <= minx) {
            ++spans;
            continue;
        }
        if (spans->x < minx) {
            out->len = VMIN(spans->len - (minx - spans->x), maxx - minx + 1);
            out->x = minx;
        } else {
            out->x = spans->x;
            out->len = VMIN(spans->len, (maxx - spans->x + 1));
        }
        if (out->len != 0) {
            out->y = spans->y;
            out->coverage = spans->coverage;
            ++out;
            --available;
        }
        ++spans;
    }
    // update the span list that yet to be processed
    tmp_obj->spans = spans;
    tmp_obj->size = end - spans;
    // update the result
    result->size = result->alloc - available;
}
void blitXor(VRle::Span *spans, int count, uchar *buffer, int offsetX) {
    while (count--) {
        int    x = spans->x + offsetX;
        int    l = spans->len;
        uchar *ptr = buffer + x;
        while (l--) {
            int da = *ptr;
            *ptr = divBy255((255 - spans->coverage) * (da) +
                            spans->coverage * (255 - da));
            ptr++;
        }
        spans++;
    }
}
void blitDestinationOut(VRle::Span *spans, int count, uchar *buffer,
                        int offsetX) {
    while (count--) {
        int    x = spans->x + offsetX;
        int    l = spans->len;
        uchar *ptr = buffer + x;
        while (l--) {
            *ptr = divBy255((255 - spans->coverage) * (*ptr));
            ptr++;
        }
        spans++;
    }
}
void blitSrcOver(VRle::Span *spans, int count, uchar *buffer, int offsetX) {
    while (count--) {
        int    x = spans->x + offsetX;
        int    l = spans->len;
        uchar *ptr = buffer + x;
        while (l--) {
            *ptr = spans->coverage + divBy255((255 - spans->coverage) * (*ptr));
            ptr++;
        }
        spans++;
    }
}
void blit(VRle::Span *spans, int count, uchar *buffer, int offsetX) {
    while (count--) {
        int    x = spans->x + offsetX;
        int    l = spans->len;
        uchar *ptr = buffer + x;
        while (l--) {
            *ptr = std::max(spans->coverage, *ptr);
            ptr++;
        }
        spans++;
    }
}
size_t bufferToRle(uchar *buffer, int size, int offsetX, int y, VRle::Span *out) {
    size_t count = 0;
    uchar  value = buffer[0];
    int    curIndex = 0;
    size = offsetX < 0 ? size + offsetX : size;
    for (int i = 0; i < size; i++) {
        uchar curValue = buffer[0];
        if (value != curValue) {
            if (value) {
                out->y = y;
                out->x = offsetX + curIndex;
                out->len = i - curIndex;
                out->coverage = value;
                out++;
                count++;
            }
            curIndex = i;
            value = curValue;
        }
        buffer++;
    }
    if (value) {
        out->y = y;
        out->x = offsetX + curIndex;
        out->len = size - curIndex;
        out->coverage = value;
        count++;
    }
    return count;
}
static void rleOpGeneric(VRleHelper *a, VRleHelper *b, VRleHelper *result,
                         Operation op) {
    std::array<VRle::Span, 256> temp;
    VRle::Span *                out = result->spans;
    size_t                      available = result->alloc;
    VRle::Span *                aPtr = a->spans;
    VRle::Span *                aEnd = a->spans + a->size;
    VRle::Span *                bPtr = b->spans;
    VRle::Span *                bEnd = b->spans + b->size;
    while (available && aPtr < aEnd && bPtr < bEnd) {
        if (aPtr->y < bPtr->y) {
            *out++ = *aPtr++;
            available--;
        } else if (bPtr->y < aPtr->y) {
            *out++ = *bPtr++;
            available--;
        } else {
            // same y
            VRle::Span *aStart = aPtr;
            VRle::Span *bStart = bPtr;
            int y = aPtr->y;
            while (aPtr < aEnd && aPtr->y == y) aPtr++;
            while (bPtr < bEnd && bPtr->y == y) bPtr++;
            int aLength = (aPtr - 1)->x + (aPtr - 1)->len;
            int bLength = (bPtr - 1)->x + (bPtr - 1)->len;
            int offset = std::min(aStart->x, bStart->x);
            std::array<uchar, 1024> array = { {
                    0
                }
            }
            ;
            blit(aStart, (aPtr - aStart), array.data(), -offset);
            if (op == Operation::Add)
                blitSrcOver(bStart, (bPtr - bStart), array.data(), -offset); else if (op == Operation::Xor)
                blitXor(bStart, (bPtr - bStart), array.data(), -offset);
            VRle::Span *tResult = temp.data();
            size_t size = bufferToRle(array.data(), std::max(aLength, bLength),
                                      offset, y, tResult);
            if (available >= size) {
                while (size--) {
                    *out++ = *tResult++;
                    available--;
                }
            } else {
                aPtr = aStart;
                bPtr = bStart;
                break;
            }
        }
    }
    // update the span list that yet to be processed
    a->spans = aPtr;
    a->size = aEnd - aPtr;
    // update the clip list that yet to be processed
    b->spans = bPtr;
    b->size = bEnd - bPtr;
    // update the result
    result->size = result->alloc - available;
}
static void rleSubstractWithRle(VRleHelper *a, VRleHelper *b,
                                VRleHelper *result) {
    std::array<VRle::Span, 256> temp;
    VRle::Span *                out = result->spans;
    size_t                      available = result->alloc;
    VRle::Span *                aPtr = a->spans;
    VRle::Span *                aEnd = a->spans + a->size;
    VRle::Span *                bPtr = b->spans;
    VRle::Span *                bEnd = b->spans + b->size;
    while (available && aPtr < aEnd && bPtr < bEnd) {
        if (aPtr->y < bPtr->y) {
            *out++ = *aPtr++;
            available--;
        } else if (bPtr->y < aPtr->y) {
            bPtr++;
        } else {
            // same y
            VRle::Span *aStart = aPtr;
            VRle::Span *bStart = bPtr;
            int y = aPtr->y;
            while (aPtr < aEnd && aPtr->y == y) aPtr++;
            while (bPtr < bEnd && bPtr->y == y) bPtr++;
            int aLength = (aPtr - 1)->x + (aPtr - 1)->len;
            int bLength = (bPtr - 1)->x + (bPtr - 1)->len;
            int offset = std::min(aStart->x, bStart->x);
            std::array<uchar, 1024> array = { {
                    0
                }
            }
            ;
            blit(aStart, (aPtr - aStart), array.data(), -offset);
            blitDestinationOut(bStart, (bPtr - bStart), array.data(), -offset);
            VRle::Span *tResult = temp.data();
            size_t size = bufferToRle(array.data(), std::max(aLength, bLength),
                                      offset, y, tResult);
            if (available >= size) {
                while (size--) {
                    *out++ = *tResult++;
                    available--;
                }
            } else {
                aPtr = aStart;
                bPtr = bStart;
                break;
            }
        }
    }
    // update the span list that yet to be processed
    a->spans = aPtr;
    a->size = size_t(aEnd - aPtr);
    // update the clip list that yet to be processed
    b->spans = bPtr;
    b->size = size_t(bEnd - bPtr);
    // update the result
    result->size = result->alloc - available;
}
VRle VRle::toRle(const VRect &rect) {
    if (rect.empty()) return VRle();
    VRle result;
    result.d.write().addRect(rect);
    return result;
}
/*
* this api makes use of thread_local temporary
* buffer to avoid creating intermediate temporary rle buffer
* the scratch buffer object will grow its size on demand
* so that future call won't need any more memory allocation.
* this function is thread safe as it uses thread_local variable
* which is unique per thread.
*/
static thread_local VRle::VRleData Scratch_Object;
void VRle::operator&=(const VRle &o) {
    if (empty()) return;
    if (o.empty()) {
        reset();
        return;
    }
    Scratch_Object.reset();
    Scratch_Object.opIntersect(d.read(), o.d.read());
    d.write() = Scratch_Object;
}
template <typename T>
class dyn_array {
public:
    explicit dyn_array(size_t size)
        : mCapacity(size), mData(std::make_unique<T[]>(mCapacity)) {
    }
    void reserve(size_t size) {
        if (mCapacity > size) return;
        mCapacity = size;
        mData = std::make_unique<T[]>(mCapacity);
    }
    T *        data() const {
        return mData.get();
    }
    dyn_array &operator=(dyn_array &&) noexcept = delete;
private:
    size_t               mCapacity {
        0
    }
    ;
    std::unique_ptr<T[]> mData {
        nullptr
    }
    ;
}
;
struct FTOutline {
public:
    void reset();
    void grow(size_t, size_t);
    void convert(const VPath &path);
    void convert(CapStyle, JoinStyle, float, float);
    void moveTo(const VPointF &pt);
    void lineTo(const VPointF &pt);
    void cubicTo(const VPointF &ctr1, const VPointF &ctr2, const VPointF end);
    void close();
    void end();
    void transform(const VMatrix &m);
    SW_FT_Pos TO_FT_COORD(float x) {
        return SW_FT_Pos(x * 64);
    }
    // to freetype 26.6 coordinate.
    SW_FT_Outline           ft;
    bool                    closed {
        false
    }
    ;
    SW_FT_Stroker_LineCap   ftCap;
    SW_FT_Stroker_LineJoin  ftJoin;
    SW_FT_Fixed             ftWidth;
    SW_FT_Fixed             ftMiterLimit;
    dyn_array<SW_FT_Vector> mPointMemory {
        100
    }
    ;
    dyn_array<char>         mTagMemory {
        100
    }
    ;
    dyn_array<short>        mContourMemory {
        10
    }
    ;
    dyn_array<char>         mContourFlagMemory {
        10
    }
    ;
}
;
void FTOutline::reset() {
    ft.n_points = ft.n_contours = 0;
    ft.flags = 0x0;
}
void FTOutline::grow(size_t points, size_t segments) {
    reset();
    mPointMemory.reserve(points + segments);
    mTagMemory.reserve(points + segments);
    mContourMemory.reserve(segments);
    mContourFlagMemory.reserve(segments);
    ft.points = mPointMemory.data();
    ft.tags = mTagMemory.data();
    ft.contours = mContourMemory.data();
    ft.contours_flag = mContourFlagMemory.data();
}
void FTOutline::convert(const VPath &path) {
    const std::vector<VPath::Element> &elements = path.elements();
    const std::vector<VPointF> &       points = path.points();
    grow(points.size(), path.segments());
    size_t index = 0;
    for (auto element : elements) {
        switch (element) {
        case VPath::Element::MoveTo:
        moveTo(points[index]);
        index++;
        break;
        case VPath::Element::LineTo:
        lineTo(points[index]);
        index++;
        break;
        case VPath::Element::CubicTo:
        cubicTo(points[index], points[index + 1], points[index + 2]);
        index = index + 3;
        break;
        case VPath::Element::Close:
        close();
        break;
        }
    }
    end();
}
void FTOutline::convert(CapStyle cap, JoinStyle join, float width,
                        float miterLimit) {
    // map strokeWidth to freetype. It uses as the radius of the pen not the
    // diameter
    width = width / 2.0f;
    // convert to freetype co-ordinate
    // IMP: stroker takes radius in 26.6 co-ordinate
    ftWidth = SW_FT_Fixed(width * (1 << 6));
    // IMP: stroker takes meterlimit in 16.16 co-ordinate
    ftMiterLimit = SW_FT_Fixed(miterLimit * (1 << 16));
    // map to freetype capstyle
    switch (cap) {
    case CapStyle::Square:
    ftCap = SW_FT_STROKER_LINECAP_SQUARE;
    break;
    case CapStyle::Round:
    ftCap = SW_FT_STROKER_LINECAP_ROUND;
    break;
    default:
    ftCap = SW_FT_STROKER_LINECAP_BUTT;
    break;
    }
    switch (join) {
    case JoinStyle::Bevel:
    ftJoin = SW_FT_STROKER_LINEJOIN_BEVEL;
    break;
    case JoinStyle::Round:
    ftJoin = SW_FT_STROKER_LINEJOIN_ROUND;
    break;
    default:
    ftJoin = SW_FT_STROKER_LINEJOIN_MITER_FIXED;
    break;
    }
}
void FTOutline::moveTo(const VPointF &pt) {
    assert(ft.n_points <= SHRT_MAX - 1);
    ft.points[ft.n_points].x = TO_FT_COORD(pt.x());
    ft.points[ft.n_points].y = TO_FT_COORD(pt.y());
    ft.tags[ft.n_points] = SW_FT_CURVE_TAG_ON;
    if (ft.n_points) {
        ft.contours[ft.n_contours] = ft.n_points - 1;
        ft.n_contours++;
    }
    // mark the current contour as open
    // will be updated if ther is a close tag at the end.
    ft.contours_flag[ft.n_contours] = 1;
    ft.n_points++;
}
void FTOutline::lineTo(const VPointF &pt) {
    assert(ft.n_points <= SHRT_MAX - 1);
    ft.points[ft.n_points].x = TO_FT_COORD(pt.x());
    ft.points[ft.n_points].y = TO_FT_COORD(pt.y());
    ft.tags[ft.n_points] = SW_FT_CURVE_TAG_ON;
    ft.n_points++;
}
void FTOutline::cubicTo(const VPointF &cp1, const VPointF &cp2,
                        const VPointF ep) {
    assert(ft.n_points <= SHRT_MAX - 3);
    ft.points[ft.n_points].x = TO_FT_COORD(cp1.x());
    ft.points[ft.n_points].y = TO_FT_COORD(cp1.y());
    ft.tags[ft.n_points] = SW_FT_CURVE_TAG_CUBIC;
    ft.n_points++;
    ft.points[ft.n_points].x = TO_FT_COORD(cp2.x());
    ft.points[ft.n_points].y = TO_FT_COORD(cp2.y());
    ft.tags[ft.n_points] = SW_FT_CURVE_TAG_CUBIC;
    ft.n_points++;
    ft.points[ft.n_points].x = TO_FT_COORD(ep.x());
    ft.points[ft.n_points].y = TO_FT_COORD(ep.y());
    ft.tags[ft.n_points] = SW_FT_CURVE_TAG_ON;
    ft.n_points++;
}
void FTOutline::close() {
    assert(ft.n_points <= SHRT_MAX - 1);
    // mark the contour as a close path.
    ft.contours_flag[ft.n_contours] = 0;
    int index;
    if (ft.n_contours) {
        index = ft.contours[ft.n_contours - 1] + 1;
    } else {
        index = 0;
    }
    // make sure atleast 1 point exists in the segment.
    if (ft.n_points == index) {
        closed = false;
        return;
    }
    ft.points[ft.n_points].x = ft.points[index].x;
    ft.points[ft.n_points].y = ft.points[index].y;
    ft.tags[ft.n_points] = SW_FT_CURVE_TAG_ON;
    ft.n_points++;
}
void FTOutline::end() {
    assert(ft.n_contours <= SHRT_MAX - 1);
    if (ft.n_points) {
        ft.contours[ft.n_contours] = ft.n_points - 1;
        ft.n_contours++;
    }
}
static void rleGenerationCb(int count, const SW_FT_Span *spans, void *user) {
    VRle *rle = static_cast<VRle *>(user);
    auto *rleSpan = reinterpret_cast<const VRle::Span *>(spans);
    rle->addSpan(rleSpan, count);
}
static void bboxCb(int x, int y, int w, int h, void *user) {
    VRle *rle = static_cast<VRle *>(user);
    rle->setBoundingRect( {
        x, y, w, h
                         }
    );
}
class SharedRle {
public:
    SharedRle() = default;
    VRle &unsafe() {
        return _rle;
    }
    void  notify() {
        {
            ::std::lock_guard<::std::mutex> lock(_mutex);
            _ready = true;
        }
        _cv.notify_one();
    }
    void wait() {
        if (!_pending) return; {
            ::std::unique_lock<::std::mutex> lock(_mutex);
            while (!_ready) _cv.wait(lock);
        }
        _pending = false;
    }
    VRle &get() {
        wait();
        return _rle;
    }
    void reset() {
        wait();
        _ready = false;
        _pending = true;
    }
private:
    VRle                    _rle;
    ::std::mutex              _mutex;
    ::std::condition_variable _cv;
    bool                    _ready {
        true
    }
    ;
    bool                    _pending {
        false
    }
    ;
}
;
struct VRleTask {
    SharedRle mRle;
    VPath     mPath;
    float     mStrokeWidth;
    float     mMiterLimit;
    VRect     mClip;
    FillRule  mFillRule;
    CapStyle  mCap;
    JoinStyle mJoin;
    bool      mGenerateStroke;
    VRle &rle() {
        return mRle.get();
    }
    void update(VPath path, FillRule fillRule, const VRect &clip) {
        mRle.reset();
        mPath = std::move(path);
        mFillRule = fillRule;
        mClip = clip;
        mGenerateStroke = false;
    }
    void update(VPath path, CapStyle cap, JoinStyle join, float width,
                float miterLimit, const VRect &clip) {
        mRle.reset();
        mPath = std::move(path);
        mCap = cap;
        mJoin = join;
        mStrokeWidth = width;
        mMiterLimit = miterLimit;
        mClip = clip;
        mGenerateStroke = true;
    }
    void render(FTOutline &outRef) {
        SW_FT_Raster_Params params;
        mRle.unsafe().reset();
        params.flags = SW_FT_RASTER_FLAG_DIRECT | SW_FT_RASTER_FLAG_AA;
        params.gray_spans = &rleGenerationCb;
        params.bbox_cb = &bboxCb;
        params.user = &mRle.unsafe();
        params.source = &outRef.ft;
        if (!mClip.empty()) {
            params.flags |= SW_FT_RASTER_FLAG_CLIP;
            params.clip_box.xMin = mClip.left();
            params.clip_box.yMin = mClip.top();
            params.clip_box.xMax = mClip.right();
            params.clip_box.yMax = mClip.bottom();
        }
        // compute rle
        sw_ft_grays_raster.raster_render(nullptr, &params);
    }
    void operator()(FTOutline &outRef, SW_FT_Stroker &stroker) {
        if (mPath.points().size() > SHRT_MAX ||
            mPath.points().size() + mPath.segments() > SHRT_MAX) {
            return;
        }
        if (mGenerateStroke) {
            // Stroke Task
            outRef.convert(mPath);
            outRef.convert(mCap, mJoin, mStrokeWidth, mMiterLimit);
            uint points, contors;
            SW_FT_Stroker_Set(stroker, outRef.ftWidth, outRef.ftCap,
                              outRef.ftJoin, outRef.ftMiterLimit);
            SW_FT_Stroker_ParseOutline(stroker, &outRef.ft);
            SW_FT_Stroker_GetCounts(stroker, &points, &contors);
            outRef.grow(points, contors);
            SW_FT_Stroker_Export(stroker, &outRef.ft);
        } else {
            // Fill Task
            outRef.convert(mPath);
            int fillRuleFlag = SW_FT_OUTLINE_NONE;
            switch (mFillRule) {
            case FillRule::EvenOdd:
            fillRuleFlag = SW_FT_OUTLINE_EVEN_ODD_FILL;
            break;
            default:
            fillRuleFlag = SW_FT_OUTLINE_NONE;
            break;
            }
            outRef.ft.flags = fillRuleFlag;
        }
        render(outRef);
        mPath = VPath();
        mRle.notify();
    }
}
;
using VTask = std::shared_ptr<VRleTask>;
class RleTaskScheduler {
public:
    FTOutline     outlineRef {
    }
    ;
    SW_FT_Stroker stroker;
public:
    static RleTaskScheduler &instance() {
        static RleTaskScheduler singleton;
        return singleton;
    }
    RleTaskScheduler() {
        SW_FT_Stroker_New(&stroker);
    }
    ~RleTaskScheduler() {
        SW_FT_Stroker_Done(stroker);
    }
    void process(VTask task) {
        (*task)(outlineRef, stroker);
    }
}
;
struct VRasterizer::VRasterizerImpl {
    VRleTask mTask;
    VRle &    rle() {
        return mTask.rle();
    }
    VRleTask &task() {
        return mTask;
    }
}
;
VRle VRasterizer::rle() {
    if (!d) return VRle();
    return d->rle();
}
void VRasterizer::init() {
    if (!d) d = std::make_shared<VRasterizerImpl>();
}
void VRasterizer::updateRequest() {
    VTask taskObj = VTask(d, &d->task());
    RleTaskScheduler::instance().process(std::move(taskObj));
}
void VRasterizer::rasterize(VPath path, FillRule fillRule, const VRect &clip) {
    init();
    if (path.empty()) {
        d->rle().reset();
        return;
    }
    d->task().update(std::move(path), fillRule, clip);
    updateRequest();
}
void VRasterizer::rasterize(VPath path, CapStyle cap, JoinStyle join,
                            float width, float miterLimit, const VRect &clip) {
    init();
    if (path.empty() || vIsZero(width)) {
        d->rle().reset();
        return;
    }
    d->task().update(std::move(path), cap, join, width, miterLimit, clip);
    updateRequest();
}
void VPath::VPathData::transform(const VMatrix &m) {
    for (auto &i : m_points) {
        i = m.map(i);
    }
    mLengthDirty = true;
}
float VPath::VPathData::length() const {
    if (!mLengthDirty) return mLength;
    mLengthDirty = false;
    mLength = 0.0;
    size_t i = 0;
    for (auto e : m_elements) {
        switch (e) {
        case VPath::Element::MoveTo:
        i++;
        break;
        case VPath::Element::LineTo: {
            mLength += VLine(m_points[i - 1], m_points[i]).length();
            i++;
            break;
        }
        case VPath::Element::CubicTo: {
            mLength += VBezier::fromPoints(m_points[i - 1], m_points[i],
                                           m_points[i + 1], m_points[i + 2])
                .length();
            i += 3;
            break;
        }
        case VPath::Element::Close:
        break;
        }
    }
    return mLength;
}
void VPath::VPathData::checkNewSegment() {
    if (mNewSegment) {
        moveTo(0, 0);
        mNewSegment = false;
    }
}
void VPath::VPathData::moveTo(float x, float y) {
    mStartPoint = {
        x, y
    }
    ;
    mNewSegment = false;
    m_elements.emplace_back(VPath::Element::MoveTo);
    m_points.emplace_back(x, y);
    m_segments++;
    mLengthDirty = true;
}
void VPath::VPathData::lineTo(float x, float y) {
    checkNewSegment();
    m_elements.emplace_back(VPath::Element::LineTo);
    m_points.emplace_back(x, y);
    mLengthDirty = true;
}
void VPath::VPathData::cubicTo(float cx1, float cy1, float cx2, float cy2,
                               float ex, float ey) {
    checkNewSegment();
    m_elements.emplace_back(VPath::Element::CubicTo);
    m_points.emplace_back(cx1, cy1);
    m_points.emplace_back(cx2, cy2);
    m_points.emplace_back(ex, ey);
    mLengthDirty = true;
}
void VPath::VPathData::close() {
    if (empty()) return;
    const VPointF &lastPt = m_points.back();
    if (!fuzzyCompare(mStartPoint, lastPt)) {
        lineTo(mStartPoint.x(), mStartPoint.y());
    }
    m_elements.push_back(VPath::Element::Close);
    mNewSegment = true;
    mLengthDirty = true;
}
void VPath::VPathData::reset() {
    if (empty()) return;
    m_elements.clear();
    m_points.clear();
    m_segments = 0;
    mLength = 0;
    mLengthDirty = false;
}
size_t VPath::VPathData::segments() const {
    return m_segments;
}
void VPath::VPathData::reserve(size_t pts, size_t elms) {
    if (m_points.capacity() < m_points.size() + pts)
        m_points.reserve(m_points.size() + pts);
    if (m_elements.capacity() < m_elements.size() + elms)
        m_elements.reserve(m_elements.size() + elms);
}
static VPointF curvesForArc(const VRectF &, float, float, VPointF *, size_t *);
static constexpr float PATH_KAPPA = 0.5522847498f;
static constexpr float K_PI = 3.141592f;
void VPath::VPathData::arcTo(const VRectF &rect, float startAngle,
                             float sweepLength, bool forceMoveTo) {
    size_t  point_count = 0;
    VPointF pts[15];
    VPointF curve_start =
        curvesForArc(rect, startAngle, sweepLength, pts, &point_count);
    reserve(point_count + 1, point_count / 3 + 1);
    if (empty() || forceMoveTo) {
        moveTo(curve_start.x(), curve_start.y());
    } else {
        lineTo(curve_start.x(), curve_start.y());
    }
    for (size_t i = 0; i < point_count; i += 3) {
        cubicTo(pts[i].x(), pts[i].y(), pts[i + 1].x(), pts[i + 1].y(),
                pts[i + 2].x(), pts[i + 2].y());
    }
}
void VPath::VPathData::addCircle(float cx, float cy, float radius,
                                 VPath::Direction dir) {
    addOval(VRectF(cx - radius, cy - radius, 2 * radius, 2 * radius), dir);
}
void VPath::VPathData::addOval(const VRectF &rect, VPath::Direction dir) {
    if (rect.empty()) return;
    float x = rect.x();
    float y = rect.y();
    float w = rect.width();
    float w2 = rect.width() / 2;
    float w2k = w2 * PATH_KAPPA;
    float h = rect.height();
    float h2 = rect.height() / 2;
    float h2k = h2 * PATH_KAPPA;
    reserve(13, 6);
    // 1Move + 4Cubic + 1Close
    if (dir == VPath::Direction::CW) {
        // moveto 12 o'clock.
        moveTo(x + w2, y);
        // 12 -> 3 o'clock
        cubicTo(x + w2 + w2k, y, x + w, y + h2 - h2k, x + w, y + h2);
        // 3 -> 6 o'clock
        cubicTo(x + w, y + h2 + h2k, x + w2 + w2k, y + h, x + w2, y + h);
        // 6 -> 9 o'clock
        cubicTo(x + w2 - w2k, y + h, x, y + h2 + h2k, x, y + h2);
        // 9 -> 12 o'clock
        cubicTo(x, y + h2 - h2k, x + w2 - w2k, y, x + w2, y);
    } else {
        // moveto 12 o'clock.
        moveTo(x + w2, y);
        // 12 -> 9 o'clock
        cubicTo(x + w2 - w2k, y, x, y + h2 - h2k, x, y + h2);
        // 9 -> 6 o'clock
        cubicTo(x, y + h2 + h2k, x + w2 - w2k, y + h, x + w2, y + h);
        // 6 -> 3 o'clock
        cubicTo(x + w2 + w2k, y + h, x + w, y + h2 + h2k, x + w, y + h2);
        // 3 -> 12 o'clock
        cubicTo(x + w, y + h2 - h2k, x + w2 + w2k, y, x + w2, y);
    }
    close();
}
void VPath::VPathData::addRect(const VRectF &rect, VPath::Direction dir) {
    if (rect.empty()) return;
    float x = rect.x();
    float y = rect.y();
    float w = rect.width();
    float h = rect.height();
    reserve(5, 6);
    // 1Move + 4Line + 1Close
    if (dir == VPath::Direction::CW) {
        moveTo(x + w, y);
        lineTo(x + w, y + h);
        lineTo(x, y + h);
        lineTo(x, y);
        close();
    } else {
        moveTo(x + w, y);
        lineTo(x, y);
        lineTo(x, y + h);
        lineTo(x + w, y + h);
        close();
    }
}
void VPath::VPathData::addRoundRect(const VRectF &rect, float roundness,
                                    VPath::Direction dir) {
    if (2 * roundness > rect.width()) roundness = rect.width() / 2.0f;
    if (2 * roundness > rect.height()) roundness = rect.height() / 2.0f;
    addRoundRect(rect, roundness, roundness, dir);
}
void VPath::VPathData::addRoundRect(const VRectF &rect, float rx, float ry,
                                    VPath::Direction dir) {
    if (vCompare(rx, 0.f) || vCompare(ry, 0.f)) {
        addRect(rect, dir);
        return;
    }
    float x = rect.x();
    float y = rect.y();
    float w = rect.width();
    float h = rect.height();
    // clamp the rx and ry radius value.
    rx = 2 * rx;
    ry = 2 * ry;
    if (rx > w) rx = w;
    if (ry > h) ry = h;
    reserve(17, 10);
    // 1Move + 4Cubic + 1Close
    if (dir == VPath::Direction::CW) {
        moveTo(x + w, y + ry / 2.f);
        arcTo(VRectF(x + w - rx, y + h - ry, rx, ry), 0, -90, false);
        arcTo(VRectF(x, y + h - ry, rx, ry), -90, -90, false);
        arcTo(VRectF(x, y, rx, ry), -180, -90, false);
        arcTo(VRectF(x + w - rx, y, rx, ry), -270, -90, false);
        close();
    } else {
        moveTo(x + w, y + ry / 2.f);
        arcTo(VRectF(x + w - rx, y, rx, ry), 0, 90, false);
        arcTo(VRectF(x, y, rx, ry), 90, 90, false);
        arcTo(VRectF(x, y + h - ry, rx, ry), 180, 90, false);
        arcTo(VRectF(x + w - rx, y + h - ry, rx, ry), 270, 90, false);
        close();
    }
}
static float tForArcAngle(float angle);
void         findEllipseCoords(const VRectF &r, float angle, float length,
                               VPointF *startPoint, VPointF *endPoint) {
    if (r.empty()) {
        if (startPoint) *startPoint = VPointF();
        if (endPoint) *endPoint = VPointF();
        return;
    }
    float w2 = r.width() / 2;
    float h2 = r.height() / 2;
    float    angles[2] = {
        angle, angle + length
    }
    ;
    VPointF *points[2] = {
        startPoint, endPoint
    }
    ;
    for (int i = 0; i < 2; ++i) {
        if (!points[i]) continue;
        float theta = angles[i] - 360 * floorf(angles[i] / 360);
        float t = theta / 90;
        // truncate
        int quadrant = int(t);
        t -= quadrant;
        t = tForArcAngle(90 * t);
        // swap x and y?
        if (quadrant & 1) t = 1 - t;
        float a, b, c, d;
        VBezier::coefficients(t, a, b, c, d);
        VPointF p(a + b + c * PATH_KAPPA, d + c + b * PATH_KAPPA);
        // left quadrants
        if (quadrant == 1 || quadrant == 2) p.rx() = -p.x();
        // top quadrants
        if (quadrant == 0 || quadrant == 1) p.ry() = -p.y();
        *points[i] = r.center() + VPointF(w2 * p.x(), h2 * p.y());
    }
}
static float tForArcAngle(float angle) {
    float radians, cos_angle, sin_angle, tc, ts, t;
    if (vCompare(angle, 0.f)) return 0;
    if (vCompare(angle, 90.0f)) return 1;
    radians = (angle / 180) * K_PI;
    cos_angle = cosf(radians);
    sin_angle = sinf(radians);
    // initial guess
    tc = angle / 90;
    // do some iterations of newton's method to approximate cos_angle
    // finds the zero of the function b.pointAt(tc).x() - cos_angle
    tc -= ((((2 - 3 * PATH_KAPPA) * tc + 3 * (PATH_KAPPA - 1)) * tc) * tc + 1 -
           cos_angle)  // value
        / (((6 - 9 * PATH_KAPPA) * tc + 6 * (PATH_KAPPA - 1)) *
           tc);
    // derivative
    tc -= ((((2 - 3 * PATH_KAPPA) * tc + 3 * (PATH_KAPPA - 1)) * tc) * tc + 1 -
           cos_angle)  // value
        / (((6 - 9 * PATH_KAPPA) * tc + 6 * (PATH_KAPPA - 1)) *
           tc);
    // derivative
    // initial guess
    ts = tc;
    // do some iterations of newton's method to approximate sin_angle
    // finds the zero of the function b.pointAt(tc).y() - sin_angle
    ts -= ((((3 * PATH_KAPPA - 2) * ts - 6 * PATH_KAPPA + 3) * ts +
           3 * PATH_KAPPA) *
           ts -
           sin_angle) /
        (((9 * PATH_KAPPA - 6) * ts + 12 * PATH_KAPPA - 6) * ts +
         3 * PATH_KAPPA);
    ts -= ((((3 * PATH_KAPPA - 2) * ts - 6 * PATH_KAPPA + 3) * ts +
           3 * PATH_KAPPA) *
           ts -
           sin_angle) /
        (((9 * PATH_KAPPA - 6) * ts + 12 * PATH_KAPPA - 6) * ts +
         3 * PATH_KAPPA);
    // use the average of the t that best approximates cos_angle
    // and the t that best approximates sin_angle
    t = 0.5f * (tc + ts);
    return t;
}
// The return value is the starting point of the arc
static VPointF curvesForArc(const VRectF &rect, float startAngle,
                            float sweepLength, VPointF *curves,
                            size_t *point_count) {
    if (rect.empty()) {
        return {
        }
        ;
    }
    float x = rect.x();
    float y = rect.y();
    float w = rect.width();
    float w2 = rect.width() / 2;
    float w2k = w2 * PATH_KAPPA;
    float h = rect.height();
    float h2 = rect.height() / 2;
    float h2k = h2 * PATH_KAPPA;
    VPointF points[16] = {
        // start point
        VPointF(x + w, y + h2),
        // 0 -> 270 degrees
        VPointF(x + w, y + h2 + h2k), VPointF(x + w2 + w2k, y + h),
        VPointF(x + w2, y + h),
        // 270 -> 180 degrees
        VPointF(x + w2 - w2k, y + h), VPointF(x, y + h2 + h2k),
        VPointF(x, y + h2),
        // 180 -> 90 degrees
        VPointF(x, y + h2 - h2k), VPointF(x + w2 - w2k, y), VPointF(x + w2, y),
        // 90 -> 0 degrees
        VPointF(x + w2 + w2k, y), VPointF(x + w, y + h2 - h2k),
        VPointF(x + w, y + h2)
    }
    ;
    if (sweepLength > 360)
        sweepLength = 360; else if (sweepLength < -360)
        sweepLength = -360;
    // Special case fast paths
    if (startAngle == 0.0f) {
        if (vCompare(sweepLength, 360)) {
            for (int i = 11; i >= 0; --i) curves[(*point_count)++] = points[i];
            return points[12];
        } else if (vCompare(sweepLength, -360)) {
            for (int i = 1; i <= 12; ++i) curves[(*point_count)++] = points[i];
            return points[0];
        }
    }
    int startSegment = int(floorf(startAngle / 90.0f));
    int endSegment = int(floorf((startAngle + sweepLength) / 90.0f));
    float startT = (startAngle - startSegment * 90) / 90;
    float endT = (startAngle + sweepLength - endSegment * 90) / 90;
    int delta = sweepLength > 0 ? 1 : -1;
    if (delta < 0) {
        startT = 1 - startT;
        endT = 1 - endT;
    }
    // avoid empty start segment
    if (vIsZero(startT - float(1))) {
        startT = 0;
        startSegment += delta;
    }
    // avoid empty end segment
    if (vIsZero(endT)) {
        endT = 1;
        endSegment -= delta;
    }
    startT = tForArcAngle(startT * 90);
    endT = tForArcAngle(endT * 90);
    const bool splitAtStart = !vIsZero(startT);
    const bool splitAtEnd = !vIsZero(endT - float(1));
    const int end = endSegment + delta;
    // empty arc?
    if (startSegment == end) {
        const int quadrant = 3 - ((startSegment % 4) + 4) % 4;
        const int j = 3 * quadrant;
        return delta > 0 ? points[j + 3] : points[j];
    }
    VPointF startPoint, endPoint;
    findEllipseCoords(rect, startAngle, sweepLength, &startPoint, &endPoint);
    for (int i = startSegment; i != end; i += delta) {
        const int quadrant = 3 - ((i % 4) + 4) % 4;
        const int j = 3 * quadrant;
        VBezier b;
        if (delta > 0)
            b = VBezier::fromPoints(points[j + 3], points[j + 2], points[j + 1],
                                    points[j]); else
            b = VBezier::fromPoints(points[j], points[j + 1], points[j + 2],
                                    points[j + 3]);
        // empty arc?
        if (startSegment == endSegment && vCompare(startT, endT))
            return startPoint;
        if (i == startSegment) {
            if (i == endSegment && splitAtEnd)
                b = b.onInterval(startT, endT); else if (splitAtStart)
                b = b.onInterval(startT, 1);
        } else if (i == endSegment && splitAtEnd) {
            b = b.onInterval(0, endT);
        }
        // push control points
        curves[(*point_count)++] = b.pt2();
        curves[(*point_count)++] = b.pt3();
        curves[(*point_count)++] = b.pt4();
    }
    curves[*(point_count) - 1] = endPoint;
    return startPoint;
}
void VPath::VPathData::addPolystar(float points, float innerRadius,
                                   float outerRadius, float innerRoundness,
                                   float outerRoundness, float startAngle,
                                   float cx, float cy, VPath::Direction dir) {
    const static float POLYSTAR_MAGIC_NUMBER = 0.47829f / 0.28f;
    float              currentAngle = (startAngle - 90.0f) * K_PI / 180.0f;
    float              x;
    float              y;
    float              partialPointRadius = 0;
    float              anglePerPoint = (2.0f * K_PI / points);
    float              halfAnglePerPoint = anglePerPoint / 2.0f;
    float              partialPointAmount = points - floorf(points);
    bool               longSegment = false;
    size_t             numPoints = size_t(ceilf(points) * 2);
    float              angleDir = ((dir == VPath::Direction::CW) ? 1.0f : -1.0f);
    bool               hasRoundness = false;
    innerRoundness /= 100.0f;
    outerRoundness /= 100.0f;
    if (!vCompare(partialPointAmount, 0)) {
        currentAngle +=
            halfAnglePerPoint * (1.0f - partialPointAmount) * angleDir;
    }
    if (!vCompare(partialPointAmount, 0)) {
        partialPointRadius =
            innerRadius + partialPointAmount * (outerRadius - innerRadius);
        x = partialPointRadius * cosf(currentAngle);
        y = partialPointRadius * sinf(currentAngle);
        currentAngle += anglePerPoint * partialPointAmount / 2.0f * angleDir;
    } else {
        x = outerRadius * cosf(currentAngle);
        y = outerRadius * sinf(currentAngle);
        currentAngle += halfAnglePerPoint * angleDir;
    }
    if (vIsZero(innerRoundness) && vIsZero(outerRoundness)) {
        reserve(numPoints + 2, numPoints + 3);
    } else {
        reserve(numPoints * 3 + 2, numPoints + 3);
        hasRoundness = true;
    }
    moveTo(x + cx, y + cy);
    for (size_t i = 0; i < numPoints; i++) {
        float radius = longSegment ? outerRadius : innerRadius;
        float dTheta = halfAnglePerPoint;
        if (!vIsZero(partialPointRadius) && i == numPoints - 2) {
            dTheta = anglePerPoint * partialPointAmount / 2.0f;
        }
        if (!vIsZero(partialPointRadius) && i == numPoints - 1) {
            radius = partialPointRadius;
        }
        float previousX = x;
        float previousY = y;
        x = radius * cosf(currentAngle);
        y = radius * sinf(currentAngle);
        if (hasRoundness) {
            float cp1Theta =
                (atan2f(previousY, previousX) - K_PI / 2.0f * angleDir);
            float cp1Dx = cosf(cp1Theta);
            float cp1Dy = sinf(cp1Theta);
            float cp2Theta = (atan2f(y, x) - K_PI / 2.0f * angleDir);
            float cp2Dx = cosf(cp2Theta);
            float cp2Dy = sinf(cp2Theta);
            float cp1Roundness = longSegment ? innerRoundness : outerRoundness;
            float cp2Roundness = longSegment ? outerRoundness : innerRoundness;
            float cp1Radius = longSegment ? innerRadius : outerRadius;
            float cp2Radius = longSegment ? outerRadius : innerRadius;
            float cp1x = cp1Radius * cp1Roundness * POLYSTAR_MAGIC_NUMBER *
                cp1Dx / points;
            float cp1y = cp1Radius * cp1Roundness * POLYSTAR_MAGIC_NUMBER *
                cp1Dy / points;
            float cp2x = cp2Radius * cp2Roundness * POLYSTAR_MAGIC_NUMBER *
                cp2Dx / points;
            float cp2y = cp2Radius * cp2Roundness * POLYSTAR_MAGIC_NUMBER *
                cp2Dy / points;
            if (!vIsZero(partialPointAmount) &&
                ((i == 0) || (i == numPoints - 1))) {
                cp1x *= partialPointAmount;
                cp1y *= partialPointAmount;
                cp2x *= partialPointAmount;
                cp2y *= partialPointAmount;
            }
            cubicTo(previousX - cp1x + cx, previousY - cp1y + cy, x + cp2x + cx,
                    y + cp2y + cy, x + cx, y + cy);
        } else {
            lineTo(x + cx, y + cy);
        }
        currentAngle += dTheta * angleDir;
        longSegment = !longSegment;
    }
    close();
}
void VPath::VPathData::addPolygon(float points, float radius, float roundness,
                                  float startAngle, float cx, float cy,
                                  VPath::Direction dir) {
    // TODO: Need to support floating point number for number of points
    const static float POLYGON_MAGIC_NUMBER = 0.25;
    float              currentAngle = (startAngle - 90.0f) * K_PI / 180.0f;
    float              x;
    float              y;
    float              anglePerPoint = 2.0f * K_PI / floorf(points);
    size_t             numPoints = size_t(floorf(points));
    float              angleDir = ((dir == VPath::Direction::CW) ? 1.0f : -1.0f);
    bool               hasRoundness = false;
    roundness /= 100.0f;
    currentAngle = (currentAngle - 90.0f) * K_PI / 180.0f;
    x = radius * cosf(currentAngle);
    y = radius * sinf(currentAngle);
    currentAngle += anglePerPoint * angleDir;
    if (vIsZero(roundness)) {
        reserve(numPoints + 2, numPoints + 3);
    } else {
        reserve(numPoints * 3 + 2, numPoints + 3);
        hasRoundness = true;
    }
    moveTo(x + cx, y + cy);
    for (size_t i = 0; i < numPoints; i++) {
        float previousX = x;
        float previousY = y;
        x = (radius * cosf(currentAngle));
        y = (radius * sinf(currentAngle));
        if (hasRoundness) {
            float cp1Theta =
                (atan2f(previousY, previousX) - K_PI / 2.0f * angleDir);
            float cp1Dx = cosf(cp1Theta);
            float cp1Dy = sinf(cp1Theta);
            float cp2Theta = atan2f(y, x) - K_PI / 2.0f * angleDir;
            float cp2Dx = cosf(cp2Theta);
            float cp2Dy = sinf(cp2Theta);
            float cp1x = radius * roundness * POLYGON_MAGIC_NUMBER * cp1Dx;
            float cp1y = radius * roundness * POLYGON_MAGIC_NUMBER * cp1Dy;
            float cp2x = radius * roundness * POLYGON_MAGIC_NUMBER * cp2Dx;
            float cp2y = radius * roundness * POLYGON_MAGIC_NUMBER * cp2Dy;
            cubicTo(previousX - cp1x + cx, previousY - cp1y + cy, x + cp2x + cx,
                    y + cp2y + cy, x, y);
        } else {
            lineTo(x + cx, y + cy);
        }
        currentAngle += anglePerPoint * angleDir;
    }
    close();
}
void VPath::VPathData::addPath(const VPathData &path, const VMatrix *m) {
    size_t segment = path.segments();
    // make sure enough memory available
    if (m_points.capacity() < m_points.size() + path.m_points.size())
        m_points.reserve(m_points.size() + path.m_points.size());
    if (m_elements.capacity() < m_elements.size() + path.m_elements.size())
        m_elements.reserve(m_elements.size() + path.m_elements.size());
    if (m) {
        for (const auto &i : path.m_points) {
            m_points.push_back(m->map(i));
        }
    } else {
        std::copy(path.m_points.begin(), path.m_points.end(),
                  std::back_inserter(m_points));
    }
    std::copy(path.m_elements.begin(), path.m_elements.end(),
              std::back_inserter(m_elements));
    m_segments += segment;
    mLengthDirty = true;
}
void VPainter::drawRle(const VPoint &, const VRle &rle) {
    if (rle.empty()) return;
    // mSpanData.updateSpanFunc();
    if (!mSpanData.mUnclippedBlendFunc) return;
    // do draw after applying clip.
    rle.intersect(mSpanData.clipRect(), mSpanData.mUnclippedBlendFunc,
                  &mSpanData);
}
void VPainter::drawRle(const VRle &rle, const VRle &clip) {
    if (rle.empty() || clip.empty()) return;
    if (!mSpanData.mUnclippedBlendFunc) return;
    rle.intersect(clip, mSpanData.mUnclippedBlendFunc, &mSpanData);
}
static void fillRect(const VRect &r, VSpanData *data) {
    auto x1 = std::max(r.x(), 0);
    auto x2 = std::min(r.x() + r.width(), data->mDrawableSize.width());
    auto y1 = std::max(r.y(), 0);
    auto y2 = std::min(r.y() + r.height(), data->mDrawableSize.height());
    if (x2 <= x1 || y2 <= y1) return;
    const int  nspans = 256;
    VRle::Span spans[nspans];
    int y = y1;
    while (y < y2) {
        int n = std::min(nspans, y2 - y);
        int i = 0;
        while (i < n) {
            spans[i].x = short(x1);
            spans[i].len = ushort(x2 - x1);
            spans[i].y = short(y + i);
            spans[i].coverage = 255;
            ++i;
        }
        data->mUnclippedBlendFunc(n, spans, data);
        y += n;
    }
}
void VPainter::drawBitmapUntransform(const VRect &  target,
                                     const VBitmap &bitmap,
                                     const VRect &  source,
                                     uint8_t        const_alpha) {
    mSpanData.initTexture(&bitmap, const_alpha, VBitmapData::Plain, source);
    if (!mSpanData.mUnclippedBlendFunc) return;
    mSpanData.dx = float(-target.x());
    mSpanData.dy = float(-target.y());
    VRect rr = source.translated(target.x(), target.y());
    fillRect(rr, &mSpanData);
}
VPainter::VPainter(VBitmap *buffer) {
    begin(buffer);
}
bool VPainter::begin(VBitmap *buffer) {
    mBuffer.prepare(buffer);
    mSpanData.init(&mBuffer);
    // TODO find a better api to clear the surface
    mBuffer.clear();
    return true;
}
void VPainter::end() {
}
void VPainter::setDrawRegion(const VRect &region) {
    mSpanData.setDrawRegion(region);
}
void VPainter::setBrush(const VBrush &brush) {
    mSpanData.setup(brush);
}
void VPainter::setBlendMode(BlendMode mode) {
    mSpanData.mBlendMode = mode;
}
VRect VPainter::clipBoundingRect() const {
    return mSpanData.clipRect();
}
void VPainter::drawBitmap(const VPoint &point, const VBitmap &bitmap,
                          const VRect &source, uint8_t const_alpha) {
    if (!bitmap.valid()) return;
    drawBitmap(VRect(point, bitmap.size()),
               bitmap, source, const_alpha);
}
void VPainter::drawBitmap(const VRect &target, const VBitmap &bitmap,
                          const VRect &source, uint8_t const_alpha) {
    if (!bitmap.valid()) return;
    // clear any existing brush data.
    setBrush(VBrush());
    if (target.size() == source.size()) {
        drawBitmapUntransform(target, bitmap, source, const_alpha);
    } else {
        // @TODO scaling
    }
}
void VPainter::drawBitmap(const VPoint &point, const VBitmap &bitmap,
                          uint8_t const_alpha) {
    if (!bitmap.valid()) return;
    drawBitmap(VRect(point, bitmap.size()),
               bitmap, bitmap.rect(),
               const_alpha);
}
void VPainter::drawBitmap(const VRect &rect, const VBitmap &bitmap,
                          uint8_t const_alpha) {
    if (!bitmap.valid()) return;
    drawBitmap(rect, bitmap, bitmap.rect(),
               const_alpha);
}
constexpr int NEWTON_ITERATIONS = 4;
constexpr float NEWTON_MIN_SLOPE = 0.02f;
constexpr float SUBDIVISION_PRECISION = 0.0000001f;
constexpr int SUBDIVISION_MAX_ITERATIONS = 10;
constexpr float kSampleStepSize = 1.0f / float(VInterpolator::kSplineTableSize - 1);
void VInterpolator::init(float aX1, float aY1, float aX2, float aY2) {
    mX1 = aX1;
    mY1 = aY1;
    mX2 = aX2;
    mY2 = aY2;
    if (mX1 != mY1 || mX2 != mY2) CalcSampleValues();
}
/*static*/
float VInterpolator::CalcBezier(float aT, float aA1, float aA2) {
    // use Horner's scheme to evaluate the Bezier polynomial
    return ((A(aA1, aA2) * aT + B(aA1, aA2)) * aT + C(aA1)) * aT;
}
void VInterpolator::CalcSampleValues() {
    for (int i = 0; i < kSplineTableSize; ++i) {
        mSampleValues[i] = CalcBezier(float(i) * kSampleStepSize, mX1, mX2);
    }
}
float VInterpolator::GetSlope(float aT, float aA1, float aA2) {
    return 3.0f * A(aA1, aA2) * aT * aT + 2.0f * B(aA1, aA2) * aT + C(aA1);
}
float VInterpolator::value(float aX) const {
    if (mX1 == mY1 && mX2 == mY2) return aX;
    return CalcBezier(GetTForX(aX), mY1, mY2);
}
float VInterpolator::GetTForX(float aX) const {
    // Find interval where t lies
    float              intervalStart = 0.0;
    const float*       currentSample = &mSampleValues[1];
    const float* const lastSample = &mSampleValues[kSplineTableSize - 1];
    for (; currentSample != lastSample && *currentSample <= aX;
         ++currentSample) {
        intervalStart += kSampleStepSize;
    }
    --currentSample;
    // t now lies between *currentSample and *currentSample+1
    // Interpolate to provide an initial guess for t
    float dist =
        (aX - *currentSample) / (*(currentSample + 1) - *currentSample);
    float guessForT = intervalStart + dist * kSampleStepSize;
    // Check the slope to see what strategy to use. If the slope is too small
    // Newton-Raphson iteration won't converge on a root so we use bisection
    // instead.
    float initialSlope = GetSlope(guessForT, mX1, mX2);
    if (initialSlope >= NEWTON_MIN_SLOPE) {
        return NewtonRaphsonIterate(aX, guessForT);
    } else if (initialSlope == 0.0) {
        return guessForT;
    } else {
        return BinarySubdivide(aX, intervalStart,
                               intervalStart + kSampleStepSize);
    }
}
float VInterpolator::NewtonRaphsonIterate(float aX, float aGuessT) const {
    // Refine guess with Newton-Raphson iteration
    for (int i = 0; i < NEWTON_ITERATIONS; ++i) {
        // We're trying to find where f(t) = aX,
        // so we're actually looking for a root for: CalcBezier(t) - aX
        float currentX = CalcBezier(aGuessT, mX1, mX2) - aX;
        float currentSlope = GetSlope(aGuessT, mX1, mX2);
        if (currentSlope == 0.0) return aGuessT;
        aGuessT -= currentX / currentSlope;
    }
    return aGuessT;
}
float VInterpolator::BinarySubdivide(float aX, float aA, float aB) const {
    float currentX;
    float currentT;
    int   i = 0;
    do {
        currentT = aA + (aB - aA) / 2.0f;
        currentX = CalcBezier(currentT, mX1, mX2) - aX;
        if (currentX > 0.0) {
            aB = currentT;
        } else {
            aA = currentT;
        }
    }
    while (fabs(currentX) > SUBDIVISION_PRECISION &&
           ++i < SUBDIVISION_MAX_ITERATIONS);
    return currentT;
}
VImageLoader::VImageLoader() : mImpl(std::make_unique<VImageLoader::Impl>()) {
}
VImageLoader::~VImageLoader() {
}
VImageLoader &VImageLoader::instance() {
    static VImageLoader singleton;
    return singleton;
}
VBitmap VImageLoader::load(const char *fileName) {
    return mImpl->load(fileName);
}
VBitmap VImageLoader::load(const char *data, size_t len) {
    return mImpl->load(data, int(len));
}
void VRasterBuffer::clear() {
    if (mNeedClear)
        memset(mBuffer, 0, mHeight * mBytesPerLine);
}
VBitmap::Format VRasterBuffer::prepare(VBitmap *image) {
    mBuffer = image->data();
    mWidth = image->width();
    mHeight = image->height();
    mBytesPerPixel = 4;
    mBytesPerLine = image->stride();
    mNeedClear = image->isNeedClear();
    mFormat = image->format();
    return mFormat;
}
class VGradientCache {
public:
    struct CacheInfo : public VColorTable {
        inline CacheInfo(VGradientStops s) : stops(std::move(s)) {
        }
        VGradientStops stops;
    }
    ;
    using VCacheData = std::shared_ptr<const CacheInfo>;
    using VCacheKey = int64_t;
    using VGradientColorTableHash =
        std::unordered_multimap<VCacheKey, VCacheData>;
    bool generateGradientColorTable(const VGradientStops &stops, float alpha,
                                    uint32_t *colorTable, int size);
    VCacheData getBuffer(const VGradient &gradient) {
        VCacheKey             hash_val = 0;
        VCacheData            info;
        const VGradientStops &stops = gradient.mStops;
        for (uint i = 0; i < stops.size() && i <= 2; i++)
            hash_val += VCacheKey(stops[i].second.premulARGB() * gradient.alpha()); {
            ::std::lock_guard<::std::mutex> guard(mMutex);
            size_t count = mCache.count(hash_val);
            if (!count) {
                // key is not present in the hash
                info = addCacheElement(hash_val, gradient);
            } else if (count == 1) {
                auto search = mCache.find(hash_val);
                if (search->second->stops == stops) {
                    info = search->second;
                } else {
                    // didn't find an exact match
                    info = addCacheElement(hash_val, gradient);
                }
            } else {
                // we have a multiple data with same key
                auto range = mCache.equal_range(hash_val);
                for (auto it = range.first; it != range.second; ++it) {
                    if (it->second->stops == stops) {
                        info = it->second;
                        break;
                    }
                }
                if (!info) {
                    // didn't find an exact match
                    info = addCacheElement(hash_val, gradient);
                }
            }
        }
        return info;
    }
    static VGradientCache &instance() {
        static VGradientCache CACHE;
        return CACHE;
    }
protected:
    uint       maxCacheSize() const {
        return 60;
    }
    VCacheData addCacheElement(VCacheKey hash_val, const VGradient &gradient) {
        if (mCache.size() == maxCacheSize()) {
            uint count = maxCacheSize() / 10;
            while (count--) {
                mCache.erase(mCache.begin());
            }
        }
        auto cache_entry = std::make_shared<CacheInfo>(gradient.mStops);
        cache_entry->alpha = generateGradientColorTable(
            gradient.mStops, gradient.alpha(), cache_entry->buffer32,
            VGradient::colorTableSize);
        mCache.insert(std::make_pair(hash_val, cache_entry));
        return cache_entry;
    }
private:
    VGradientCache() = default;
    VGradientColorTableHash mCache;
    ::std::mutex              mMutex;
}
;
#define FIXPT_BITS 8
#define FIXPT_SIZE (1 << FIXPT_BITS)
static inline void getLinearGradientValues(LinearGradientValues *v,
                                           const VSpanData *     data) {
    const VGradientData *grad = &data->mGradient;
    v->dx = grad->linear.x2 - grad->linear.x1;
    v->dy = grad->linear.y2 - grad->linear.y1;
    v->l = v->dx * v->dx + v->dy * v->dy;
    v->off = 0;
    if (v->l != 0) {
        v->dx /= v->l;
        v->dy /= v->l;
        v->off = -v->dx * grad->linear.x1 - v->dy * grad->linear.y1;
    }
}
static inline int gradientClamp(const VGradientData *grad, int ipos) {
    int limit;
    if (grad->mSpread == VGradient::Spread::Repeat) {
        ipos = ipos % VGradient::colorTableSize;
        ipos = ipos < 0 ? VGradient::colorTableSize + ipos : ipos;
    } else if (grad->mSpread == VGradient::Spread::Reflect) {
        limit = VGradient::colorTableSize * 2;
        ipos = ipos % limit;
        ipos = ipos < 0 ? limit + ipos : ipos;
        ipos = ipos >= VGradient::colorTableSize ? limit - 1 - ipos : ipos;
    } else {
        if (ipos < 0)
            ipos = 0; else if (ipos >= VGradient::colorTableSize)
            ipos = VGradient::colorTableSize - 1;
    }
    return ipos;
}
static inline uint32_t gradientPixel(const VGradientData *grad, float pos) {
    int ipos = (int)(pos * (VGradient::colorTableSize - 1) + (float)(0.5));
    return grad->mColorTable[gradientClamp(grad, ipos)];
}
static uint32_t gradientPixelFixed(const VGradientData *grad, int fixed_pos) {
    int ipos = (fixed_pos + (FIXPT_SIZE / 2)) >> FIXPT_BITS;
    return grad->mColorTable[gradientClamp(grad, ipos)];
}
void memfill32(uint32_t *dest, uint32_t value, int length) {
    int n;
    if (length <= 0) return;
    // Cute hack to align future memcopy operation
    // and do unroll the loop a bit. Not sure it is
    // the most efficient, but will do for now.
    n = (length + 7) / 8;
    switch (length & 0x07) {
    case 0:
    do {
        *dest++ = value;
        VECTOR_FALLTHROUGH;
    case 7:
    *dest++ = value;
    VECTOR_FALLTHROUGH;
    case 6:
    *dest++ = value;
    VECTOR_FALLTHROUGH;
    case 5:
    *dest++ = value;
    VECTOR_FALLTHROUGH;
    case 4:
    *dest++ = value;
    VECTOR_FALLTHROUGH;
    case 3:
    *dest++ = value;
    VECTOR_FALLTHROUGH;
    case 2:
    *dest++ = value;
    VECTOR_FALLTHROUGH;
    case 1:
    *dest++ = value;
    }
    while (--n > 0);
    }
}
void fetch_linear_gradient(uint32_t *buffer, const Operator *op,
                           const VSpanData *data, int y, int x, int length) {
    float                t, inc;
    const VGradientData *gradient = &data->mGradient;
    bool  affine = true;
    float rx = 0, ry = 0;
    if (op->linear.l == 0) {
        t = inc = 0;
    } else {
        rx = data->m21 * (y + float(0.5)) + data->m11 * (x + float(0.5)) +
            data->dx;
        ry = data->m22 * (y + float(0.5)) + data->m12 * (x + float(0.5)) +
            data->dy;
        t = op->linear.dx * rx + op->linear.dy * ry + op->linear.off;
        inc = op->linear.dx * data->m11 + op->linear.dy * data->m12;
        affine = !data->m13 && !data->m23;
        if (affine) {
            t *= (VGradient::colorTableSize - 1);
            inc *= (VGradient::colorTableSize - 1);
        }
    }
    const uint32_t *end = buffer + length;
    if (affine) {
        if (inc > float(-1e-5) && inc < float(1e-5)) {
            memfill32(buffer, gradientPixelFixed(gradient, int(t * FIXPT_SIZE)),
                      length);
        } else {
            if (t + inc * length < float(INT_MAX >> (FIXPT_BITS + 1)) &&
                t + inc * length > float(INT_MIN >> (FIXPT_BITS + 1))) {
                // we can use fixed point math
                int t_fixed = int(t * FIXPT_SIZE);
                int inc_fixed = int(inc * FIXPT_SIZE);
                while (buffer < end) {
                    *buffer = gradientPixelFixed(gradient, t_fixed);
                    t_fixed += inc_fixed;
                    ++buffer;
                }
            } else {
                // we have to fall back to float math
                while (buffer < end) {
                    *buffer =
                        gradientPixel(gradient, t / VGradient::colorTableSize);
                    t += inc;
                    ++buffer;
                }
            }
        }
    } else {
        // fall back to float math here as well
        float rw = data->m23 * (y + float(0.5)) + data->m13 * (x + float(0.5)) +
            data->m33;
        while (buffer < end) {
            float xt = rx / rw;
            float yt = ry / rw;
            t = (op->linear.dx * xt + op->linear.dy * yt) + op->linear.off;
            *buffer = gradientPixel(gradient, t);
            rx += data->m11;
            ry += data->m12;
            rw += data->m13;
            if (!rw) {
                rw += data->m13;
            }
            ++buffer;
        }
    }
}
static inline void getRadialGradientValues(RadialGradientValues *v,
                                           const VSpanData *     data) {
    const VGradientData &gradient = data->mGradient;
    v->dx = gradient.radial.cx - gradient.radial.fx;
    v->dy = gradient.radial.cy - gradient.radial.fy;
    v->dr = gradient.radial.cradius - gradient.radial.fradius;
    v->sqrfr = gradient.radial.fradius * gradient.radial.fradius;
    v->a = v->dr * v->dr - v->dx * v->dx - v->dy * v->dy;
    v->inv2a = 1 / (2 * v->a);
    v->extended = !vIsZero(gradient.radial.fradius) || v->a <= 0;
}
static void fetch(uint32_t *buffer, uint32_t *end, const Operator *op,
                  const VSpanData *data, float det, float delta_det,
                  float delta_delta_det, float b, float delta_b) {
    if (op->radial.extended) {
        while (buffer < end) {
            uint32_t result = 0;
            if (det >= 0) {
                float w = std::sqrt(det) - b;
                if (data->mGradient.radial.fradius + op->radial.dr * w >= 0)
                    result = gradientPixel(&data->mGradient, w);
            }
            *buffer = result;
            det += delta_det;
            delta_det += delta_delta_det;
            b += delta_b;
            ++buffer;
        }
    } else {
        while (buffer < end) {
            *buffer++ = gradientPixel(&data->mGradient, std::sqrt(det) - b);
            det += delta_det;
            delta_det += delta_delta_det;
            b += delta_b;
        }
    }
}
static inline float radialDeterminant(float a, float b, float c) {
    return (b * b) - (4 * a * c);
}
void fetch_radial_gradient(uint32_t *buffer, const Operator *op,
                           const VSpanData *data, int y, int x, int length) {
    // avoid division by zero
    if (vIsZero(op->radial.a)) {
        memfill32(buffer, 0, length);
        return;
    }
    float rx =
        data->m21 * (y + float(0.5)) + data->dx + data->m11 * (x + float(0.5));
    float ry =
        data->m22 * (y + float(0.5)) + data->dy + data->m12 * (x + float(0.5));
    bool affine = !data->m13 && !data->m23;
    uint32_t *end = buffer + length;
    if (affine) {
        rx -= data->mGradient.radial.fx;
        ry -= data->mGradient.radial.fy;
        float inv_a = 1 / float(2 * op->radial.a);
        const float delta_rx = data->m11;
        const float delta_ry = data->m12;
        float b = 2 * (op->radial.dr * data->mGradient.radial.fradius +
                       rx * op->radial.dx + ry * op->radial.dy);
        float delta_b =
            2 * (delta_rx * op->radial.dx + delta_ry * op->radial.dy);
        const float b_delta_b = 2 * b * delta_b;
        const float delta_b_delta_b = 2 * delta_b * delta_b;
        const float bb = b * b;
        const float delta_bb = delta_b * delta_b;
        b *= inv_a;
        delta_b *= inv_a;
        const float rxrxryry = rx * rx + ry * ry;
        const float delta_rxrxryry = delta_rx * delta_rx + delta_ry * delta_ry;
        const float rx_plus_ry = 2 * (rx * delta_rx + ry * delta_ry);
        const float delta_rx_plus_ry = 2 * delta_rxrxryry;
        inv_a *= inv_a;
        float det =
            (bb - 4 * op->radial.a * (op->radial.sqrfr - rxrxryry)) * inv_a;
        float delta_det = (b_delta_b + delta_bb +
                           4 * op->radial.a * (rx_plus_ry + delta_rxrxryry)) *
            inv_a;
        const float delta_delta_det =
            (delta_b_delta_b + 4 * op->radial.a * delta_rx_plus_ry) * inv_a;
        fetch(buffer, end, op, data, det, delta_det, delta_delta_det, b,
              delta_b);
    } else {
        float rw = data->m23 * (y + float(0.5)) + data->m33 +
            data->m13 * (x + float(0.5));
        while (buffer < end) {
            if (rw == 0) {
                *buffer = 0;
            } else {
                float invRw = 1 / rw;
                float gx = rx * invRw - data->mGradient.radial.fx;
                float gy = ry * invRw - data->mGradient.radial.fy;
                float b = 2 * (op->radial.dr * data->mGradient.radial.fradius +
                               gx * op->radial.dx + gy * op->radial.dy);
                float det = radialDeterminant(
                    op->radial.a, b, op->radial.sqrfr - (gx * gx + gy * gy));
                uint32_t result = 0;
                if (det >= 0) {
                    float detSqrt = std::sqrt(det);
                    float s0 = (-b - detSqrt) * op->radial.inv2a;
                    float s1 = (-b + detSqrt) * op->radial.inv2a;
                    float s = vMax(s0, s1);
                    if (data->mGradient.radial.fradius + op->radial.dr * s >= 0)
                        result = gradientPixel(&data->mGradient, s);
                }
                *buffer = result;
            }
            rx += data->m11;
            ry += data->m12;
            rw += data->m13;
            ++buffer;
        }
    }
}
extern CompositionFunction             COMP_functionForMode_C[];
extern CompositionFunctionSolid        COMP_functionForModeSolid_C[];
static const CompositionFunction *     functionForMode = COMP_functionForMode_C;
static const CompositionFunctionSolid *functionForModeSolid = COMP_functionForModeSolid_C;
static inline Operator getOperator(const VSpanData *data, const VRle::Span *,
                                   size_t) {
    Operator op;
    bool     solidSource = false;
    switch (data->mType) {
    case VSpanData::Type::Solid:
    solidSource = (vAlpha(data->mSolid) == 255);
    op.srcFetch = nullptr;
    break;
    case VSpanData::Type::LinearGradient:
    solidSource = false;
    getLinearGradientValues(&op.linear, data);
    op.srcFetch = &fetch_linear_gradient;
    break;
    case VSpanData::Type::RadialGradient:
    solidSource = false;
    getRadialGradientValues(&op.radial, data);
    op.srcFetch = &fetch_radial_gradient;
    break;
    default:
    op.srcFetch = nullptr;
    break;
    }
    op.mode = data->mBlendMode;
    if (op.mode == BlendMode::SrcOver && solidSource)
        op.mode = BlendMode::Src;
    op.funcSolid = functionForModeSolid[uint(op.mode)];
    op.func = functionForMode[uint(op.mode)];
    return op;
}
static void blendColorARGB(size_t count, const VRle::Span *spans,
                           void *userData) {
    VSpanData *data = (VSpanData *)(userData);
    Operator   op = getOperator(data, spans, count);
    const uint color = data->mSolid;
    if (op.mode == BlendMode::Src) {
        // inline for performance
        while (count--) {
            uint *target = data->buffer(spans->x, spans->y);
            if (spans->coverage == 255) {
                memfill32(target, color, spans->len);
            } else {
                uint c = BYTE_MUL(color, spans->coverage);
                int  ialpha = 255 - spans->coverage;
                for (int i = 0; i < spans->len; ++i)
                    target[i] = c + BYTE_MUL(target[i], ialpha);
            }
            ++spans;
        }
        return;
    }
    while (count--) {
        uint *target = data->buffer(spans->x, spans->y);
        op.funcSolid(target, spans->len, color, spans->coverage);
        ++spans;
    }
}
constexpr int BLEND_GRADIENT_BUFFER_SIZE = 2048;
static void blendGradientARGB(size_t count, const VRle::Span *spans,
                              void *userData) {
    VSpanData *data = (VSpanData *)(userData);
    Operator   op = getOperator(data, spans, count);
    unsigned int buffer[BLEND_GRADIENT_BUFFER_SIZE];
    if (!op.srcFetch) return;
    while (count--) {
        uint *target = data->buffer(spans->x, spans->y);
        int   length = spans->len;
        while (length) {
            int l = std::min(length, BLEND_GRADIENT_BUFFER_SIZE);
            op.srcFetch(buffer, &op, data, spans->y, spans->x, l);
            op.func(target, buffer, l, spans->coverage);
            target += l;
            length -= l;
        }
        ++spans;
    }
}

static void blend_untransformed_argb(size_t count, const VRle::Span *spans,
                                     void *userData) {
    VSpanData *data = reinterpret_cast<VSpanData *>(userData);
    if (data->mBitmap.format != VBitmap::Format::ARGB32_Premultiplied &&
        data->mBitmap.format != VBitmap::Format::ARGB32) {
        //@TODO other formats not yet handled.
        return;
    }
    Operator op = getOperator(data, spans, count);
    const int image_width = data->mBitmap.width;
    const int image_height = data->mBitmap.height;
    int xoff = int(data->dx);
    int yoff = int(data->dy);
    while (count--) {
        int x = spans->x;
        int length = spans->len;
        int sx = xoff + x;
        int sy = yoff + spans->y;
        if (sy >= 0 && sy < image_height && sx < image_width) {
            if (sx < 0) {
                x -= sx;
                length += sx;
                sx = 0;
            }
            if (sx + length > image_width) length = image_width - sx;
            if (length > 0) {
                const int coverage =
                    (spans->coverage * data->mBitmap.const_alpha) >> 8;
                const uint *src = (const uint *)data->mBitmap.scanLine(sy) + sx;
                uint *      dest = data->buffer(x, spans->y);
                op.func(dest, src, length, coverage);
            }
        }
        ++spans;
    }
}
template <class T>
constexpr const T &clamp(const T &v, const T &lo, const T &hi) { return v < lo ? lo : hi < v ? hi : v; }

constexpr int buffer_size = 1024;
constexpr int fixed_scale = 1 << 16;
static void      blend_transformed_argb(size_t count, const VRle::Span *spans, void *userData) {
    VSpanData *data = reinterpret_cast<VSpanData *>(userData);
    if (data->mBitmap.format != VBitmap::Format::ARGB32_Premultiplied &&
        data->mBitmap.format != VBitmap::Format::ARGB32) {
        //@TODO other formats not yet handled.
        return;
    }
    Operator op = getOperator(data, spans, count);
    uint     buffer[buffer_size];
    const int image_x1 = data->mBitmap.x1;
    const int image_y1 = data->mBitmap.y1;
    const int image_x2 = data->mBitmap.x2 - 1;
    const int image_y2 = data->mBitmap.y2 - 1;
    if (data->fast_matrix) {
        // The increment pr x in the scanline
        int fdx = (int)(data->m11 * fixed_scale);
        int fdy = (int)(data->m12 * fixed_scale);
        while (count--) {
            uint *target = data->buffer(spans->x, spans->y);
            const float cx = spans->x + float(0.5);
            const float cy = spans->y + float(0.5);
            int x =
                int((data->m21 * cy + data->m11 * cx + data->dx) * fixed_scale);
            int y =
                int((data->m22 * cy + data->m12 * cx + data->dy) * fixed_scale);
            int       length = spans->len;
            const int coverage =
                (spans->coverage * data->mBitmap.const_alpha) >> 8;
            while (length) {
                int         l = std::min(length, buffer_size);
                const uint *end = buffer + l;
                uint *      b = buffer;
                while (b < end) {
                    int px = clamp(x >> 16, image_x1, image_x2);
                    int py = clamp(y >> 16, image_y1, image_y2);
                    *b = reinterpret_cast<const uint *>(
                        data->mBitmap.scanLine(py))[px];
                    x += fdx;
                    y += fdy;
                    ++b;
                }
                op.func(target, buffer, l, coverage);
                target += l;
                length -= l;
            }
            ++spans;
        }
    } else {
        const float fdx = data->m11;
        const float fdy = data->m12;
        const float fdw = data->m13;
        while (count--) {
            uint *target = data->buffer(spans->x, spans->y);
            const float cx = spans->x + float(0.5);
            const float cy = spans->y + float(0.5);
            float x = data->m21 * cy + data->m11 * cx + data->dx;
            float y = data->m22 * cy + data->m12 * cx + data->dy;
            float w = data->m23 * cy + data->m13 * cx + data->m33;
            int       length = spans->len;
            const int coverage =
                (spans->coverage * data->mBitmap.const_alpha) >> 8;
            while (length) {
                int         l = std::min(length, buffer_size);
                const uint *end = buffer + l;
                uint *      b = buffer;
                while (b < end) {
                    const float iw = w == 0 ? 1 : 1 / w;
                    const float tx = x * iw;
                    const float ty = y * iw;
                    const int   px =
                        clamp(int(tx) - (tx < 0), image_x1, image_x2);
                    const int py =
                        clamp(int(ty) - (ty < 0), image_y1, image_y2);
                    *b = reinterpret_cast<const uint *>(
                        data->mBitmap.scanLine(py))[px];
                    x += fdx;
                    y += fdy;
                    w += fdw;
                    ++b;
                }
                op.func(target, buffer, l, coverage);
                target += l;
                length -= l;
            }
            ++spans;
        }
    }
}
void VSpanData::updateSpanFunc() {
    switch (mType) {
    case VSpanData::Type::None:
    mUnclippedBlendFunc = nullptr;
    break;
    case VSpanData::Type::Solid:
    mUnclippedBlendFunc = &blendColorARGB;
    break;
    case VSpanData::Type::LinearGradient:
    case VSpanData::Type::RadialGradient: {
        mUnclippedBlendFunc = &blendGradientARGB;
        break;
    }
    case VSpanData::Type::Texture: {
        //@TODO update proper image function.
        if (transformType <= VMatrix::MatrixType::Translate) {
            mUnclippedBlendFunc = &blend_untransformed_argb;
        } else {
            mUnclippedBlendFunc = &blend_transformed_argb;
        }
        break;
    }
    }
}
void VSpanData::init(VRasterBuffer *image) {
    mRasterBuffer = image;
    setDrawRegion(VRect(0, 0, int(image->width()), int(image->height())));
    mType = VSpanData::Type::None;
    mBlendFunc = nullptr;
    mUnclippedBlendFunc = nullptr;
}
void VSpanData::setup(const VBrush &brush, BlendMode /*mode*/, int /*alpha*/) {
    transformType = VMatrix::MatrixType::None;
    switch (brush.type()) {
    case VBrush::Type::NoBrush:
    mType = VSpanData::Type::None;
    break;
    case VBrush::Type::Solid:
    mType = VSpanData::Type::Solid;
    mSolid = brush.mColor.premulARGB();
    break;
    case VBrush::Type::LinearGradient: {
        mType = VSpanData::Type::LinearGradient;
        mColorTable = VGradientCache::instance().getBuffer(*brush.mGradient);
        mGradient.mColorTable = mColorTable->buffer32;
        mGradient.mColorTableAlpha = mColorTable->alpha;
        mGradient.linear.x1 = brush.mGradient->linear.x1;
        mGradient.linear.y1 = brush.mGradient->linear.y1;
        mGradient.linear.x2 = brush.mGradient->linear.x2;
        mGradient.linear.y2 = brush.mGradient->linear.y2;
        mGradient.mSpread = brush.mGradient->mSpread;
        setupMatrix(brush.mGradient->mMatrix);
        break;
    }
    case VBrush::Type::RadialGradient: {
        mType = VSpanData::Type::RadialGradient;
        mColorTable = VGradientCache::instance().getBuffer(*brush.mGradient);
        mGradient.mColorTable = mColorTable->buffer32;
        mGradient.mColorTableAlpha = mColorTable->alpha;
        mGradient.radial.cx = brush.mGradient->radial.cx;
        mGradient.radial.cy = brush.mGradient->radial.cy;
        mGradient.radial.fx = brush.mGradient->radial.fx;
        mGradient.radial.fy = brush.mGradient->radial.fy;
        mGradient.radial.cradius = brush.mGradient->radial.cradius;
        mGradient.radial.fradius = brush.mGradient->radial.fradius;
        mGradient.mSpread = brush.mGradient->mSpread;
        setupMatrix(brush.mGradient->mMatrix);
        break;
    }
    case VBrush::Type::Texture: {
        mType = VSpanData::Type::Texture;
        initTexture(
            &brush.mTexture->mBitmap, brush.mTexture->mAlpha, VBitmapData::Plain,
            brush.mTexture->mBitmap.rect());
        setupMatrix(brush.mTexture->mMatrix);
        break;
    }
    default:
    break;
    }
    updateSpanFunc();
}
void VSpanData::setupMatrix(const VMatrix &matrix) {
    VMatrix inv = matrix.inverted();
    m11 = inv.m11;
    m12 = inv.m12;
    m13 = inv.m13;
    m21 = inv.m21;
    m22 = inv.m22;
    m23 = inv.m23;
    m33 = inv.m33;
    dx = inv.mtx;
    dy = inv.mty;
    transformType = inv.type();
    const bool  affine = inv.isAffine();
    const float f1 = m11 * m11 + m21 * m21;
    const float f2 = m12 * m12 + m22 * m22;
    fast_matrix = affine && f1 < 1e4 && f2 < 1e4 && f1 > (1.0 / 65536) &&
        f2 > (1.0 / 65536) && fabs(dx) < 1e4 && fabs(dy) < 1e4;
}
void VSpanData::initTexture(const VBitmap *bitmap, int alpha,
                            VBitmapData::Type type, const VRect &sourceRect) {
    mType = VSpanData::Type::Texture;
    mBitmap.imageData = bitmap->data();
    mBitmap.width = int(bitmap->width());
    mBitmap.height = int(bitmap->height());
    mBitmap.bytesPerLine = static_cast<uint>(bitmap->stride());
    mBitmap.format = bitmap->format();
    mBitmap.x1 = sourceRect.x();
    mBitmap.y1 = sourceRect.y();
    mBitmap.x2 = std::min(mBitmap.x1 + sourceRect.width(), mBitmap.width);
    mBitmap.y2 = std::min(mBitmap.y1 + sourceRect.height(), mBitmap.height);
    mBitmap.const_alpha = alpha;
    mBitmap.type = type;
    updateSpanFunc();
}
bool VGradientCache::generateGradientColorTable(const VGradientStops &stops,
                                                float                 opacity,
                                                uint32_t *colorTable, int size) {
    int                  dist, idist, pos = 0;
    size_t i;
    bool                 alpha = false;
    size_t               stopCount = stops.size();
    const VGradientStop *curr, *next, *start;
    uint32_t             curColor, nextColor;
    float                delta, t, incr, fpos;
    if (!vCompare(opacity, 1.0f)) alpha = true;
    start = stops.data();
    curr = start;
    if (!curr->second.isOpaque()) alpha = true;
    curColor = curr->second.premulARGB(opacity);
    incr = 1.0f / (float)size;
    fpos = 1.5f * incr;
    colorTable[pos++] = curColor;
    while (fpos <= curr->first) {
        colorTable[pos] = colorTable[pos - 1];
        pos++;
        fpos += incr;
    }
    for (i = 0; i < stopCount - 1; ++i) {
        curr = (start + i);
        next = (start + i + 1);
        delta = 1 / (next->first - curr->first);
        if (!next->second.isOpaque()) alpha = true;
        nextColor = next->second.premulARGB(opacity);
        while (fpos < next->first && pos < size) {
            t = (fpos - curr->first) * delta;
            dist = (int)(255 * t);
            idist = 255 - dist;
            colorTable[pos] = INTERPOLATE_PIXEL_255(curColor, idist, nextColor, dist);
            ++pos;
            fpos += incr;
        }
        curColor = nextColor;
    }
    for (; pos < size; ++pos) colorTable[pos] = curColor;
    // Make sure the last color stop is represented at the end of the table
    colorTable[size - 1] = curColor;
    return alpha;
}

VDrawable::VDrawable(VDrawable::Type type)
{
    setType(type);
}

VDrawable::~VDrawable()
{
    if (mStrokeInfo) {
        if (mType == Type::StrokeWithDash) {
            delete static_cast<StrokeWithDashInfo *>(mStrokeInfo);
        } else {
            delete mStrokeInfo;
        }
    }
}

void VDrawable::setType(VDrawable::Type type)
{
    mType = type;
    if (mType == VDrawable::Type::Stroke) {
        mStrokeInfo = new StrokeInfo();
    } else if (mType == VDrawable::Type::StrokeWithDash) {
        mStrokeInfo = new StrokeWithDashInfo();
    }
}

void VDrawable::applyDashOp()
{
    if (mStrokeInfo && (mType == Type::StrokeWithDash)) {
        auto obj = static_cast<StrokeWithDashInfo *>(mStrokeInfo);
        if (!obj->mDash.empty()) {
            VDasher dasher(obj->mDash.data(), obj->mDash.size());
            mPath.clone(dasher.dashed(mPath));
        }
    }
}

void VDrawable::preprocess(const VRect &clip)
{
    if (mFlag & (DirtyState::Path)) {
        if (mType == Type::Fill) {
            mRasterizer.rasterize(std::move(mPath), mFillRule, clip);
        } else {
            applyDashOp();
            mRasterizer.rasterize(std::move(mPath), mStrokeInfo->cap, mStrokeInfo->join,
                                  mStrokeInfo->width, mStrokeInfo->miterLimit, clip);
        }
        mPath = {};
        mFlag &= ~DirtyFlag(DirtyState::Path);
    }
}

VRle VDrawable::rle()
{
    return mRasterizer.rle();
}

void VDrawable::setStrokeInfo(CapStyle cap, JoinStyle join, float miterLimit,
                              float strokeWidth)
{
    assert(mStrokeInfo);
    if ((mStrokeInfo->cap == cap) && (mStrokeInfo->join == join) &&
        vCompare(mStrokeInfo->miterLimit, miterLimit) &&
        vCompare(mStrokeInfo->width, strokeWidth))
        return;

    mStrokeInfo->cap = cap;
    mStrokeInfo->join = join;
    mStrokeInfo->miterLimit = miterLimit;
    mStrokeInfo->width = strokeWidth;
    mFlag |= DirtyState::Path;
}

void VDrawable::setDashInfo(std::vector<float> &dashInfo)
{
    assert(mStrokeInfo);
    assert(mType == VDrawable::Type::StrokeWithDash);

    auto obj = static_cast<StrokeWithDashInfo *>(mStrokeInfo);
    bool hasChanged = false;

    if (obj->mDash.size() == dashInfo.size()) {
        for (uint i = 0; i < dashInfo.size(); ++i) {
            if (!vCompare(obj->mDash[i], dashInfo[i])) {
                hasChanged = true;
                break;
            }
        }
    } else {
        hasChanged = true;
    }

    if (!hasChanged) return;

    obj->mDash = dashInfo;

    mFlag |= DirtyState::Path;
}

void VDrawable::setPath(const VPath &path)
{
    mPath = path;
    mFlag |= DirtyState::Path;
}

void memfill32(uint32_t *dest, uint32_t value, int length);
static void comp_func_solid_Source(uint32_t *dest, int length, uint32_t color,
                                   uint32_t const_alpha)
{
    int ialpha, i;

    if (const_alpha == 255) {
        memfill32(dest, color, length);
    } else {
        ialpha = 255 - const_alpha;
        color = BYTE_MUL(color, const_alpha);
        for (i = 0; i < length; ++i)
            dest[i] = color + BYTE_MUL(dest[i], ialpha);
    }
}

/*
r = s + d * sia
dest = r * ca + d * cia
=  (s + d * sia) * ca + d * cia
= s * ca + d * (sia * ca + cia)
= s * ca + d * (1 - sa*ca)
= s' + d ( 1 - s'a)
*/
static void comp_func_solid_SourceOver(uint32_t *dest, int length,
                                       uint32_t color,
                                       uint32_t const_alpha)
{
    int ialpha, i;

    if (const_alpha != 255) color = BYTE_MUL(color, const_alpha);
    ialpha = 255 - vAlpha(color);
    for (i = 0; i < length; ++i) dest[i] = color + BYTE_MUL(dest[i], ialpha);
}

/*
result = d * sa
dest = d * sa * ca + d * cia
= d * (sa * ca + cia)
*/
static void comp_func_solid_DestinationIn(uint *dest, int length, uint color,
                                          uint const_alpha)
{
    uint a = vAlpha(color);
    if (const_alpha != 255) {
        a = BYTE_MUL(a, const_alpha) + 255 - const_alpha;
    }
    for (int i = 0; i < length; ++i) {
        dest[i] = BYTE_MUL(dest[i], a);
    }
}

/*
result = d * sia
dest = d * sia * ca + d * cia
= d * (sia * ca + cia)
*/
static void comp_func_solid_DestinationOut(uint *dest, int length, uint color,
                                           uint const_alpha)
{
    uint a = vAlpha(~color);
    if (const_alpha != 255) a = BYTE_MUL(a, const_alpha) + 255 - const_alpha;
    for (int i = 0; i < length; ++i) {
        dest[i] = BYTE_MUL(dest[i], a);
    }
}

static void comp_func_Source(uint32_t *dest, const uint32_t *src, int length,
                             uint32_t const_alpha)
{
    if (const_alpha == 255) {
        memcpy(dest, src, size_t(length) * sizeof(uint));
    } else {
        uint ialpha = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            dest[i] =
                INTERPOLATE_PIXEL_255(src[i], const_alpha, dest[i], ialpha);
        }
    }
}

/* s' = s * ca
* d' = s' + d (1 - s'a)
*/
static void comp_func_SourceOver(uint32_t *dest, const uint32_t *src, int length, uint32_t const_alpha) {
    uint s, sia;

    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            s = src[i];
            if (s >= 0xff000000)
                dest[i] = s;
            else if (s != 0) {
                sia = vAlpha(~s);
                dest[i] = s + BYTE_MUL(dest[i], sia);
            }
        }
    } else {
        /* source' = source * const_alpha
        * dest = source' + dest ( 1- source'a)
        */
        for (int i = 0; i < length; ++i) {
            s = BYTE_MUL(src[i], const_alpha);
            sia = vAlpha(~s);
            dest[i] = s + BYTE_MUL(dest[i], sia);
        }
    }
}

static void comp_func_DestinationIn(uint *dest, const uint *src, int length, uint const_alpha) {
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            dest[i] = BYTE_MUL(dest[i], vAlpha(src[i]));
        }
    } else {
        uint cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint a = BYTE_MUL(vAlpha(src[i]), const_alpha) + cia;
            dest[i] = BYTE_MUL(dest[i], a);
        }
    }
}

static void comp_func_DestinationOut(uint *dest, const uint *src, int length, uint const_alpha) {
    if (const_alpha == 255) {
        for (int i = 0; i < length; ++i) {
            dest[i] = BYTE_MUL(dest[i], vAlpha(~src[i]));
        }
    } else {
        uint cia = 255 - const_alpha;
        for (int i = 0; i < length; ++i) {
            uint sia = BYTE_MUL(vAlpha(~src[i]), const_alpha) + cia;
            dest[i] = BYTE_MUL(dest[i], sia);
        }
    }
}

CompositionFunctionSolid COMP_functionForModeSolid_C[] = { comp_func_solid_Source, comp_func_solid_SourceOver, comp_func_solid_DestinationIn, comp_func_solid_DestinationOut};
CompositionFunction COMP_functionForMode_C[] = { comp_func_Source, comp_func_SourceOver, comp_func_DestinationIn, comp_func_DestinationOut};

void vInitBlendFunctions() {}

void VBitmap::Impl::reset(size_t width, size_t height, VBitmap::Format format)
{
    mRoData = nullptr;
    mWidth = uint(width);
    mHeight = uint(height);
    mFormat = format;

    mDepth = depth(format);
    mStride = ((mWidth * mDepth + 31) >> 5)
        << 2;  // bytes per scanline (must be multiple of 4)
    mOwnData = std::make_unique<uchar[]>(mStride * mHeight);
}

void VBitmap::Impl::reset(uchar *data, size_t width, size_t height, size_t bytesPerLine,
                          VBitmap::Format format)
{
    mRoData = data;
    mWidth = uint(width);
    mHeight = uint(height);
    mStride = uint(bytesPerLine);
    mFormat = format;
    mDepth = depth(format);
    mOwnData = nullptr;
}

uchar VBitmap::Impl::depth(VBitmap::Format format)
{
    uchar depth = 1;
    switch (format) {
    case VBitmap::Format::Alpha8:
    depth = 8;
    break;
    case VBitmap::Format::ARGB32:
    case VBitmap::Format::ARGB32_Premultiplied:
    depth = 32;
    break;
    default:
    break;
    }
    return depth;
}

void VBitmap::Impl::fill(uint /*pixel*/)
{
    //@TODO
}

void VBitmap::Impl::updateLuma()
{
    if (mFormat != VBitmap::Format::ARGB32_Premultiplied) return;
    auto dataPtr = data();
    for (uint col = 0; col < mHeight; col++) {
        uint *pixel = (uint *)(dataPtr + mStride * col);
        for (uint row = 0; row < mWidth; row++) {
            int alpha = vAlpha(*pixel);
            if (alpha == 0) {
                pixel++;
                continue;
            }

            int red = vRed(*pixel);
            int green = vGreen(*pixel);
            int blue = vBlue(*pixel);

            if (alpha != 255) {
                // un multiply
                red = (red * 255) / alpha;
                green = (green * 255) / alpha;
                blue = (blue * 255) / alpha;
            }
            int luminosity = int(0.299f * red + 0.587f * green + 0.114f * blue);
            *pixel = luminosity << 24;
            pixel++;
        }
    }
}

VBitmap::VBitmap(size_t width, size_t height, VBitmap::Format format)
{
    if (width <= 0 || height <= 0 || format == Format::Invalid) return;

    mImpl = std::make_shared<Impl>(width, height, format);
}

VBitmap::VBitmap(uchar *data, size_t width, size_t height, size_t bytesPerLine,
                 VBitmap::Format format)
{
    if (!data || width <= 0 || height <= 0 || bytesPerLine <= 0 ||
        format == Format::Invalid)
        return;

    mImpl = std::make_shared<Impl>(data, width, height, bytesPerLine, format);
}

void VBitmap::reset(uchar *data, size_t w, size_t h, size_t bytesPerLine,
                    VBitmap::Format format)
{
    if (mImpl) {
        mImpl->reset(data, w, h, bytesPerLine, format);
    } else {
        mImpl = std::make_shared<Impl>(data, w, h, bytesPerLine, format);
    }
}

void VBitmap::reset(size_t w, size_t h, VBitmap::Format format)
{
    if (mImpl) {
        if (w == mImpl->width() && h == mImpl->height() &&
            format == mImpl->format()) {
            return;
        }
        mImpl->reset(w, h, format);
    } else {
        mImpl = std::make_shared<Impl>(w, h, format);
    }
}

size_t VBitmap::stride() const
{
    return mImpl ? mImpl->stride() : 0;
}

size_t VBitmap::width() const
{
    return mImpl ? mImpl->width() : 0;
}

size_t VBitmap::height() const
{
    return mImpl ? mImpl->height() : 0;
}

size_t VBitmap::depth() const
{
    return mImpl ? mImpl->mDepth : 0;
}

uchar *VBitmap::data()
{
    return mImpl ? mImpl->data() : nullptr;
}

uchar *VBitmap::data() const
{
    return mImpl ? mImpl->data() : nullptr;
}

VRect VBitmap::rect() const
{
    return mImpl ? mImpl->rect() : VRect();
}

VSize VBitmap::size() const
{
    return mImpl ? mImpl->size() : VSize();
}

bool VBitmap::valid() const
{
    return mImpl.get();
}

VBitmap::Format VBitmap::format() const
{
    return mImpl ? mImpl->format() : VBitmap::Format::Invalid;
}

void VBitmap::fill(uint pixel)
{
    if (mImpl) mImpl->fill(pixel);
}

void VBitmap::updateLuma()
{
    if (mImpl) mImpl->updateLuma();
}

VGradient::VGradient(VGradient::Type type)
    : mType(type)
{
    if (mType == Type::Linear)
        linear.x1 = linear.y1 = linear.x2 = linear.y2 = 0.0f;
    else
        radial.cx = radial.cy = radial.fx =
        radial.fy = radial.cradius = radial.fradius = 0.0f;
}

void VGradient::setStops(const VGradientStops &stops)
{
    mStops = stops;
}

VBrush::VBrush(const VColor &color) : mType(VBrush::Type::Solid), mColor(color)
{
}

VBrush::VBrush(uchar r, uchar g, uchar b, uchar a)
    : mType(VBrush::Type::Solid), mColor(r, g, b, a)

{
}

VBrush::VBrush(const VGradient *gradient)
{
    if (!gradient) return;

    mGradient = gradient;

    if (gradient->mType == VGradient::Type::Linear) {
        mType = VBrush::Type::LinearGradient;
    } else if (gradient->mType == VGradient::Type::Radial) {
        mType = VBrush::Type::RadialGradient;
    }
}

VBrush::VBrush(const VTexture *texture):mType(VBrush::Type::Texture), mTexture(texture)
{
}

VBezier VBezier::fromPoints(const VPointF &p1, const VPointF &p2,
                            const VPointF &p3, const VPointF &p4)
{
    VBezier b;
    b.x1 = p1.x();
    b.y1 = p1.y();
    b.x2 = p2.x();
    b.y2 = p2.y();
    b.x3 = p3.x();
    b.y3 = p3.y();
    b.x4 = p4.x();
    b.y4 = p4.y();
    return b;
}

float VBezier::length() const
{
    VBezier left, right; /* bez poly splits */
    float   len = 0.0;   /* arc length */
    float   chord;       /* chord length */
    float   length;

    len = len + VLine::length(x1, y1, x2, y2);
    len = len + VLine::length(x2, y2, x3, y3);
    len = len + VLine::length(x3, y3, x4, y4);

    chord = VLine::length(x1, y1, x4, y4);

    if ((len - chord) > 0.01) {
        split(&left, &right);    /* split in two */
        length = left.length() + /* try left side */
            right.length(); /* try right side */

        return length;
    }

    return len;
}

VBezier VBezier::onInterval(float t0, float t1) const
{
    if (t0 == 0 && t1 == 1) return *this;

    VBezier bezier = *this;

    VBezier result;
    bezier.parameterSplitLeft(t0, &result);
    float trueT = (t1 - t0) / (1 - t0);
    bezier.parameterSplitLeft(trueT, &result);

    return result;
}

float VBezier::tAtLength(float l) const
{
    float       len = length();
    float       t = 1.0;
    const float error = 0.01f;
    if (l > len || vCompare(l, len)) return t;

    t *= 0.5;

    float lastBigger = 1.0;
    for (int num = 0; num < 100500; num++) {
        VBezier right = *this;
        VBezier left;
        right.parameterSplitLeft(t, &left);
        float lLen = left.length();
        if (fabs(lLen - l) < error) break;

        if (lLen < l) {
            t += (lastBigger - t) * 0.5f;
        } else {
            lastBigger = t;
            t -= t * 0.5f;
        }
    }
    return t;
}

void VBezier::splitAtLength(float len, VBezier *left, VBezier *right)
{
    float t;

    *right = *this;
    t = right->tAtLength(len);
    right->parameterSplitLeft(t, left);
}

VPointF VBezier::derivative(float t) const
{
    // p'(t) = 3 * (-(1-2t+t^2) * p0 + (1 - 4 * t + 3 * t^2) * p1 + (2 * t - 3 *
    // t^2) * p2 + t^2 * p3)

    float m_t = 1.0f - t;

    float d = t * t;
    float a = -m_t * m_t;
    float b = 1 - 4 * t + 3 * d;
    float c = 2 * t - 3 * d;

    return 3 * VPointF(a * x1 + b * x2 + c * x3 + d * x4,
                       a * y1 + b * y2 + c * y3 + d * y4);
}

float VBezier::angleAt(float t) const
{
    if (t < 0 || t > 1) {
        return 0;
    }
    return VLine({}, derivative(t)).angle();
}

void VBezier::coefficients(float t, float &a, float &b, float &c, float &d)
{
    float m_t = 1.0f - t;
    b = m_t * m_t;
    c = t * t;
    d = c * t;
    a = b * m_t;
    b *= 3.0f * t;
    c *= 3.0f * m_t;
}

VPointF VBezier::pointAt(float t) const
{
    // numerically more stable:
    float x, y;

    float m_t = 1.0f - t;
    {
        float a = x1 * m_t + x2 * t;
        float b = x2 * m_t + x3 * t;
        float c = x3 * m_t + x4 * t;
        a = a * m_t + b * t;
        b = b * m_t + c * t;
        x = a * m_t + b * t;
    }
    {
        float a = y1 * m_t + y2 * t;
        float b = y2 * m_t + y3 * t;
        float c = y3 * m_t + y4 * t;
        a = a * m_t + b * t;
        b = b * m_t + c * t;
        y = a * m_t + b * t;
    }
    return {x, y};
}

void VBezier::parameterSplitLeft(float t, VBezier *left)
{
    left->x1 = x1;
    left->y1 = y1;

    left->x2 = x1 + t * (x2 - x1);
    left->y2 = y1 + t * (y2 - y1);

    left->x3 = x2 + t * (x3 - x2);  // temporary holding spot
    left->y3 = y2 + t * (y3 - y2);  // temporary holding spot

    x3 = x3 + t * (x4 - x3);
    y3 = y3 + t * (y4 - y3);

    x2 = left->x3 + t * (x3 - left->x3);
    y2 = left->y3 + t * (y3 - left->y3);

    left->x3 = left->x2 + t * (left->x3 - left->x2);
    left->y3 = left->y2 + t * (left->y3 - left->y2);

    left->x4 = x1 = left->x3 + t * (x2 - left->x3);
    left->y4 = y1 = left->y3 + t * (y2 - left->y3);
}

void VBezier::split(VBezier *firstHalf, VBezier *secondHalf) const
{
    float c = (x2 + x3) * 0.5f;
    firstHalf->x2 = (x1 + x2) * 0.5f;
    secondHalf->x3 = (x3 + x4) * 0.5f;
    firstHalf->x1 = x1;
    secondHalf->x4 = x4;
    firstHalf->x3 = (firstHalf->x2 + c) * 0.5f;
    secondHalf->x2 = (secondHalf->x3 + c) * 0.5f;
    firstHalf->x4 = secondHalf->x1 = (firstHalf->x3 + secondHalf->x2) * 0.5f;

    c = (y2 + y3) / 2;
    firstHalf->y2 = (y1 + y2) * 0.5f;
    secondHalf->y3 = (y3 + y4) * 0.5f;
    firstHalf->y1 = y1;
    secondHalf->y4 = y4;
    firstHalf->y3 = (firstHalf->y2 + c) * 0.5f;
    secondHalf->y2 = (secondHalf->y3 + c) * 0.5f;
    firstHalf->y4 = secondHalf->y1 = (firstHalf->y3 + secondHalf->y2) * 0.5f;
}

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

RjValue::RjValue()
{
    v_ = new rapidjson::Value();
}

static rapidjson::Value& vcast(void* p) { return *(rapidjson::Value*)p; }
void RjValue::SetNull() { vcast(v_).SetNull(); }
void RjValue::SetBool(bool b) { vcast(v_).SetBool(b); }
void RjValue::SetInt(int i) { vcast(v_).SetInt(i); }
void RjValue::SetInt64(int64_t i) { vcast(v_).SetInt64(i); }
void RjValue::SetUint64(uint64_t i) { vcast(v_).SetUint64(i); }
void RjValue::SetUint(unsigned int i) { vcast(v_).SetUint(i); }
void RjValue::SetDouble(double i) { vcast(v_).SetDouble(i); }
void RjValue::SetString(const char* str,size_t length) { vcast(v_).SetString(str, static_cast<rapidjson::SizeType>(length)); }

RjValue::~RjValue()
{
    rapidjson::Value* d = (rapidjson::Value*)v_;
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
    ss_ = new rapidjson::InsituStringStream(str);
}

RjReader::RjReader() { r_ = new rapidjson::Reader(); }

static rapidjson::Reader& rcast(void* p) { return *(rapidjson::Reader*)p; }
void RjReader::IterativeParseInit() { rcast(r_).IterativeParseInit(); }
bool RjReader::HasParseError() const { return rcast(r_).HasParseError(); }

bool RjReader::IterativeParseNext(int parseFlags, RjInsituStringStream& ss_, LookaheadParserHandlerBase& handler)
{
    if (parseFlags == (rapidjson::kParseDefaultFlags | rapidjson::kParseInsituFlag))
        return rcast(r_).IterativeParseNext<rapidjson::kParseDefaultFlags|rapidjson::kParseInsituFlag>(*(rapidjson::InsituStringStream*)(ss_.ss_), handler);
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
    bool RawNumber(const char *, rapidjson::SizeType, bool) { return false; }
    bool String(const char *str, rapidjson::SizeType length, bool)
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
    bool Key(const char *str, rapidjson::SizeType length, bool)
    {
        st_ = kHasKey;
        v_.SetString(str, length);
        return true;
    }
    bool EndObject(rapidjson::SizeType)
    {
        st_ = kExitingObject;
        return true;
    }
    bool StartArray()
    {
        st_ = kEnteringArray;
        return true;
    }
    bool EndArray(rapidjson::SizeType)
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
        return rapidjson::kArrayType;
    }

    if (st_ == kEnteringObject) {
        return rapidjson::kObjectType;
    }

    return -1;
}

void LottieParserImpl::Skip(const char * /*key*/)
{
    if (PeekType() == rapidjson::kArrayType) {
        EnterArray();
        SkipArray();
    } else if (PeekType() == rapidjson::kObjectType) {
        EnterObject();
        SkipObject();
    } else {
        SkipValue();
    }
}

LottieBlendMode LottieParserImpl::getBlendMode()
{
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
    EnterObject();
    std::shared_ptr<LOTCompositionData> sharedComposition =
        std::make_shared<LOTCompositionData>();
    LOTCompositionData *comp = sharedComposition.get();
    compRef = comp;
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "v")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
            comp->mVersion = std::string(GetString());
        } else if (0 == strcmp(key, "w")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            comp->mSize.setWidth(GetInt());
        } else if (0 == strcmp(key, "h")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            comp->mSize.setHeight(GetInt());
        } else if (0 == strcmp(key, "ip")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            comp->mStartFrame = (long)GetDouble();
        } else if (0 == strcmp(key, "op")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            comp->mEndFrame = (long)GetDouble();
        } else if (0 == strcmp(key, "fr")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
    EnterObject();
    std::string comment;
    int         timeframe{0};
    int          duration{0};
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "cm")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
            comment = std::string(GetString());
        } else if (0 == strcmp(key, "tm")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            timeframe = (int)GetDouble();
        } else if (0 == strcmp(key, "dr")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        parseMarker();
    }
    // update the precomp layers with the actual layer object
}

void LottieParserImpl::parseAssets(LOTCompositionData *composition)
{
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);

    auto                      asset = allocator().make<LOTAsset>();
    std::string               filename;
    std::string               relativePath;
    bool                      embededResource = false;
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "w")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            asset->mWidth = GetInt();
        } else if (0 == strcmp(key, "h")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            asset->mHeight = GetInt();
        } else if (0 == strcmp(key, "p")) { /* image name */
            asset->mAssetType = LOTAsset::Type::Image;
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
            filename = std::string(GetString());
        } else if (0 == strcmp(key, "u")) { /* relative image path */
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
            relativePath = std::string(GetString());
        } else if (0 == strcmp(key, "e")) { /* relative image path */
            embededResource = GetInt();
        } else if (0 == strcmp(key, "id")) { /* reference id*/
            if (PeekType() == rapidjson::kStringType) {
                asset->mRefId = std::string(GetString());
            } else {
                RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
                asset->mRefId = toString(GetInt()).c_str();
            }
        } else if (0 == strcmp(key, "layers")) {
            asset->mAssetType = LOTAsset::Type::Precomp;
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
    LOTLayerData *layer = allocator().make<LOTLayerData>();
    curLayerRef = layer;
    bool ddd = true;
    EnterObject();
    while (const char *key = NextObjectKey()) {
        if (0 == strcmp(key, "ty")) { /* Type of layer*/
            layer->mLayerType = getLayerType();
        } else if (0 == strcmp(key, "nm")) { /*Layer name*/
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
            layer->setName(GetString());
        } else if (0 == strcmp(key, "ind")) { /*Layer index in AE. Used for
                                              parenting and expressions.*/
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            layer->mId = GetInt();
        } else if (0 == strcmp(key, "ddd")) { /*3d layer */
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            ddd = GetInt();
        } else if (0 ==
                   strcmp(key,
                   "parent")) { /*Layer Parent. Uses "ind" of parent.*/
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            layer->mParentId = GetInt();
        } else if (0 == strcmp(key, "refId")) { /*preComp Layer reference id*/
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
            layer->extra()->mPreCompRefId = std::string(GetString());
            layer->mHasGradient = true;
            mLayersToUpdate.push_back(layer);
        } else if (0 == strcmp(key, "sr")) {  // "Layer Time Stretching"
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            layer->mTimeStreatch = (float)GetDouble();
        } else if (0 == strcmp(key, "tm")) {  // time remapping
            parseProperty(layer->extra()->mTimeRemap);
        } else if (0 == strcmp(key, "ip")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            layer->mInFrame = std::lround((float)GetDouble());
        } else if (0 == strcmp(key, "op")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            layer->mOutFrame = std::lround((float)GetDouble());
        } else if (0 == strcmp(key, "st")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
            layer->mStartFrame = (int)GetDouble();
        } else if (0 == strcmp(key, "bm")) {
            layer->mBlendMode = getBlendMode();
        } else if (0 == strcmp(key, "ks")) {
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        layer->extra()->mMasks.push_back(parseMaskObject());
    }
}

LOTMaskData* LottieParserImpl::parseMaskObject()
{
    auto obj = allocator().make<LOTMaskData>();

    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        parseObject(layer);
    }
}

LOTData* LottieParserImpl::parseObjectTypeAttr()
{
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
            EnterArray();
            while (NextArrayValue()) {
                RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
        RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
            RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
    EnterArray();
    while (NextArrayValue()) {
        RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
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

    if (PeekType() == rapidjson::kArrayType) EnterArray();

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
    if (PeekType() == rapidjson::kArrayType) {
        EnterArray();
        if (NextArrayValue()) val = (float)GetDouble();
        // discard rest
        while (NextArrayValue()) {
            GetDouble();
        }
    } else if (PeekType() == rapidjson::kNumberType) {
        val = (float)GetDouble();
    } else {
        RAPIDJSON_ASSERT(0);
    }
}

void LottieParserImpl::getValue(LottieColor &color)
{
    float val[4] = {0.f};
    int   i = 0;
    if (PeekType() == rapidjson::kArrayType) EnterArray();

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
    if (PeekType() == rapidjson::kArrayType) EnterArray();

    while (NextArrayValue()) {
        grad.mGradient.push_back((float)GetDouble());
    }
}

void LottieParserImpl::getValue(int &val)
{
    if (PeekType() == rapidjson::kArrayType) {
        EnterArray();
        while (NextArrayValue()) {
            val = GetInt();
        }
    } else if (PeekType() == rapidjson::kNumberType) {
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
    bool arrayWrapper = (PeekType() == rapidjson::kArrayType);
    if (arrayWrapper) EnterArray();

    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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

VInterpolator* LottieParserImpl::interpolator(VPointF inTangent, VPointF outTangent, const char* key)
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
            if (PeekType() == rapidjson::kStringType) {
                parsed.interpolatorKey = GetString();
            } else {
                RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
                EnterArray();
                while (NextArrayValue()) {
                    RAPIDJSON_ASSERT(PeekType() == rapidjson::kStringType);
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
            obj.mKeyFrames.back().mValue.mEndValue = keyframe.mValue.mStartValue;
        }
    }

    if (parsed.hold) {
        keyframe.mValue.mEndValue = keyframe.mValue.mStartValue;
        keyframe.mEndFrame = keyframe.mStartFrame;
        obj.mKeyFrames.push_back(std::move(keyframe));
    } else if (parsed.interpolator) {
        keyframe.mInterpolator = interpolator(inTangent, outTangent, parsed.interpolatorKey.size() > 0 ? parsed.interpolatorKey.c_str() : "unk");
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
            if (PeekType() == rapidjson::kArrayType) {
                EnterArray();
                while (NextArrayValue()) {
                    RAPIDJSON_ASSERT(PeekType() == rapidjson::kObjectType);
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
    if (PeekType() == rapidjson::kNumberType) {
        if (!obj.isStatic()) {
            RAPIDJSON_ASSERT(false);
            st_ = kError;
            return;
        }
        /*single value property with no animation*/
        getValue(obj.value());
    } else {
        RAPIDJSON_ASSERT(PeekType() == rapidjson::kArrayType);
        EnterArray();
        while (NextArrayValue()) {
            /* property with keyframe info*/
            if (PeekType() == rapidjson::kObjectType) {
                parseKeyFrame(obj.animation());
            } else {
                /* Read before modifying.
                * as there is no way of knowing if the
                * value of the array is either array of numbers
                * or array of object without entering the array
                * thats why this hack is there
                */
                RAPIDJSON_ASSERT(PeekType() == rapidjson::kNumberType);
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

LOTDrawable::~LOTDrawable() {
    if (mCNode && mCNode->mGradient.stopPtr)
        free(mCNode->mGradient.stopPtr);
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


} // imlottie