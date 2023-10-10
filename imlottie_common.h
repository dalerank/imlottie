#pragma once

namespace imlottie {

struct VDebug {
    template<typename Args>
    VDebug &operator<<(const Args &) { return *this; }
};

#define vDebug VDebug()
#define vWarning VDebug()
#define vCritical VDebug()

}