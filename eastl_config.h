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

#include <EASTL/vector.h>
#include <EASTL/unordered_map.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/shared_ptr.h>
#include <EASTL/bitset.h>
#include <EASTL/array.h>
#include <EASTL/string.h>

#include <EASTL/ratio.h>
#include <EASTL/chrono.h>
#include <EASTL/iterator.h>
#include <EASTL/type_traits.h>
#include <EASTL/numeric_limits.h>
#include <EASTL/internal/type_properties.h>

#include <atomic>
#include <future>

namespace imlottie
{

namespace std
{
using namespace eastl;
using string = eastl::string;
template <class T> using unique_ptr = eastl::unique_ptr<T>;
template <class T> using shared_ptr = eastl::shared_ptr<T>;
template <class T> using vector = eastl::vector<T>;
template <class T> using atomic = ::std::atomic<T>;

template <typename... Args> using pair = eastl::pair<Args...>;
template <typename... Args> using tuple = eastl::tuple<Args...>;
template <typename... Args> using promise = ::std::promise<Args...>;
template <typename... Args> using future = ::std::future<Args...>;

template<class T> T atan2 ( T y, T x ) { return ::atan2(y, x); }
template<class T> T cos ( T arg ) { return ::cos( arg ); }
template<class T> T sin ( T arg ) { return ::sin( arg ); }
template<class T> long int lround (T x) { return ::lround(x); }
template<class T> T sqrt (T x) { return ::sqrt(x); }
template<class T> T fmod (T x, T y) { return ::fmod(x, y); }
template<class T> T fabs(T arg) { return ::fabs(arg); }
template<class T> T abs(T arg) { return ::abs(arg); }
template<class T> T pow( T base, T exp ) { return ::pow(base, exp); }

using size_t = ::size_t;


inline long      strtol( const char *str, char **str_end, int base ) { return ::strtol(str, str_end, base);}
inline long long strtoll( const char *str, char **str_end, int base ){ return ::strtoll(str, str_end, base);}
}

} // imlottie