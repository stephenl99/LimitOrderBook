#ifndef LIMITORDERBOOK_HELPERS_H
#define LIMITORDERBOOK_HELPERS_H

#include <cstdint>

class Helpers {
public:
    template <typename R, typename T>
    static R convert(T* ptr, int size)
    {
        R ans = 0;
        for (int i = 0; i < size; ++i) {
            ans = (ans << 8) | static_cast<R>(ptr[i]);
        }
        return ans;
    }
};

#endif
