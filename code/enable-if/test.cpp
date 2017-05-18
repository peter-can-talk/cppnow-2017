#include <type_traits>

template<typename T> 
typename std::enable_if<std::is_integral<T>::value>::type f(T& value) {
  value += 1;
}
