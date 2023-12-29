//
// Created by Aaron Gill-Braun on 2023-05-22.
//

#ifndef LIB_MACROS_H
#define LIB_MACROS_H

// https://github.com/swansontec/map-macro/blob/master/map.h
#define __macro_EVAL0(...) __VA_ARGS__
#define __macro_EVAL1(...) __macro_EVAL0(__macro_EVAL0(__macro_EVAL0(__VA_ARGS__)))
#define __macro_EVAL2(...) __macro_EVAL1(__macro_EVAL1(__macro_EVAL1(__VA_ARGS__)))
#define __macro_EVAL3(...) __macro_EVAL2(__macro_EVAL2(__macro_EVAL2(__VA_ARGS__)))
#define __macro_EVAL4(...) __macro_EVAL3(__macro_EVAL3(__macro_EVAL3(__VA_ARGS__)))
#define __macro_EVAL(...)  __macro_EVAL4(__macro_EVAL4(__macro_EVAL4(__VA_ARGS__)))

#define __macro_MAP_END(...)
#define __macro_MAP_OUT
#define __macro_MAP_COMMA ,

#define __macro_MAP_GET_END2() 0, __macro_MAP_END
#define __macro_MAP_GET_END1(...) __macro_MAP_GET_END2
#define __macro_MAP_GET_END(...) __macro_MAP_GET_END1
#define __macro_MAP_NEXT0(test, next, ...) next __macro_MAP_OUT
#define __macro_MAP_NEXT1(test, next) __macro_MAP_NEXT0(test, next, 0)
#define __macro_MAP_NEXT(test, next)  __macro_MAP_NEXT1(__macro_MAP_GET_END test, next)

#define __macro_MAP0(f, x, peek, ...) f(x) __macro_MAP_NEXT(peek, __macro_MAP1)(f, peek, __VA_ARGS__)
#define __macro_MAP1(f, x, peek, ...) f(x) __macro_MAP_NEXT(peek, __macro_MAP0)(f, peek, __VA_ARGS__)

#define __macro_MAP_LIST_NEXT1(test, next) __macro_MAP_NEXT0(test, __macro_MAP_COMMA next, 0)
#define __macro_MAP_LIST_NEXT(test, next)  __macro_MAP_LIST_NEXT1(__macro_MAP_GET_END test, next)
#define __macro_MAP_LIST0(f, x, peek, ...) f(x) __macro_MAP_LIST_NEXT(peek, __macro_MAP_LIST1)(f, peek, __VA_ARGS__)
#define __macro_MAP_LIST1(f, x, peek, ...) f(x) __macro_MAP_LIST_NEXT(peek, __macro_MAP_LIST0)(f, peek, __VA_ARGS__)
//
#define __macro_MAP_LIST_NEXT1_S(sep, test, next) __macro_MAP_NEXT0(test, sep next, 0)
#define __macro_MAP_LIST_NEXT_S(sep, test, next)  __macro_MAP_LIST_NEXT1_S(sep, __macro_MAP_GET_END test, next)
#define __macro_MAP_LIST0_S(f, sep, x, peek, ...) f(x) __macro_MAP_LIST_NEXT_S(sep, peek, __macro_MAP_LIST1_S)(f, sep, peek, __VA_ARGS__)
#define __macro_MAP_LIST1_S(f, sep, x, peek, ...) f(x) __macro_MAP_LIST_NEXT_S(sep, peek, __macro_MAP_LIST0_S)(f, sep, peek, __VA_ARGS__)

#define __macro_ident(x) x
#define __macro_str(x) #x

#define MACRO_JOIN(...) MACRO_MAP_LIST(__macro_ident, __VA_ARGS__)

/**
 * Applies the function macro `f` to each of the remaining parameters.
 */
#define MACRO_MAP(f, ...) __macro_EVAL(__macro_MAP1(f, __VA_ARGS__, ()()(), ()()(), ()()(), 0))

/**
 * Applies the function macro `f` to each of the remaining parameters and
 * inserts commas between the results.
 */
#define MACRO_MAP_LIST(f, ...) __macro_EVAL(__macro_MAP_LIST1(f, __VA_ARGS__, ()()(), ()()(), ()()(), 0))

/**
 * Applies the function macro `f` to each of the remaining parameters and
 * inserts the separator `sep` between the results.
 */
#define MACRO_MAP_JOIN(f, sep, ...) __macro_EVAL(__macro_MAP_LIST1_S(f, sep, __VA_ARGS__, ()()(), ()()(), ()()(), 0))

#endif
